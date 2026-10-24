#include "game.h"
#include <string.h>

uint32_t Rand(uint64_t* s, uint32_t n) {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return (uint32_t)((*s >> 11) % n);
}

static int InField(const Board* b, int x, int y) {
    return x >= 0 && y >= 0 && x < b->size && y < b->size;
}

static int CanPlace(const Board* b, int x, int y, int dx, int dy, int len, int touch) {
    for (int i = 0; i < len; i++) {
        int cx = x + dx * i, cy = y + dy * i;
        if (!InField(b, cx, cy)) return 0;
        for (int oy = -1; oy <= 1; oy++)
            for (int ox = -1; ox <= 1; ox++) {
                if (!InField(b, cx + ox, cy + oy) || !b->cell[cy + oy][cx + ox]) continue;
                if (!touch || (ox == 0 && oy == 0)) return 0;
            }
    }
    return 1;
}

int BoardPlace(Board* b, int size, int touch, uint64_t* rng) {
    static const int fleet[] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
    for (int restart = 0; restart < 100; restart++) {
        memset(b, 0, sizeof(*b));
        b->size = size;
        int ok = 1;
        for (int k = 0; k < 10 && ok; k++) {
            ok = 0;
            for (int t = 0; t < 500 && !ok; t++) {
                int dx = Rand(rng, 2), dy = 1 - dx;
                int x = Rand(rng, size), y = Rand(rng, size);
                if (!CanPlace(b, x, y, dx, dy, fleet[k], touch)) continue;
                b->ships[k] = (Ship){fleet[k], 0, x, y, dx, dy};
                for (int i = 0; i < fleet[k]; i++) b->cell[y + dy * i][x + dx * i] = k + 1;
                ok = 1;
            }
        }
        if (ok) {
            b->n_ships = b->alive = 10;
            return 0;
        }
    }
    return -1;
}

Result BoardShoot(Board* b, int x, int y) {
    Result r = {MISS, 0, 0, 0, 0, 0};
    if (b->shot[y][x]) {
        r.kind = REPEAT;
        return r;
    }
    b->shot[y][x] = 1;
    if (!b->cell[y][x]) return r;
    Ship* s = &b->ships[b->cell[y][x] - 1];
    s->hits++;
    r.kind = HIT;
    if (s->hits == s->len) {
        b->alive--;
        r = (Result){SUNK, s->len, s->x0, s->y0, s->dx, s->dy};
    }
    return r;
}

const char* BoardCheck(const Board* b) {
    int hit_cells = 0, hits = 0, alive = 0;
    for (int y = 0; y < b->size; y++)
        for (int x = 0; x < b->size; x++)
            if (b->shot[y][x] && b->cell[y][x]) hit_cells++;
    for (int k = 0; k < b->n_ships; k++) {
        hits += b->ships[k].hits;
        if (b->ships[k].hits < b->ships[k].len) alive++;
    }
    if (hit_cells != hits) return "число попаданий не совпало с полем";
    if (alive != b->alive) return "неправильно посчитано число живых кораблей";
    return NULL;
}
