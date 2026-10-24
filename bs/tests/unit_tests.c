#include "../src/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile sig_atomic_t g_stop = 0;

static int checks = 0, failed = 0;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { failed++; printf("FAIL: %s (строка %d)\n", msg, __LINE__); } } while (0)

static int HasForeignNeighbor(const Board* b, int x, int y) {
    for (int oy = -1; oy <= 1; oy++)
        for (int ox = -1; ox <= 1; ox++) {
            int nx = x + ox, ny = y + oy;
            if (nx < 0 || ny < 0 || nx >= b->size || ny >= b->size) continue;
            if (b->cell[ny][nx] && b->cell[ny][nx] != b->cell[y][x]) return 1;
        }
    return 0;
}

static void TestPlacement(void) {
    for (int touch = 0; touch <= 1; touch++)
        for (int size = 8; size <= 16; size += 4)
            for (unsigned seed = 1; seed <= 50; seed++) {
                Board b;
                uint64_t rng = seed * 12345 + 1;
                CHECK(BoardPlace(&b, size, touch, &rng) == 0, "флот расставился");
                int cells = 0, foreign = 0;
                for (int y = 0; y < size; y++)
                    for (int x = 0; x < size; x++)
                        if (b.cell[y][x]) {
                            cells++;
                            foreign += HasForeignNeighbor(&b, x, y);
                        }
                CHECK(cells == 20, "всего 20 палуб");
                CHECK(b.n_ships == 10 && b.alive == 10, "10 кораблей");
                if (!touch) CHECK(foreign == 0, "при touch=0 корабли не касаются");
                CHECK(BoardCheck(&b) == NULL, "поле после расстановки в порядке");
            }
}

static void TestShoot(void) {
    Board b;
    uint64_t rng = 99;
    BoardPlace(&b, 10, 0, &rng);
    int wx = 0, wy = 0;
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++)
            if (!b.cell[y][x]) { wx = x; wy = y; }
    CHECK(BoardShoot(&b, wx, wy).kind == MISS, "по воде - мимо");
    CHECK(BoardShoot(&b, wx, wy).kind == REPEAT, "второй раз в ту же клетку - повтор");
    Ship s = b.ships[0];
    for (int i = 0; i < s.len; i++) {
        Result r = BoardShoot(&b, s.x0 + s.dx * i, s.y0 + s.dy * i);
        CHECK(r.kind == (i < s.len - 1 ? HIT : SUNK), "попал, а на последней палубе убил");
        if (r.kind == SUNK) CHECK(r.len == 4 && r.x0 == s.x0 && r.y0 == s.y0, "результат описывает убитый корабль");
    }
    CHECK(b.alive == 9, "живых стало 9");
    CHECK(BoardCheck(&b) == NULL, "поле после выстрелов в порядке");
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++) BoardShoot(&b, x, y);
    CHECK(b.alive == 0 && BoardCheck(&b) == NULL, "после стрельбы по всему полю живых нет");
}

static void TestPlayerGame(const char* strat, int touch) {
    Config c = {10, touch, 1, 0, 0, 1, 1, {"", ""}, "none"};
    strcpy(c.strat[0], strat);
    for (unsigned seed = 1; seed <= 40; seed++) {
        Board b;
        Player p;
        uint64_t rng = seed * 777 + 3;
        BoardPlace(&b, 10, touch, &rng);
        PlayerInit(&p, &c, 0, seed);
        int shots = 0, repeats = 0;
        while (b.alive > 0 && shots < 100) {
            int x, y;
            if (PlayerChoose(&p, &x, &y) != 0) break;
            Result r = BoardShoot(&b, x, y);
            if (r.kind == REPEAT) repeats++;
            PlayerOnResult(&p, x, y, &r);
            shots++;
        }
        CHECK(repeats == 0, "игрок не стреляет дважды в одну клетку");
        CHECK(b.alive == 0, "игрок потопил весь флот");
    }
}

static void TestHunt(void) {
    Config c = {10, 0, 1, 0, 0, 1, 1, {"hunt", "hunt"}, "none"};
    Player p;
    PlayerInit(&p, &c, 0, 5);
    Result hit = {HIT, 0, 0, 0, 0, 0};
    PlayerOnResult(&p, 5, 5, &hit);
    int x, y;
    CHECK(PlayerChoose(&p, &x, &y) == 0, "hunt выбрал клетку");
    CHECK(abs(x - 5) + abs(y - 5) == 1, "после попадания hunt бьёт рядом");
    Result sunk = {SUNK, 1, 3, 3, 1, 0};
    PlayerOnResult(&p, 3, 3, &sunk);
    CHECK(p.known[2][2] == 1 && p.known[4][4] == 1 && p.known[3][4] == 1, "вокруг убитого помечена вода");
    CHECK(p.known[3][3] == 3, "сам корабль помечен убитым");
}

static void TestRand(void) {
    uint64_t s = 42;
    int ok = 1, seen[7] = {0};
    for (int i = 0; i < 1000; i++) {
        uint32_t v = Rand(&s, 7);
        if (v >= 7) ok = 0;
        else seen[v] = 1;
    }
    CHECK(ok, "Rand не выходит за границы");
    for (int i = 0; i < 7; i++) CHECK(seen[i], "Rand выдаёт все значения");
}

int main(void) {
    TestPlacement();
    TestShoot();
    TestPlayerGame("random", 0);
    TestPlayerGame("random", 1);
    TestPlayerGame("hunt", 0);
    TestPlayerGame("hunt", 1);
    TestHunt();
    TestRand();
    printf("проверок: %d, провалено: %d\n", checks, failed);
    return failed ? 1 : 0;
}
