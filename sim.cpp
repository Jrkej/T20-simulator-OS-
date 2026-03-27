//
// Created by jrke on 3/19/26.
//
#include <bits/stdc++.h>
#include <pthread.h>
#include <iostream>
#include <unistd.h>
#include <semaphore.h>

using namespace std;

class bowlType;
class batsman;
class bowler;
class fielder;
class team;
class game;
void *batsmanApi(void *);
void *bowlersApi(void *);
void *fieldersApi(void *);
bool inningRun();

int RUNOUT_WAIT = 80;
int BALL_WAIT = 100000;

pthread_mutex_t GLOB = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t NEXTBOWLER = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t BOWLER = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t BATTER = PTHREAD_MUTEX_INITIALIZER;

vector<pthread_mutex_t> creases(2, PTHREAD_MUTEX_INITIALIZER);
bowler *liveBowler;
batsman *liveBatsman;

team *battingTeam, *bowlingTeam;
sem_t onfield;
pthread_cond_t READY = PTHREAD_COND_INITIALIZER;
pthread_cond_t BALL = PTHREAD_COND_INITIALIZER;
pthread_mutex_t pitch = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t FIELD = PTHREAD_COND_INITIALIZER;
pthread_cond_t READYOUT = PTHREAD_COND_INITIALIZER;
pthread_cond_t READYBAT = PTHREAD_COND_INITIALIZER;
bool RUN = false;


vector<string> commentries;
int currCrease = 0;
bool out = false;
int TARGET;

class batsman {
public:
    pthread_mutex_t out, act, run;
    bool notout;
    int crease;
    pthread_t mainThread;
    pthread_cond_t OUT, ACT;
    int myCrease;
    int runs;
    int balls;
    string name, outType;
    int fours;
    int six;
    int lastBall;

    void batsmans_init(string myName) {
        out = PTHREAD_MUTEX_INITIALIZER;
        act = PTHREAD_MUTEX_INITIALIZER;
        run = PTHREAD_MUTEX_INITIALIZER;
        OUT = PTHREAD_COND_INITIALIZER;
        ACT = PTHREAD_COND_INITIALIZER;

        runs = 0;
        myCrease = 0;
        balls = 0;
        name = myName;
        fours = 0;
        six = 0;
        lastBall = 0;
        notout = true;
        myCrease = 0;
        outType = "YET TO BAT";
    }
};

class bowler {
public:
    pthread_mutex_t act;
    pthread_t mainThread;

    string name;
    int balls;
    int runs;
    int wickets;

    void bowler_init(string myName) {
        act = PTHREAD_MUTEX_INITIALIZER;
        name = myName;
        balls = 0;
        runs = 0;
        wickets = 0;
    }
};

class fielder {
public:
    pthread_mutex_t act, ref;
    pthread_t mainThread;

    string name;

    void fielder_init(string myName) {
        act = PTHREAD_MUTEX_INITIALIZER;
        ref = PTHREAD_MUTEX_INITIALIZER;
        name = myName;
    }
};

class team {
public:
    vector<batsman> batsmans;
    vector<bowler> bowlers;
    vector<fielder> fielders;

    int score;
    int balls;
    int wickets;
    string name;

    team() {
        score = 0;
        balls = 0;
        wickets = 0;
        batsmans.resize(11);
        bowlers.resize(11);
        fielders.resize(11);
    }
};

class game {
public:
    vector<team> teams;
    int field;
    int inning;

    game() {
        field = rand() % 2;
        teams.resize(2);
        inning = 0;
        TARGET = 100000;
    }

    void firstInningInitial() {
        teams[0].name = "India";
        teams[1].name = "Australia";
        battingTeam = &teams[field];
        bowlingTeam = &teams[field ^ 1];
        vector<string> BNames = {
            "Usman Khwaja",
            "Aeron Finch",
            "Alex Carry",
            "Glen Maxwell",
            "Steve Smith",
            "David Warner",
            "J. Richardson",
            "M. Stoinis",
            "M. Starc",
            "A. Zampa",
            "N. Lyon"
        };
        vector<string> ANames = {
            "Rohit Sharma",
            "Shikhar Dhawan",
            "Virat Kohli",
            "Yuvraj Singh",
            "Suresh Raina",
            "M.S. Dhoni",
            "R. Jadeja",
            "H. Pandya",
            "M. Shami",
            "Jasprit Bumrah",
            "B. Kumar"
        };
        for (int i = 0; i < 11; i++) {
            teams[0].batsmans[i].batsmans_init(ANames[i]);
            teams[0].bowlers[i].bowler_init(ANames[i]);
            teams[0].fielders[i].fielder_init(ANames[i]);

            teams[1].batsmans[i].batsmans_init(BNames[i]);
            teams[1].bowlers[i].bowler_init(BNames[i]);
            teams[1].fielders[i].fielder_init(BNames[i]);
        }
        for (int i = 0; i < 11; i++) {
            pthread_create(&teams[field].batsmans[i].mainThread, NULL, batsmanApi, &teams[field].batsmans[i]);
            pthread_create(&teams[field ^ 1].bowlers[i].mainThread, NULL, bowlersApi, &teams[field ^ 1].bowlers[i]);
            pthread_create(&teams[field ^ 1].fielders[i].mainThread, NULL, fieldersApi, &teams[field ^ 1].fielders[i]);
        }
    }

    void play() {
        GLOB = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&GLOB);
        while (inningRun()) {

            pthread_mutex_lock(&BOWLER);
            pthread_mutex_unlock(&BOWLER);

            bowler *currBowler = liveBowler;
            string Bowler = liveBowler->name;
            pthread_cond_signal(&BALL);
            pthread_cond_wait(&READY, &GLOB);
            currBowler->balls++;
            //BALL DONE
            pthread_mutex_lock(&BATTER);
            pthread_mutex_unlock(&BATTER);

            pthread_cond_signal(&liveBatsman->ACT);
            pthread_cond_wait(&READYBAT, &GLOB);

            liveBatsman->balls++;


            out = false;
            currCrease = 1;
            int runs = liveBatsman->lastBall;
            if (runs > 7) runs = 1;
            if (runs == 5) runs = 4;
            if (runs == 7) {
                currBowler->wickets += 1;
                liveBatsman->outType = Bowler;
                commentries.push_back(Bowler + " to " + liveBatsman->name + " OUT!!");
                if (battingTeam->balls == 120) {
                    battingTeam->wickets++;
                    liveBatsman->notout = false;
                } else {
                    pthread_mutex_lock(&BATTER);
                    pthread_mutex_unlock(&BATTER);
                    liveBatsman->notout = false;
                    battingTeam->wickets++;
                    pthread_cond_signal(&liveBatsman->ACT);
                    pthread_cond_wait(&READYBAT, &GLOB);
                }
            } else {
                if (runs > 3 || runs == 0 || battingTeam->balls == 120) {
                    currBowler->runs += runs;
                    commentries.push_back(Bowler + " to " + liveBatsman->name + " " +  to_string(runs));
                    if (runs == 4) liveBatsman->fours++;
                    if (runs == 6) liveBatsman->six++;
                    liveBatsman->runs += runs;
                    battingTeam->score += runs;
                } else {
                    RUN = true;
                    for (int i = 0; i < 11; i++) {
                        pthread_mutex_lock(&teams[field ^ 1].fielders[i].act);
                        pthread_mutex_unlock(&teams[field ^ 1].fielders[i].act);
                    }
                    liveBatsman->runs += runs - 1;
                    battingTeam->score += runs - 1;
                    currBowler->runs += runs - 1;
                    batsman *prevBatsman = liveBatsman;

                    if (battingTeam->score < TARGET) {
                        if (runs % 2 == 0) {
                            pthread_mutex_lock(&BATTER);
                            pthread_mutex_unlock(&BATTER);

                            pthread_cond_signal(&liveBatsman->ACT);
                            pthread_cond_wait(&READYBAT, &GLOB);
                        }

                        pthread_mutex_lock(&BATTER);
                        pthread_mutex_unlock(&BATTER);

                        pthread_cond_signal(&liveBatsman->ACT);
                        pthread_cond_broadcast(&FIELD);
                        pthread_cond_wait(&READYBAT, &GLOB);
                    }

                    if (out) {
                        commentries.push_back(Bowler + " to " + prevBatsman->name + " " + to_string(runs - 1) + ", " + liveBatsman->name +  " RUN OUT!!");
                        liveBatsman->outType = "RUN OUT";
                        pthread_mutex_lock(&BATTER);
                        pthread_mutex_unlock(&BATTER);
                        liveBatsman->notout = false;
                        battingTeam->wickets++;
                        pthread_cond_signal(&liveBatsman->ACT);
                        pthread_cond_wait(&READYBAT, &GLOB);
                    } else {
                        currBowler->runs += 1;
                        prevBatsman->runs += 1;
                        battingTeam->score += 1;
                        commentries.push_back(Bowler + " to " + prevBatsman->name + " " + to_string(runs));
                    }

                    for (int i = 0; i < 11; i++) {
                        pthread_mutex_lock(&teams[field ^ 1].fielders[i].act);
                        pthread_mutex_unlock(&teams[field ^ 1].fielders[i].act);
                    }
                    RUN = false;
                }
            }
            // cout << teams[field].balls << " - " << teams[field].score << endl;
            scoreBoard();
            if (BALL_WAIT) usleep(BALL_WAIT);
        }
        //LAST CALLS
        for (int i = 0; i < 11; i++) {
            pthread_mutex_lock(&teams[field ^ 1].fielders[i].act);
            pthread_mutex_unlock(&teams[field ^ 1].fielders[i].act);
        }
        pthread_cond_broadcast(&FIELD);
        for (int i = 0; i < 11; i++) {
            pthread_mutex_lock(&teams[field ^ 1].fielders[i].act);
            pthread_mutex_unlock(&teams[field ^ 1].fielders[i].act);
        }
        if (battingTeam->balls < 120) {
            pthread_mutex_lock(&BOWLER);
            pthread_mutex_unlock(&BOWLER);

            pthread_cond_signal(&BALL);
            pthread_cond_wait(&READY, &GLOB);
        }
        if (battingTeam->balls < 120) {
            pthread_mutex_lock(&BATTER);
            pthread_mutex_unlock(&BATTER);

            pthread_cond_signal(&liveBatsman->ACT);
        }


        for (int i = 0; i < 11; i++) {
            pthread_join(teams[field].batsmans[i].mainThread, NULL);
            pthread_join(teams[field ^ 1].bowlers[i].mainThread, NULL);
            pthread_join(teams[field ^ 1].fielders[i].mainThread, NULL);
        }


        pthread_mutex_unlock(&GLOB);
    }

    void secondInningInitial() {
        TARGET = battingTeam->score;
        inning++;
        field ^= 1;
        battingTeam = &teams[field];
        bowlingTeam = &teams[field ^ 1];
        for (int i = 0; i < 11; i++) {
            pthread_create(&teams[field].batsmans[i].mainThread, NULL, batsmanApi, &teams[field].batsmans[i]);
            pthread_create(&teams[field ^ 1].bowlers[i].mainThread, NULL, bowlersApi, &teams[field ^ 1].bowlers[i]);
            pthread_create(&teams[field ^ 1].fielders[i].mainThread, NULL, fieldersApi, &teams[field ^ 1].fielders[i]);
        }
    }

    void printTeamStats(team &bat, team &bowl, string title) {
        cout << "|================================================================================================================|\n";
        cout << "| " << title << "\n";
        cout << "|----------------------------------------------------------------------------------------------------------------|\n";

        cout << "| "
             << left << setw(55) << "BATTING"
             << "| "
             << left << setw(38) << "BOWLING"
             << "|\n";

        cout << "| "
             << left << setw(18) << "NAME"
             << right << setw(6) << "R"
             << setw(6) << "4s"
             << setw(6) << "6s"
             << setw(6) << "B"
             << setw(13) << "OUT"
             << " | "
             << left << setw(18) << "NAME"
             << right << setw(8) << "OV"
             << setw(8) << "RUNS"
             << setw(8) << "WKTS"
             << setw(10) << "ECON"
             << " |\n";

        cout << "|----------------------------------------------------------------------------------------------------------------|\n";

        for (int i = 0; i < 11; i++) {
            auto &p = bat.batsmans[i];
            auto &b = bowl.bowlers[i];

            int overs = b.balls / 6;
            int balls = b.balls % 6;
            double econ = (b.balls == 0) ? 0.0 : (6.0 * b.runs) / b.balls;

            cout << "| "
                 << left << setw(18) << p.name.substr(0,17)
                 << right << setw(6) << p.runs
                 << setw(6) << p.fours
                 << setw(6) << p.six
                 << setw(6) << p.balls
                 << setw(13) << p.outType.substr(0,12);

            cout << " | "
                 << left << setw(18) << b.name.substr(0,17)
                 << right << setw(8) << (to_string(overs) + "." + to_string(balls))
                 << setw(8) << b.runs
                 << setw(8) << b.wickets
                 << setw(10) << fixed << setprecision(2) << econ
                 << " |\n";
        }

        cout << "|================================================================================================================|\n";
    }

    void scoreBoard() {
        system("clear");

        cout << "|===========================================================================================================|\n";

        cout << "| INNING = " << inning + 1
             << " | " << teams[field].name
             << " vs " << teams[field ^ 1].name << "\n";

        if (inning == 1) {
            cout << "| TARGET: " << TARGET
                 << " | " << teams[field ^ 1].name << ": "
                 << teams[field ^ 1].score << "/"
                 << teams[field ^ 1].wickets
                 << " (" << teams[field ^ 1].balls / 6 << "."
                 << teams[field ^ 1].balls % 6 << " ov)\n";
        }

        cout << "| CURRENT: "
             << teams[field].score << "/"
             << teams[field].wickets
             << " (" << teams[field].balls / 6 << "."
             << teams[field].balls % 6 << " ov)\n";

        cout << "|===========================================================================================================|\n";

        printTeamStats(teams[0], teams[1], "TEAM: " + teams[0].name);

        printTeamStats(teams[1], teams[0], "TEAM: " + teams[1].name);

        while (commentries.size() > 10) commentries.erase(commentries.begin());

        cout << "| COMMENTARY:\n";
        cout << "|-----------------------------------------------------------------------------------------------------------|\n";

        for (string &comm : commentries) cout << "| -> " << comm << "\n";

        cout << "|===========================================================================================================|\n";
    }

    void result() {
        team &t1 = teams[field ^ 1];
        team &t2 = teams[field];

        cout << "|==============================================================|\n";
        cout << "|                        MATCH RESULT                          |\n";
        cout << "|==============================================================|\n";

        // Final Scores
        cout << "| "
             << t1.name << " : "
             << t1.score << "/" << t1.wickets
             << " (" << t1.balls / 6 << "." << t1.balls % 6 << " ov)\n";

        cout << "| "
             << t2.name << " : "
             << t2.score << "/" << t2.wickets
             << " (" << t2.balls / 6 << "." << t2.balls % 6 << " ov)\n";

        cout << "|--------------------------------------------------------------|\n";

        // Decide result
        if (t1.score > t2.score) {
            cout << "| RESULT: " << t1.name
                 << " won by " << (t1.score - t2.score)
                 << " runs\n";
        }
        else if (t2.score > t1.score) {
            int wicketsLeft = 10 - t2.wickets;
            cout << "| RESULT: " << t2.name
                 << " won by " << wicketsLeft
                 << " wickets\n";
        }
        else {
            cout << "| RESULT: MATCH TIED\n";
        }

        cout << "|==============================================================|\n";
    }
};


bool inningRun() {
    return battingTeam->balls < 120 && battingTeam->wickets < 10 && battingTeam->score <= TARGET;
}

void *batsmanApi(void *arg) {
    batsman *me = (batsman *) arg;

    sem_wait(&onfield);
    if (!inningRun()) {
        sem_post(&onfield);
        return nullptr;
    }
    me->outType = "NOT OUT";
    while (inningRun() && me->notout) {
        pthread_mutex_lock(&creases[me->crease]);
        pthread_mutex_lock(&creases[me->crease ^ 1]);
        pthread_mutex_unlock(&creases[me->crease]);
        me->crease ^= 1;
        while (me->notout && inningRun()) {
            pthread_mutex_lock(&me->act);
            pthread_mutex_lock(&BATTER);

            pthread_cond_signal(&READYBAT);
            liveBatsman = me;
            pthread_cond_wait(&me->ACT, &BATTER);

            if (battingTeam->score > TARGET) {
                pthread_mutex_unlock(&BATTER);
                pthread_mutex_unlock(&me->act);
                break;
            }
            if (!me->notout) {
                pthread_mutex_unlock(&BATTER);
                pthread_mutex_unlock(&me->act);
                pthread_mutex_lock(&GLOB);
                pthread_mutex_unlock(&GLOB);

                if (battingTeam->wickets == 10) pthread_cond_signal(&READYBAT);
                break;
            }
            if (RUN) {
                pthread_mutex_lock(&GLOB);
                pthread_mutex_unlock(&GLOB);
                pthread_mutex_unlock(&BATTER);
                pthread_mutex_unlock(&me->act);
                break;
            }

            pthread_mutex_lock(&GLOB);
            pthread_mutex_unlock(&GLOB);

            me->lastBall = rand() % 20;
            if (!inningRun()) pthread_cond_signal(&READYBAT);

            pthread_mutex_unlock(&BATTER);
            pthread_mutex_unlock(&me->act);
        }
        pthread_mutex_unlock(&creases[me->crease]);
        me->crease ^= 1;
    }
    sem_post(&onfield);
    return nullptr;
}
int balls = 6;
void *bowlersApi(void *arg) {
    bowler *me = (bowler *) arg;
    for (int i = 0; i < 4 && inningRun(); i++) {
        if (!inningRun()) break;
        pthread_mutex_lock(&NEXTBOWLER);
        pthread_mutex_lock(&pitch);
        pthread_mutex_unlock(&NEXTBOWLER);

        if (!inningRun()) {
            pthread_mutex_unlock(&pitch);
            break;
        }
        liveBowler = me;
        balls = 6;
        for (int i = 0; i < balls && inningRun(); i++) {
            pthread_mutex_lock(&BOWLER);

            pthread_cond_signal(&READY);
            pthread_cond_wait(&BALL, &BOWLER);
            pthread_mutex_lock(&GLOB);
            pthread_mutex_unlock(&GLOB);

            if (!inningRun()) {
                pthread_mutex_unlock(&pitch);
                pthread_mutex_unlock(&BOWLER);
                pthread_cond_signal(&READY);
                return nullptr;
            }

            battingTeam->balls++;
            pthread_mutex_unlock(&BOWLER);
        }
        pthread_mutex_lock(&GLOB);
        pthread_mutex_unlock(&GLOB);
        if (!inningRun()) pthread_cond_signal(&READY);
        pthread_mutex_unlock(&pitch);
    }
    return nullptr;
}

void *fieldersApi(void *arg) {
    fielder *me = (fielder *) arg;
    pthread_mutex_lock(&me->act);
    while (inningRun()) {
        pthread_cond_wait(&FIELD, &me->act);
        usleep(RUNOUT_WAIT);
        if (rand() % 10 == 0 && (liveBowler->name != me->name && pthread_mutex_trylock(&creases[currCrease]) == 0)) {
            out = true;
            pthread_mutex_unlock(&creases[currCrease]);
        }
    }
    pthread_mutex_unlock(&me->act);
    return nullptr;
}

int main() {
    // cout << "WAIT AFTER BALL - " << endl;
    // cin >> BALL_WAIT;
    // cout << "FIELDER WAIT BALL - " << endl;
    // cin >> RUNOUT_WAIT;
    // BALL_WAIT = 0;
    // for (int i = 0; i < 500; i++) {
    //     system("clear");
    //     cout << "Starting - " << i + 1 << " Match" << endl;
    //
    //     srand(time(NULL));
    //     sem_init(&onfield, 0, 2);
    //     game engine;
    //     engine.firstInningInitial();
    //     engine.play();
    //     cout << "DONE - " << i + 1 << " Matches" << endl;
    //     usleep(1000000);
    // }
    for (int i = 0; i < 1; i++) {
        srand(time(NULL));
        sem_init(&onfield, 0, 2);
        game engine;
        engine.firstInningInitial();
        engine.play();
        engine.secondInningInitial();
        engine.play();
        engine.result();
        usleep(10000);
    }
}