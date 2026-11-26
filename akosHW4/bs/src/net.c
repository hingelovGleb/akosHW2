#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int ModelStopped(void) { return g_stop; }

int OpenConnection(int server) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_port = htons(net_port);
    if (inet_pton(AF_INET, net_ip, &addr.sin_addr) != 1) { close(fd); return -1; }
    int one = 1;
    if (server) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (server ? (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) || listen(fd, 32)) :
        connect(fd, (struct sockaddr*)&addr, sizeof(addr))) { close(fd); return -1; }
    return fd;
}

int SendAll(int fd, const void* buf, size_t n) {
    const char* p = buf;
    while (n && !g_stop) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return -1;
        p += k; n -= k;
    }
    return n ? -1 : 0;
}

int ReadAll(int fd, void* buf, size_t n) {
    char* p = buf;
    while (n && !g_stop) {
        ssize_t k = read(fd, p, n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return -1;
        p += k; n -= k;
    }
    return n ? -1 : 0;
}

int ConnectPlayer(int id) {
    int fd = OpenConnection(0);
    if (fd < 0) { perror("connect"); return 2; }
    char role = '0' + id;
    int data[5]; Config c = {0};
    if (SendAll(fd, &role, 1) || ReadAll(fd, data, sizeof(data)) || ReadAll(fd, c.strat[id], 8)) {
        close(fd); return 2;
    }
    c.width = ntohl(data[0]); c.height = ntohl(data[1]);
    c.touch = ntohl(data[2]); c.repeat = ntohl(data[3]); c.seed = ntohl(data[4]);
    Player p; PlayerInit(&p, &c, id, c.seed);
    unsigned char known[MAXN][MAXN];
    uint32_t rng[2];
    if (ReadAll(fd, known, sizeof(known)) || ReadAll(fd, rng, sizeof(rng))) {
        close(fd); return 2;
    }
    for (int y = 0; y < MAXN; y++) for (int x = 0; x < MAXN; x++)
        p.known[y][x] = known[y][x];
    p.rng = ((uint64_t)ntohl(rng[0]) << 32) | ntohl(rng[1]);
    dprintf(1, "Игрок %d подключён, стратегия %s\n", id, p.strat);
    int turn;
    while (!ReadAll(fd, &turn, sizeof(turn))) {
        int x = -1, y = -1;
        if (PlayerChoose(&p, &x, &y)) x = -1;
        int xy[2] = {htonl(x), htonl(y)};
        if (SendAll(fd, xy, sizeof(xy)) || x < 0) break;
        int reply[6];
        if (ReadAll(fd, reply, sizeof(reply))) break;
        for (int i = 0; i < 6; i++) reply[i] = ntohl(reply[i]);
        Result r = {reply[0], reply[1], reply[2], reply[3], reply[4], reply[5]};
        PlayerOnResult(&p, x, y, &r);
        dprintf(1, "Выстрел %c%d: %s\n", 'A'+x, y+1,
            r.kind == MISS ? "мимо" : r.kind == HIT ? "ПОПАЛ" : r.kind == SUNK ? "УБИЛ" : "повтор");
    }
    close(fd); dprintf(1, "Игрок %d завершил работу\n", id);
    return g_stop ? 128 + g_stop : 0;
}

int ConnectObserver(void) {
    int fd = OpenConnection(0);
    if (fd < 0) { perror("connect"); return 2; }
    if (SendAll(fd, "o", 1)) { close(fd); return 2; }
    char buf[4096]; ssize_t n;
    while (!g_stop && (n = read(fd, buf, sizeof(buf))) > 0) {
        size_t pos = 0;
        while (pos < (size_t)n) {
            ssize_t k = write(1, buf + pos, n - pos);
            if (k <= 0) { close(fd); return 2; }
            pos += k;
        }
    }
    close(fd);
    return g_stop ? 128 + g_stop : 0;
}
