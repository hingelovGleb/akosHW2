#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

volatile sig_atomic_t g_stop = 0;

int ipc_mode = 0;
const char* ipc_session = "battle";

static void OnSignal(int sig) { g_stop = sig; }

static int Number(const char* text) {
    char* end; errno = 0;
    long n = strtol(text, &end, 10);
    return errno || !*text || *end || n < 0 || n > INT_MAX ? -1 : (int)n;
}

static int Set(Config* c, const char* k, const char* v) {
    int numeric = !strcmp(k,"size") || !strcmp(k,"width") || !strcmp(k,"height") ||
        !strcmp(k,"first") || !strcmp(k,"repeat") || !strcmp(k,"touch") ||
        !strcmp(k,"again") || !strcmp(k,"max_moves") || !strcmp(k,"delay") || !strcmp(k,"games");
    if (numeric && Number(v) < 0) return -1;
    if (!strcmp(k, "size")) c->width = c->height = atoi(v);
    else if (!strcmp(k, "width")) c->width = atoi(v);
    else if (!strcmp(k, "height")) c->height = atoi(v);
    else if (!strcmp(k, "first")) c->first = atoi(v);
    else if (!strcmp(k, "repeat")) c->repeat = atoi(v);
    else if (!strcmp(k, "placement")) {
        if (strcmp(v, "random") && strcmp(v, "ordered")) return -1;
        c->ordered = !strcmp(v, "ordered");
    } else if (!strcmp(k, "fleet")) {
        char buf[128], *save;
        if (strlen(v) >= sizeof(buf)) return -1;
        strcpy(buf, v); c->count = 0;
        for (char* t = strtok_r(buf, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
            if (c->count == MAXSHIPS) return -1;
            c->fleet[c->count++] = Number(t);
        }
        if (!c->count) return -1;
    }
    else if (!strcmp(k, "touch")) c->touch = atoi(v);
    else if (!strcmp(k, "again")) c->again = atoi(v);
    else if (!strcmp(k, "max_moves")) c->max_moves = atoi(v);
    else if (!strcmp(k, "delay")) c->delay = atoi(v);
    else if (!strcmp(k, "games")) c->games = atoi(v);
    else if (!strcmp(k, "seed")) {
        char* end; errno = 0; unsigned long n = strtoul(v, &end, 10);
        if (errno || !*v || *end || *v == '-' || n > UINT_MAX) return -1;
        c->seed = (unsigned)n;
    }
    else if (!strcmp(k, "a") || !strcmp(k, "b")) snprintf(c->strat[k[0] - 'a'], 8, "%s", v);
    else if (!strcmp(k, "log")) snprintf(c->log, sizeof(c->log), "%s", v);
    else return -1;
    return 0;
}

static int LoadFile(Config* c, const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[2048];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0 || n == (ssize_t)sizeof(buf) - 1) return -1;
    buf[n] = 0;
    char* save;
    for (char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        line[strcspn(line, "\r")] = 0;
        char* hash = strchr(line, '#');
        if (hash) *hash = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        if (Set(c, line, eq + 1) != 0) return -1;
    }
    return 0;
}

int main(int argc, char** argv) {
    Config c = {.width=10, .height=10, .again=1, .delay=100, .games=1,
        .seed=(unsigned)getpid(), .strat={"hunt", "random"}, .log="battleship.log",
        .fleet={4,3,3,2,2,2,1,1,1,1}, .count=10};
    struct sigaction sa = {0};
    sa.sa_handler = OnSignal;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);
    if (argc == 4 && !strcmp(argv[1], "player")) {
        if (strcmp(argv[3], "0") && strcmp(argv[3], "1")) return 1;
        return ConnectPlayer(argv[2], atoi(argv[3]));
    }
    int start = 1;
    if (argc >= 3 && !strcmp(argv[1], "judge")) {
        ipc_mode = 2; ipc_session = argv[2]; start = 3;
    } else if (argc >= 2 && !strcmp(argv[1], "named")) { ipc_mode = 1; start = 2; }
    else if (argc >= 2 && !strcmp(argv[1], "unnamed")) start = 2;
    for (int i = start; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            if (LoadFile(&c, argv[++i]) != 0) {
                dprintf(2, "не получилось прочитать файл настроек %s\n", argv[i]);
                return 2;
            }
        } else if (!strncmp(argv[i], "--", 2) && strchr(argv[i], '=')) {
            char* arg = strdup(argv[i] + 2);
            char* eq = strchr(arg, '=');
            *eq = 0;
            int bad = Set(&c, arg, eq + 1);
            free(arg);
            if (bad) {
                dprintf(2, "неизвестный параметр: %s\n", argv[i]);
                return 1;
            }
        } else {
            dprintf(2, "Запуск: %s [-c файл] [unnamed|named|judge имя|player имя 0/1] [--size=10 --touch=0 --again=1 --seed=N --a=hunt --b=random ...]\n", argv[0]);
            return 1;
        }
    }
    int human = 0;
    for (int i = 0; i < 2; i++) {
        if (!strcmp(c.strat[i], "human")) human++;
        else if (strcmp(c.strat[i], "random") && strcmp(c.strat[i], "hunt")) human = -100;
    }
    if (c.width < 1 || c.width > MAXN || c.height < 1 || c.height > MAXN ||
        (c.first | 1) != 1 || (c.repeat | 1) != 1 || (c.touch | 1) != 1 || (c.again | 1) != 1 || c.delay < 0 || c.max_moves < 0 ||
        c.games < 1 || c.games > 100000 || human < 0 || (human && c.games > 1)) {
        dprintf(2, "неправильные значения параметров (width/height 1..%d, touch/again 0 или 1, games 1..100000,\n"
                   "a/b: random, hunt или human, а human нельзя вместе с games>1)\n", MAXN);
        return 1;
    }

    int decks = 0;
    for (int i = 0; i < c.count; i++) {
        if (c.fleet[i] < 1 || c.fleet[i] > (c.width > c.height ? c.width : c.height)) return 1;
        decks += c.fleet[i];
    }
    if (decks > c.width * c.height || (ipc_mode == 2 && c.games != 1)) return 1;
    return Play(&c);
}
