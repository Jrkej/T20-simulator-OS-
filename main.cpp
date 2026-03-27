//
// Created by jrke on 3/19/26.
// Fixed: deadlocks, race conditions, thread lifecycle management
//
// Architecture: All coordination uses a single mutex (GM) + condvar (GM_CV).
// A `gameState` enum is the sole predicate for all cond_waits.
// Only one bowler/batsman claims each slot because we atomically check
// and transition the state under the lock.
//
#include <bits/stdc++.h>
#include <iostream>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

using namespace std;

// Forward declarations
class batsman;
class bowler;
class team;
class game;
void *batsmanApi(void *);
void *bowlersApi(void *);

// ============================================================
// State machine
// ============================================================
enum State {
  NEED_BOWLER,   // play() needs a bowler to step up
  NEED_BOWL_CMD, // bowler ready, play() should tell it to bowl
  BOWLING,       // bowler is bowling
  BALL_DONE,     // bowler finished delivery
  NEED_BATTER,   // play() needs a batsman to step up
  NEED_BAT_CMD,  // batsman ready, play() should tell it to bat
  BATTING,       // batsman is batting
  SHOT_DONE,     // batsman finished the shot
  INNING_OVER,   // inning ended, all threads should exit
};

pthread_mutex_t GM = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t GM_CV = PTHREAD_COND_INITIALIZER;
State gameState;

// Global game pointers
team *battingTeam = nullptr;
team *bowlingTeam = nullptr;
bowler *liveBowler = nullptr;
batsman *liveBatsman = nullptr;
sem_t onfield;

// Bowler turn-taking (ensures only one bowler at a time on the pitch)
pthread_mutex_t NEXTBOWLER = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t pitchMtx = PTHREAD_MUTEX_INITIALIZER;

// ============================================================
// Classes
// ============================================================
class batsman {
public:
  pthread_t mainThread;
  bool notout;
  int runs;
  int balls;
  string name;
  int lastBall;

  void init(string myName) {
    runs = 0;
    balls = 0;
    name = myName;
    lastBall = 0;
    notout = true;
    mainThread = 0;
  }
};

class bowler {
public:
  pthread_t mainThread;
  string name;
  int balls;
  int runs;

  void init(string myName) {
    name = myName;
    balls = 0;
    runs = 0;
    mainThread = 0;
  }
};

class team {
public:
  vector<batsman> batsmans;
  vector<bowler> bowlers;
  int score;
  int balls;
  int wickets;

  team() {
    score = 0;
    balls = 0;
    wickets = 0;
    batsmans.resize(11);
    bowlers.resize(11);
  }
};

bool inningAlive() {
  return battingTeam->balls < 120 && battingTeam->wickets < 10;
}

// ============================================================
// Game class
// ============================================================
class game {
public:
  vector<team> teams;
  int field;

  game() {
    field = rand() % 2;
    teams.resize(2);
  }

  void firstInningInitial() {
    battingTeam = &teams[field];
    bowlingTeam = &teams[field ^ 1];
    for (int i = 0; i < 11; i++) {
      teams[field].batsmans[i].init("Bat_" + to_string(i));
      teams[field ^ 1].bowlers[i].init("Bowl_" + to_string(i));
      pthread_create(&teams[field].batsmans[i].mainThread, NULL, batsmanApi,
                     &teams[field].batsmans[i]);
      pthread_create(&teams[field ^ 1].bowlers[i].mainThread, NULL, bowlersApi,
                     &teams[field ^ 1].bowlers[i]);
    }
  }

  void play() {
    pthread_mutex_lock(&GM);

    while (inningAlive()) {
      // === Need a bowler ===
      gameState = NEED_BOWLER;
      pthread_cond_broadcast(&GM_CV);

      while (gameState == NEED_BOWLER)
        pthread_cond_wait(&GM_CV, &GM);
      if (gameState == INNING_OVER || !inningAlive())
        break;
      // gameState == NEED_BOWL_CMD: bowler is ready

      // === Tell bowler to bowl ===
      gameState = BOWLING;
      pthread_cond_broadcast(&GM_CV);

      while (gameState == BOWLING)
        pthread_cond_wait(&GM_CV, &GM);
      if (gameState == INNING_OVER || !inningAlive())
        break;
      // gameState == BALL_DONE

      // === Need a batsman ===
      gameState = NEED_BATTER;
      pthread_cond_broadcast(&GM_CV);

      while (gameState == NEED_BATTER)
        pthread_cond_wait(&GM_CV, &GM);
      if (gameState == INNING_OVER || !inningAlive())
        break;
      // gameState == NEED_BAT_CMD: batsman is ready

      // === Tell batsman to bat ===
      gameState = BATTING;
      pthread_cond_broadcast(&GM_CV);

      while (gameState == BATTING)
        pthread_cond_wait(&GM_CV, &GM);
      if (gameState == INNING_OVER || !inningAlive())
        break;
      // gameState == SHOT_DONE

      // === Process result ===
      teams[field].score += liveBatsman->lastBall;

      // Handle wicket (lastBall == 7 = out)
      if (liveBatsman->lastBall == 7) {
        battingTeam->wickets++;
        liveBatsman->notout = false;
        // The current batsman will see notout==false and exit on next loop
      }
    }

    gameState = INNING_OVER;
    pthread_cond_broadcast(&GM_CV);
    pthread_mutex_unlock(&GM);

    // Unblock waiting batsmen
    for (int i = 0; i < 11; i++)
      sem_post(&onfield);

    // Unblock bowlers stuck on pitch/NEXTBOWLER
    if (pthread_mutex_trylock(&pitchMtx) == 0)
      pthread_mutex_unlock(&pitchMtx);
    if (pthread_mutex_trylock(&NEXTBOWLER) == 0)
      pthread_mutex_unlock(&NEXTBOWLER);

    // Re-broadcast in case threads woke but missed the INNING_OVER
    pthread_mutex_lock(&GM);
    pthread_cond_broadcast(&GM_CV);
    pthread_mutex_unlock(&GM);

    cout << teams[field].balls << " - " << teams[field].score << " - "
         << teams[field].wickets << endl;

    // Join all threads
    for (int i = 0; i < 11; i++)
      pthread_join(teams[field].batsmans[i].mainThread, NULL);
    for (int i = 0; i < 11; i++)
      pthread_join(teams[field ^ 1].bowlers[i].mainThread, NULL);
  }
};

// ============================================================
// Batsman thread
// ============================================================
void *batsmanApi(void *arg) {
  batsman *me = (batsman *)arg;

  sem_wait(&onfield);

  pthread_mutex_lock(&GM);
  while (me->notout && inningAlive() && gameState != INNING_OVER) {
    // Wait until play() announces NEED_BATTER
    while (gameState != NEED_BATTER && gameState != INNING_OVER) {
      pthread_cond_wait(&GM_CV, &GM);
    }
    if (gameState == INNING_OVER)
      break;

    // ATOMIC CLAIM: only one batsman gets this
    // gameState is guaranteed == NEED_BATTER here (we hold GM).
    // We set it to NEED_BAT_CMD to claim the slot. Any other batsman
    // that wakes will see NEED_BAT_CMD != NEED_BATTER and loop back.
    liveBatsman = me;
    gameState = NEED_BAT_CMD;
    pthread_cond_broadcast(&GM_CV);

    // Wait for play() to tell us to bat
    while (gameState == NEED_BAT_CMD)
      pthread_cond_wait(&GM_CV, &GM);
    if (gameState == INNING_OVER)
      break;
    if (gameState != BATTING)
      continue; // shouldn't happen, but be safe

    // Generate shot
    me->lastBall = rand() % 10;
    me->balls++;

    // Signal done
    gameState = SHOT_DONE;
    pthread_cond_broadcast(&GM_CV);

    // Wait for play() to process (it will set NEED_BOWLER for next ball,
    // or INNING_OVER). We stay in this wait until NEED_BATTER or INNING_OVER.
    while (gameState != NEED_BATTER && gameState != INNING_OVER) {
      pthread_cond_wait(&GM_CV, &GM);
    }

    // Check if we were declared out
    if (!me->notout)
      break;
  }
  pthread_mutex_unlock(&GM);

  sem_post(&onfield);
  return nullptr;
}

// ============================================================
// Bowler thread
// ============================================================
void *bowlersApi(void *arg) {
  bowler *me = (bowler *)arg;

  for (int over = 0; over < 4; over++) {
    // Acquire the bowling turn
    pthread_mutex_lock(&NEXTBOWLER);
    pthread_mutex_lock(&pitchMtx);
    pthread_mutex_unlock(&NEXTBOWLER);

    // Quick check before entering the inner loop
    pthread_mutex_lock(&GM);
    if (gameState == INNING_OVER || !inningAlive()) {
      pthread_mutex_unlock(&GM);
      pthread_mutex_unlock(&pitchMtx);
      return nullptr;
    }
    pthread_mutex_unlock(&GM);

    liveBowler = me;

    for (int ball = 0; ball < 6; ball++) {
      pthread_mutex_lock(&GM);

      // Wait until play() announces NEED_BOWLER
      while (gameState != NEED_BOWLER && gameState != INNING_OVER) {
        pthread_cond_wait(&GM_CV, &GM);
      }
      if (gameState == INNING_OVER || !inningAlive()) {
        pthread_mutex_unlock(&GM);
        pthread_mutex_unlock(&pitchMtx);
        return nullptr;
      }

      // ATOMIC CLAIM: set NEED_BOWL_CMD
      gameState = NEED_BOWL_CMD;
      pthread_cond_broadcast(&GM_CV);

      // Wait for play() to tell us to bowl
      while (gameState == NEED_BOWL_CMD)
        pthread_cond_wait(&GM_CV, &GM);
      if (gameState == INNING_OVER) {
        pthread_mutex_unlock(&GM);
        pthread_mutex_unlock(&pitchMtx);
        return nullptr;
      }

      // BOWLING: deliver the ball
      battingTeam->balls++;
      me->balls++;

      // Signal done
      gameState = BALL_DONE;
      pthread_cond_broadcast(&GM_CV);

      // Wait until play() moves past BALL_DONE (sets NEED_BATTER or
      // NEED_BOWLER for next delivery). We wait until NEED_BOWLER or
      // INNING_OVER so we can deliver the next ball in this over.
      while (gameState != NEED_BOWLER && gameState != INNING_OVER) {
        pthread_cond_wait(&GM_CV, &GM);
      }
      if (gameState == INNING_OVER || !inningAlive()) {
        pthread_mutex_unlock(&GM);
        pthread_mutex_unlock(&pitchMtx);
        return nullptr;
      }
      pthread_mutex_unlock(&GM);
    }

    pthread_mutex_unlock(&pitchMtx);
  }
  return nullptr;
}

// ============================================================
// Reset & Main
// ============================================================
void resetGlobalState() {
  GM = PTHREAD_MUTEX_INITIALIZER;
  GM_CV = PTHREAD_COND_INITIALIZER;
  NEXTBOWLER = PTHREAD_MUTEX_INITIALIZER;
  pitchMtx = PTHREAD_MUTEX_INITIALIZER;

  gameState = NEED_BOWLER;
  liveBowler = nullptr;
  liveBatsman = nullptr;
  battingTeam = nullptr;
  bowlingTeam = nullptr;
}

int main() {
  for (int i = 0; i < 500; i++) {
    cout << "Starting Match " << i + 1 << endl;

    resetGlobalState();
    srand(time(NULL) + i);
    sem_init(&onfield, 0, 2);

    game engine;
    engine.firstInningInitial();
    engine.play();

    sem_destroy(&onfield);
    cout << "DONE   Match " << i + 1 << endl;
  }
  return 0;
}