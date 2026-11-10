#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    sem_t local[3];
    int x, y;
    Result result;
} Seat;
typedef struct {
    Config config;
    pid_t judge, players[2];
    volatile sig_atomic_t over;
    Seat seat[2];
} Shared;
static Shared* shared;
static sem_t* syncs[2][3];
static char base[80];
static int log_fd = -1;

int ModelStopped(void) { return g_stop || (shared && shared->over); }

static void Say(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt); vdprintf(1, fmt, ap); va_end(ap);
    if (log_fd >= 0) {
        va_start(ap, fmt); vdprintf(log_fd, fmt, ap); va_end(ap);
    }
}

/* Ожидание с таймаутом позволяет заметить сигнал и исчезновение судьи. */
static int Wait(sem_t* sem) {
    while (!g_stop && !shared->over) {
        struct timespec t;
        clock_gettime(CLOCK_REALTIME, &t); t.tv_sec++;
        if (!sem_timedwait(sem, &t)) return 0;
        if (errno != EINTR && errno != ETIMEDOUT) break;
        if (kill(shared->judge, 0) && errno == ESRCH) break;
        if (getpid() == shared->judge) for (int i = 0; i < 2; i++) {
            pid_t pid = shared->players[i];
            if (pid > 0 && ((kill(pid, 0) && errno == ESRCH) ||
                (ipc_mode != 2 && waitpid(pid, NULL, WNOHANG) == pid))) {
                g_stop = SIGTERM; return -1;
            }
        }
    }
    return -1;
}

static int Names(const char* session) {
    if (!*session || strlen(session) > 40 || strspn(session,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(session)) return -1;
    snprintf(base, sizeof(base), "/bs3_%s", session);
    return 0;
}

static void SemName(char* name, int i, int k) {
    snprintf(name, 100, "%s_%d_%d", base, i, k);
}

static void PlayerProcess(int id) {
    shared->players[id] = getpid();
    Player p;
    PlayerInit(&p, &shared->config, id, shared->config.seed);
    Seat* s = &shared->seat[id];
    sem_post(syncs[id][1]); /* Игрок готов; далее этот семафор передаёт выстрел. */
    while (!Wait(syncs[id][0]) && !shared->over) {
        if (PlayerChoose(&p, &s->x, &s->y)) s->x = -1;
        sem_post(syncs[id][1]);
        if (Wait(syncs[id][2]) || shared->over) break;
        PlayerOnResult(&p, s->x, s->y, &s->result);
    }
    if (g_stop) kill(shared->judge, SIGTERM);
}

int ConnectPlayer(const char* session, int id) {
    if (Names(session)) return 1;
    int fd = shm_open(base, O_RDWR, 0);
    if (fd < 0) { perror("сначала запустите судью"); return 2; }
    struct flock lock = {.l_type=F_WRLCK, .l_whence=SEEK_SET, .l_start=id, .l_len=1};
    if (fcntl(fd, F_SETLK, &lock)) { close(fd); perror("игрок уже подключён"); return 2; }
    off_t size = lseek(fd, 0, SEEK_END);
    if (size != (off_t)sizeof(Shared)) { close(fd); return 2; }
    shared = mmap(NULL, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) { close(fd); return 2; }
    int opened = 0;
    for (int i = 0; i < 2; i++) for (int k = 0; k < 3; k++) {
        char name[100]; SemName(name, i, k);
        syncs[i][k] = sem_open(name, 0);
        if (syncs[i][k] == SEM_FAILED) goto done;
        opened++;
    }
    PlayerProcess(id);
done:
    for (int n = 0; n < opened; n++) sem_close(syncs[n / 3][n % 3]);
    munmap(shared, sizeof(Shared)); close(fd);
    return opened == 6 ? (g_stop ? 128 + g_stop : 0) : 2;
}

static void PrintBoards(const Board* boards) {
    for (int i = 0; i < 2; i++) {
        const Board* b = &boards[i];
        Say("Поле игрока %d (только проверенные клетки):\n   ", i);
        for (int x = 0; x < b->width; x++) Say("%c ", 'A' + x);
        for (int y = 0; y < b->height; y++) {
            Say("\n%2d ", y + 1);
            for (int x = 0; x < b->width; x++)
                Say("%c ", !b->shot[y][x] ? '.' : b->cell[y][x] ? 'X' : 'o');
        }
        Say("\n");
    }
}

static int Fight(const Config* c) {
    Board boards[2];
    for (int i = 0; i < 2; i++) {
        uint64_t rng = ((uint64_t)c->seed * 31 + 7 + i * 1000) * 0x9E3779B97F4A7C15ull;
        if (BoardPlace(&boards[i], c, &rng)) { Say("Не удалось расставить флот\n"); return 3; }
    }
    char session[40];
    snprintf(session, sizeof(session), "%ld", (long)getpid());
    if (Names(ipc_mode == 2 ? ipc_session : session)) return 1;
    int fd = shm_open(base, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { perror("shm_open (имя занято?)"); return 2; }
    if (ftruncate(fd, sizeof(Shared))) { close(fd); shm_unlink(base); return 2; }
    shared = mmap(NULL, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (shared == MAP_FAILED) { shm_unlink(base); return 2; }
    memset(shared, 0, sizeof(Shared));
    shared->config = *c; shared->judge = getpid();
    int initialized = 0, code = 0;
    pid_t children[2] = {0, 0};
    for (int i = 0; i < 2; i++) for (int k = 0; k < 3; k++) {
        if (ipc_mode) {
            char name[100]; SemName(name, i, k);
            syncs[i][k] = sem_open(name, O_CREAT | O_EXCL, 0600, 0);
            if (syncs[i][k] == SEM_FAILED) { code = 2; goto cleanup; }
        } else {
            syncs[i][k] = &shared->seat[i].local[k];
            if (sem_init(syncs[i][k], 1, 0)) { code = 2; goto cleanup; }
        }
        initialized++;
    }
    Say("Поля подготовлены: %dx%d, кораблей %d. Судья PID=%ld\n",
        c->width, c->height, c->count, (long)getpid());
    if (ipc_mode == 2) Say("Ожидание: battleship player %s 0 и battleship player %s 1\n", ipc_session, ipc_session);
    else for (int i = 0; i < 2; i++) {
        children[i] = fork();
        if (children[i] < 0) { code = 2; goto cleanup; }
        if (!children[i]) { PlayerProcess(i); _exit(0); }
    }
    for (int i = 0; i < 2; i++) if (Wait(syncs[i][1])) goto cleanup;
    int cur = c->first, moves = 0, winner = -1;
    int shots[2] = {0}, hits[2] = {0}, sunk[2] = {0};
    PrintBoards(boards);
    while (!g_stop && (!c->max_moves || moves < c->max_moves)) {
        Say("Ход игрока %d\n", cur);
        sem_post(syncs[cur][0]);
        if (Wait(syncs[cur][1])) break;
        Seat* s = &shared->seat[cur];
        if (s->x < 0) { Say("Игрок вышел\n"); break; }
        if (s->x >= c->width || s->y < 0 || s->y >= c->height) { code = 4; break; }
        Result r = BoardShoot(&boards[1 - cur], s->x, s->y);
        s->result = r; sem_post(syncs[cur][2]);
        if (r.kind == REPEAT && !c->repeat) { Say("Повтор запрещён, выберите другую клетку\n"); continue; }
        moves++; shots[cur]++;
        if (r.kind == HIT || r.kind == SUNK) hits[cur]++;
        if (r.kind == SUNK) sunk[cur]++;
        Say("Ход %3d. Игрок %d -> %c%d: %s\n", moves, cur, 'A' + s->x, s->y + 1,
            r.kind == MISS ? "мимо" : r.kind == HIT ? "ПОПАЛ" : r.kind == SUNK ? "УБИЛ" : "повтор, потеря хода");
        const char* error = BoardCheck(&boards[1 - cur]);
        if (error) { Say("Нарушение: %s\n", error); code = 4; break; }
        PrintBoards(boards);
        if (!boards[1 - cur].alive) { winner = cur; break; }
        if (!c->again || r.kind == MISS || r.kind == REPEAT) {
            cur = 1 - cur; Say("Смена игрока -> %d\n", cur);
        }
        struct timespec delay = {c->delay / 1000, c->delay % 1000 * 1000000L};
        if (c->delay) nanosleep(&delay, NULL);
    }
    if (g_stop) Say("Бой прерван сигналом %d\n", (int)g_stop);
    else if (winner >= 0) Say("Победил игрок %d за %d ходов\n", winner, moves);
    else if (c->max_moves && moves >= c->max_moves) Say("Лимит %d ходов: ничья\n", moves);
    for (int i = 0; i < 2; i++) Say("Игрок %d: выстрелов %d, попаданий %d, уничтожено %d\n", i, shots[i], hits[i], sunk[i]);
cleanup:
    shared->over = 1;
    for (int n = 0; n < initialized; n++) sem_post(syncs[n / 3][n % 3]);
    for (int i = 0; i < 2; i++) if (children[i] > 0) {
        int status;
        while (waitpid(children[i], &status, 0) < 0 && errno == EINTR) {}
    }
    for (int n = 0; n < initialized; n++) {
        int i = n / 3, k = n % 3;
        if (ipc_mode) {
            char name[100]; SemName(name, i, k);
            sem_close(syncs[i][k]); sem_unlink(name);
        } else sem_destroy(syncs[i][k]);
    }
    munmap(shared, sizeof(Shared)); shm_unlink(base);
    return g_stop ? 128 + g_stop : code;
}

int Play(const Config* c) {
    if (strcmp(c->log, "none")) {
        log_fd = open(c->log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd < 0) { perror("журнал"); return 2; }
    }
    int code = 0;
    for (int i = 0; i < c->games && !code && !g_stop; i++) {
        Config game = *c; game.seed += i;
        Say("Бой %d, seed=%u, режим=%d\n", i + 1, game.seed, ipc_mode);
        code = Fight(&game);
    }
    if (log_fd >= 0) close(log_fd);
    return code;
}
