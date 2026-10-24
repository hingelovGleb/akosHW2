#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int s_log = -1;
static int s_quiet = 0;

static pthread_mutex_t s_out = PTHREAD_MUTEX_INITIALIZER;

static void Say(const char* fmt, ...) {
    if (s_quiet) return;
    pthread_mutex_lock(&s_out);
    va_list ap;
    va_start(ap, fmt);
    vdprintf(1, fmt, ap);
    va_end(ap);
    if (s_log >= 0) {
        va_start(ap, fmt);
        vdprintf(s_log, fmt, ap);
        va_end(ap);
    }
    pthread_mutex_unlock(&s_out);
}

static void Sleep(int ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

static void PrintBoards(const Board* b, const char names[2][32]) {
    int n = b->size;
    Say("\n    %-*s    %s\n   ", n * 2 + 2, names[0], names[1]);
    for (int s = 0; s < 2; s++) {
        for (int x = 0; x < n; x++) Say("%c ", 'A' + x);
        Say("     ");
    }
    for (int y = 0; y < n; y++) {
        Say("\n%2d ", y + 1);
        for (int s = 0; s < 2; s++) {
            for (int x = 0; x < n; x++) {
                const Board* bd = &b[s];
                char c = bd->cell[y][x] ? (bd->shot[y][x] ? 'X' : '#') : (bd->shot[y][x] ? 'o' : '.');
                Say("%c ", c);
            }
            Say("   ");
        }
    }
    Say("\n");
}

typedef struct { int winner, moves, quit; } Outcome;

typedef struct {
    Player pl;
    const char* name;
    sem_t turn;
    sem_t shot;
    sem_t reply;
    int x, y;
    Result r;
    _Atomic int over;
} Seat;

static void* PlayerThread(void* arg) {
    Seat* s = arg;
    for (;;) {
        sem_wait(&s->turn);
        if (s->over) break;
        if (PlayerChoose(&s->pl, &s->x, &s->y) == 0) Say("   %s целится в %c%d\n", s->name, 'A' + s->x, s->y + 1);
        else s->x = -1;
        sem_post(&s->shot);
        sem_wait(&s->reply);
        if (s->over) break;
        PlayerOnResult(&s->pl, s->x, s->y, &s->r);
    }
    return NULL;
}

static int Fight(const Config* c, unsigned seed, int first, Outcome* out) {
    char names[2][32];
    snprintf(names[0], 32, "Нельсон(%s)", c->strat[0]);
    snprintf(names[1], 32, "Ушаков(%s)", c->strat[1]);
    Board bd[2];
    Seat seat[2];
    for (int i = 0; i < 2; i++) {
        uint64_t rng = ((uint64_t)seed * 31 + 7 + i * 1000) * 0x9E3779B97F4A7C15ull;
        if (BoardPlace(&bd[i], c->size, c->touch, &rng) != 0) {
            Say("Не получилось расставить флот для %s\n", names[i]);
            return 3;
        }
        PlayerInit(&seat[i].pl, c, i, seed);
        seat[i].name = names[i];
        sem_init(&seat[i].turn, 0, 0);
        sem_init(&seat[i].shot, 0, 0);
        sem_init(&seat[i].reply, 0, 0);
        seat[i].over = 0;
    }

    pthread_t th[2];
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    for (int i = 0; i < 2; i++) pthread_create(&th[i], NULL, PlayerThread, &seat[i]);
    pthread_sigmask(SIG_SETMASK, &old, NULL);

    int shots[2] = {0, 0}, hits[2] = {0, 0}, sunk[2] = {0, 0};
    int cur = first, moves = 0;
    int code = 0;
    *out = (Outcome){-1, 0, 0};
    while (!g_stop) {
        if (c->max_moves && moves >= c->max_moves) break;
        Seat* me = &seat[cur];
        sem_post(&me->turn);
        while (sem_wait(&me->shot) != 0 && !g_stop) {}
        if (g_stop) break;
        int x = me->x, y = me->y;
        if (x < 0) {
            out->quit = 1;
            break;
        }
        Result r = BoardShoot(&bd[1 - cur], x, y);
        if (r.kind == REPEAT) {
            Say("Ошибка: %s выстрелил второй раз в %c%d\n", names[cur], 'A' + x, y + 1);
            code = 4;
            break;
        }
        const char* err = BoardCheck(&bd[1 - cur]);
        if (err) {
            Say("Судья нашёл ошибку: %s\n", err);
            code = 4;
            break;
        }
        moves++;
        shots[cur]++;
        if (r.kind != MISS) hits[cur]++;
        if (r.kind == SUNK) sunk[cur]++;
        me->r = r;
        sem_post(&me->reply);
        Say("Ход %3d. %-18s -> %c%-2d %s\n", moves, names[cur], 'A' + x, y + 1,
            r.kind == MISS ? "мимо" : r.kind == HIT ? "ПОПАЛ" : "УБИЛ");
        if (bd[1 - cur].alive == 0) {
            out->winner = cur;
            break;
        }
        if (!(r.kind != MISS && c->again)) cur = 1 - cur;
        if (c->delay) Sleep(c->delay);
    }
    out->moves = moves;

    for (int i = 0; i < 2; i++) {
        seat[i].over = 1;
        sem_post(&seat[i].turn);
        sem_post(&seat[i].reply);
    }
    for (int i = 0; i < 2; i++) {
        pthread_join(th[i], NULL);
        sem_destroy(&seat[i].turn);
        sem_destroy(&seat[i].shot);
        sem_destroy(&seat[i].reply);
    }
    if (code) return code;

    PrintBoards(bd, names);
    if (g_stop) Say("\nБой прерван сигналом %d после %d ходов\n", (int)g_stop, moves);
    else if (out->quit) Say("\nИгрок вышел из боя после %d ходов\n", moves);
    else if (out->winner >= 0) Say("\nПобедил %s за %d ходов\n", names[out->winner], moves);
    else Say("\nДостигнут лимит ходов (%d), ничья\n", moves);
    for (int i = 0; i < 2; i++)
        Say("%-18s выстрелов %3d, попаданий %3d (%.0f%%), убито кораблей %d\n", names[i], shots[i], hits[i],
            shots[i] ? 100.0 * hits[i] / shots[i] : 0.0, sunk[i]);
    return g_stop ? 128 + g_stop : 0;
}

int Play(const Config* c) {
    if (strcmp(c->log, "none") != 0) {
        s_log = open(c->log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (s_log < 0) dprintf(2, "не удалось открыть журнал %s\n", c->log);
    }
    Say("Морской бой %dx%d, seed=%u, касание=%s, после попадания %s, боёв: %d\n", c->size, c->size, c->seed,
        c->touch ? "можно" : "нельзя", c->again ? "ходит ещё раз" : "ход переходит", c->games);

    Outcome o;
    int code = 0;
    if (c->games == 1) {
        code = Fight(c, c->seed, 0, &o);
    } else {
        int wins[2] = {0, 0}, draws = 0, played = 0;
        long total = 0;
        int min = 1 << 30, max = 0;
        s_quiet = 1;
        for (int i = 0; i < c->games && !g_stop && code == 0; i++) {
            code = Fight(c, c->seed + i, i % 2, &o);
            if (code != 0) break;
            played++;
            if (o.winner >= 0) wins[o.winner]++;
            else draws++;
            total += o.moves;
            if (o.moves < min) min = o.moves;
            if (o.moves > max) max = o.moves;
        }
        s_quiet = 0;
        if (g_stop) code = 128 + g_stop;
        Say("\nСыграно боёв: %d\n", played);
        Say("Победы: Нельсон(%s) %d, Ушаков(%s) %d, ничьих %d\n", c->strat[0], wins[0], c->strat[1], wins[1], draws);
        if (played) Say("Ходов за бой: в среднем %.1f, минимум %d, максимум %d\n", (double)total / played, min, max);
    }
    if (s_log >= 0) close(s_log);
    return code;
}
