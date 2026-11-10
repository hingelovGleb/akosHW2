#ifndef GAME_H
#define GAME_H

#include <semaphore.h>
#include <signal.h>
#include <stdint.h>

#define MAXN 16
#define MAXSHIPS 16

typedef enum { MISS, HIT, SUNK, REPEAT } Kind;

typedef struct {
    Kind kind;
    int len, x0, y0, dx, dy;
} Result;

typedef struct {
    int width, height;
    int touch;
    int again;
    int repeat, first, ordered;
    int fleet[MAXSHIPS], count;
    int max_moves;
    int delay;
    int games;
    unsigned seed;
    char strat[2][8];
    char log[64];
} Config;

typedef struct { int len, hits, x0, y0, dx, dy; } Ship;

typedef struct {
    int width, height;
    int cell[MAXN][MAXN];
    int shot[MAXN][MAXN];
    Ship ships[MAXSHIPS];
    int n_ships, alive;
} Board;

typedef struct {
    int width, height, touch, repeat;
    char strat[8];
    int known[MAXN][MAXN];
    uint64_t rng;
} Player;

extern volatile sig_atomic_t g_stop;

uint32_t Rand(uint64_t* s, uint32_t n);

int BoardPlace(Board* b, const Config* c, uint64_t* rng);
Result BoardShoot(Board* b, int x, int y);
const char* BoardCheck(const Board* b);

void PlayerInit(Player* p, const Config* c, int id, unsigned seed);
int PlayerChoose(Player* p, int* x, int* y);
void PlayerOnResult(Player* p, int x, int y, const Result* r);

int ModelStopped(void);
int Play(const Config* c);
int ConnectPlayer(const char* session, int id);
extern int ipc_mode;
extern const char* ipc_session;

#endif
