#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <ctype.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void PlayerInit(Player* p, const Config* c, int id, unsigned seed) {
    memset(p, 0, sizeof(*p));
    p->width = c->width; p->height = c->height;
    p->repeat = c->repeat;
    p->touch = c->touch;
    strcpy(p->strat, c->strat[id]);
    p->rng = ((uint64_t)seed + 1 + id) * 0x9E3779B97F4A7C15ull;
}

static int Free(const Player* p, int x, int y) {
    return x >= 0 && y >= 0 && x < p->width && y < p->height && !p->known[y][x];
}

static int Finish(const Player* p, int* x, int* y) {
    static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int pass = 0; pass < 2; pass++)
        for (int cy = 0; cy < p->height; cy++)
            for (int cx = 0; cx < p->width; cx++) {
                if (p->known[cy][cx] != 2) continue;
                for (int k = 0; k < 4; k++) {
                    int nx = cx + d[k][0], ny = cy + d[k][1];
                    int bx = cx - d[k][0], by = cy - d[k][1];
                    int line = bx >= 0 && by >= 0 && bx < p->width && by < p->height && p->known[by][bx] == 2;
                    if (Free(p, nx, ny) && (pass == 1 || line)) {
                        *x = nx;
                        *y = ny;
                        return 1;
                    }
                }
            }
    return 0;
}

static int ReadLine(char* buf, int n) {
    int len = 0;
    char c;
    while (!ModelStopped()) {
        struct pollfd pf = {0, POLLIN, 0};
        if (poll(&pf, 1, 100) <= 0) continue;
        if (read(0, &c, 1) <= 0) return -1;
        if (c == '\n') break;
        if (len < n - 1) buf[len++] = c;
    }
    buf[len] = 0;
    return ModelStopped() ? -1 : 0;
}

static int HumanChoose(const Player* p, int* x, int* y) {
    static const char sym[] = ".oXX";
    dprintf(1, "\nПоле противника:\n   ");
    for (int cx = 0; cx < p->width; cx++) dprintf(1, "%c ", 'A' + cx);
    for (int cy = 0; cy < p->height; cy++) {
        dprintf(1, "\n%2d ", cy + 1);
        for (int cx = 0; cx < p->width; cx++) dprintf(1, "%c ", sym[p->known[cy][cx]]);
    }
    dprintf(1, "\n");
    for (;;) {
        char line[64];
        dprintf(1, "Ваш выстрел (например B5, q - выход): ");
        if (ReadLine(line, sizeof(line)) != 0) return -1;
        char* s = line;
        while (*s == ' ') s++;
        if (tolower((unsigned char)*s) == 'q') return -1;
        int cx = tolower((unsigned char)s[0]) - 'a', cy = atoi(s + 1) - 1;
        if (cx < 0 || cx >= p->width || cy < 0 || cy >= p->height) dprintf(1, "Не понял клетку, нужна буква и число\n");
        else if (!p->repeat && p->known[cy][cx]) dprintf(1, "Сюда уже стреляли\n");
        else {
            *x = cx;
            *y = cy;
            return 0;
        }
    }
}

int PlayerChoose(Player* p, int* x, int* y) {
    if (strcmp(p->strat, "human") == 0) return HumanChoose(p, x, y);
    int hunt = strcmp(p->strat, "hunt") == 0;
    if (hunt && Finish(p, x, y)) return 0;
    int cand[MAXN * MAXN][2];
    for (int pass = 0; pass < 2; pass++) {
        int n = 0;
        for (int cy = 0; cy < p->height; cy++)
            for (int cx = 0; cx < p->width; cx++)
                if (Free(p, cx, cy) && !(hunt && pass == 0 && (cx + cy) % 2)) {
                    cand[n][0] = cx;
                    cand[n][1] = cy;
                    n++;
                }
        if (n) {
            int k = Rand(&p->rng, n);
            *x = cand[k][0];
            *y = cand[k][1];
            return 0;
        }
    }
    return -1;
}

void PlayerOnResult(Player* p, int x, int y, const Result* r) {
    if (r->kind == REPEAT) return;
    p->known[y][x] = r->kind == MISS ? 1 : 2;
    if (r->kind != SUNK) return;
    for (int i = 0; i < r->len; i++) {
        int cx = r->x0 + r->dx * i, cy = r->y0 + r->dy * i;
        p->known[cy][cx] = 3;
    }
    if (p->touch) return;
    for (int i = 0; i < r->len; i++)
        for (int oy = -1; oy <= 1; oy++)
            for (int ox = -1; ox <= 1; ox++) {
                int cx = r->x0 + r->dx * i + ox, cy = r->y0 + r->dy * i + oy;
                if (Free(p, cx, cy)) p->known[cy][cx] = 1;
            }
}
