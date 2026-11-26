#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int listener, players[2] = {-1, -1};
static int observers[32];
static int log_fd = -1;
static const Config* config;
static Player saved[2];
static int waiting = -1;

static void Say(const char* fmt, ...);

static void Disconnect(int id) {
    if (players[id] < 0) return;
    close(players[id]); players[id] = -1;
    Say("Игрок %d отключился\n", id);
}

static void Say(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    dprintf(1, "%s", buf);
    if (log_fd >= 0) dprintf(log_fd, "%s", buf);
    for (int i = 0; i < 32; i++) if (observers[i] >= 0) {
        // Медленный наблюдатель не должен задерживать игру.
        if (send(observers[i], buf, strlen(buf), MSG_DONTWAIT | MSG_NOSIGNAL) != (ssize_t)strlen(buf)) {
            close(observers[i]); observers[i] = -1;
        }
    }
}

static void Accept(void) {
    int fd = accept(listener, NULL, NULL);
    if (fd < 0) return;
    struct pollfd p = {fd, POLLIN, 0};
    char role;
    if (poll(&p, 1, 200) <= 0 || read(fd, &role, 1) != 1) { close(fd); return; }
    if (role == 'o') {
        for (int i = 0; i < 32; i++) if (observers[i] < 0) {
            observers[i] = fd;
            Say("Наблюдатель подключился\n"); return;
        }
    } else if (role == '0' || role == '1') {
        int id = role - '0';
        if (players[id] < 0) {
            int data[5] = {config->width, config->height, config->touch, config->repeat, (int)config->seed};
            for (int i = 0; i < 5; i++) data[i] = htonl(data[i]);
            // Состояние стратегии хранится у судьи между подключениями.
            unsigned char known[MAXN][MAXN];
            for (int y = 0; y < MAXN; y++) for (int x = 0; x < MAXN; x++)
                known[y][x] = saved[id].known[y][x];
            uint32_t rng[2] = {htonl(saved[id].rng >> 32), htonl(saved[id].rng)};
            if (!SendAll(fd, data, sizeof(data)) && !SendAll(fd, config->strat[id], 8) &&
                !SendAll(fd, known, sizeof(known)) && !SendAll(fd, rng, sizeof(rng))) {
                players[id] = fd; Say("Игрок %d подключился\n", id); return;
            }
        }
    }
    close(fd);
}

// poll обслуживает наблюдателей даже во время ожидания выстрела.
static int Service(int timeout) {
    struct pollfd p[35];
    p[0] = (struct pollfd){listener, POLLIN, 0};
    for (int i = 0; i < 32; i++) p[i+1] = (struct pollfd){observers[i], POLLIN, 0};
    poll(p, 33, timeout);
    for (int i = 0; i < 32; i++) if (p[i+1].revents) {
        close(observers[i]); observers[i] = -1;
    }
    for (int i = 0; i < 2; i++) if (i != waiting && players[i] >= 0) {
        char ch;
        ssize_t r = recv(players[i], &ch, 1, MSG_PEEK | MSG_DONTWAIT);
        if (!r || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) Disconnect(i);
    }
    if (p[0].revents & POLLIN) Accept();
    return 0;
}

static int WaitShot(int id, int* xy) {
    size_t n = 0;
    while (!g_stop && n < 2 * sizeof(int)) {
        Service(0);
        struct pollfd p = {players[id], POLLIN, 0};
        if (poll(&p, 1, 50) <= 0) continue;
        ssize_t r = recv(players[id], (char*)xy + n, 2 * sizeof(int) - n, 0);
        if (r <= 0) return -1;
        n += r;
    }
    return g_stop ? -1 : 0;
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
    Say("Поля подготовлены: %dx%d, кораблей %d\n", c->width, c->height, c->count);
    for (int i = 0; i < 2; i++) PlayerInit(&saved[i], c, i, c->seed);
    Say("Ожидание игроков 0 и 1\n");
    while (!g_stop && (players[0] < 0 || players[1] < 0)) Service(100);
    if (g_stop) return 128 + g_stop;
    int code = 0;
    int cur = c->first, moves = 0, winner = -1;
    int shots[2] = {0}, hits[2] = {0}, sunk[2] = {0};
    PrintBoards(boards);
    while (!g_stop && (!c->max_moves || moves < c->max_moves)) {
        Say("Ход игрока %d\n", cur);
        int turn = htonl(1), xy[2];
        // Пока игрок отсутствует, поля и номер хода не меняются.
        while (!g_stop) {
            while (!g_stop && (players[0] < 0 || players[1] < 0)) Service(100);
            if (g_stop) break;
            waiting = cur;
            int failed = SendAll(players[cur], &turn, sizeof(turn)) || WaitShot(cur, xy);
            waiting = -1;
            if (!failed) break;
            Disconnect(cur);
        }
        if (g_stop) break;
        struct { int x, y; } shot = {(int)ntohl(xy[0]), (int)ntohl(xy[1])};

        if (shot.x < 0) { Disconnect(cur); continue; }
        if (shot.x >= c->width || shot.y < 0 || shot.y >= c->height) { code = 4; break; }
        Result r = BoardShoot(&boards[1 - cur], shot.x, shot.y);
        int reply[6] = {r.kind, r.len, r.x0, r.y0, r.dx, r.dy};
        for (int i = 0; i < 6; i++) reply[i] = htonl(reply[i]);
        // Продвигаем генератор только после получения полного выстрела.
        if (strcmp(saved[cur].strat, "human")) {
            int x, y;
            PlayerChoose(&saved[cur], &x, &y);
        }
        PlayerOnResult(&saved[cur], shot.x, shot.y, &r);
        if (SendAll(players[cur], reply, sizeof(reply))) Disconnect(cur);
        if (r.kind == REPEAT && !c->repeat) { Say("Повтор запрещён, выберите другую клетку\n"); continue; }
        moves++; shots[cur]++;
        if (r.kind == HIT || r.kind == SUNK) hits[cur]++;
        if (r.kind == SUNK) sunk[cur]++;
        Say("Ход %3d. Игрок %d -> %c%d: %s\n", moves, cur, 'A' + shot.x, shot.y + 1,
            r.kind == MISS ? "мимо" : r.kind == HIT ? "ПОПАЛ" : r.kind == SUNK ? "УБИЛ" : "повтор, потеря хода");
        const char* error = BoardCheck(&boards[1 - cur]);
        if (error) { Say("Нарушение: %s\n", error); code = 4; break; }
        PrintBoards(boards);
        if (!boards[1 - cur].alive) { winner = cur; break; }
        if (!c->again || r.kind == MISS || r.kind == REPEAT) {
            cur = 1 - cur; Say("Смена игрока -> %d\n", cur);
        }
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        int left = c->delay;
        while (left > 0 && !g_stop) {
            Service(left);
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
            left = elapsed >= c->delay ? 0 : c->delay - (int)elapsed;
        }
        Service(0);
    }
    if (g_stop) Say("Бой прерван сигналом %d\n", (int)g_stop);
    else if (winner >= 0) Say("Победил игрок %d за %d ходов\n", winner, moves);
    else if (c->max_moves && moves >= c->max_moves) Say("Лимит %d ходов: ничья\n", moves);
    for (int i = 0; i < 2; i++) Say("Игрок %d: выстрелов %d, попаданий %d, уничтожено %d\n", i, shots[i], hits[i], sunk[i]);
    return g_stop ? 128 + g_stop : code;
}

int Play(const Config* c) {
    if (strcmp(c->log, "none")) {
        log_fd = open(c->log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd < 0) { perror("журнал"); return 2; }
    }
    for (int i = 0; i < 32; i++) observers[i] = -1;
    listener = OpenConnection(1);
    if (listener < 0) { perror("server"); return 2; }
    config = c;
    int code = 0;
    for (int i = 0; i < c->games && !code && !g_stop; i++) {
        Config game = *c; game.seed += i;
        Say("Бой %d, seed=%u\n", i + 1, game.seed);
        code = Fight(&game);
    }
    for (int i = 0; i < 2; i++) if (players[i] >= 0) close(players[i]);
    for (int i = 0; i < 32; i++) if (observers[i] >= 0) close(observers[i]);
    close(listener);
    if (log_fd >= 0) close(log_fd);
    return code;
}
