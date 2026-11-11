
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_PEERS 100
#define MAX_NAME 64
#define MAX_IP 46
#define BUFFER_SIZE 16384

typedef struct {
    char name[MAX_NAME];
    char ip[MAX_IP];
    char protocol[8];
    int server_port;
    int client_port;
} Peer;

static Peer peers[MAX_PEERS];
static int peer_count = 0;

static volatile sig_atomic_t running = 1;



static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}



static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;

    char *end = s + strlen(s);

    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';

    return s;
}

static int valid_ip(const char *ip)
{
    struct in_addr addr;
    return inet_pton(AF_INET, ip, &addr) == 1;
}

static int valid_port(const char *s)
{
    if (!*s)
        return 0;

    for (const char *p = s; *p; p++)
        if (!isdigit((unsigned char)*p))
            return 0;

    long n = strtol(s, NULL, 10);
    return n >= 1 && n <= 65535;
}

static int load_config(const char *filename)
{
    FILE *f = fopen(filename, "r");

    if (!f) {
        perror("Peer.conf");
        return -1;
    }

    char line[256];
    Peer current = {0};
    int inside = 0;

    while (fgets(line, sizeof(line), f)) {

        char *p = trim(line);

        if (!*p || *p == '#' || *p == ';')
            continue;

        if (!inside) {

            char *brace = strchr(p, '{');

            if (!brace)
                continue;

            *brace = '\0';
            char *name = trim(p);

            if (peer_count >= MAX_PEERS ||
                !*name || strlen(name) >= MAX_NAME) {
                fprintf(stderr, "[ERROR] Invalid peer name/capacity\n");
                fclose(f);
                return -1;
            }

            for (int i = 0; i < peer_count; i++) {
                if (!strcasecmp(peers[i].name, name)) {
                    fprintf(stderr, "[ERROR] Duplicate peer: %s\n", name);
                    fclose(f);
                    return -1;
                }
            }

            memset(&current, 0, sizeof(current));
            strcpy(current.name, name);
            inside = 1;
            continue;
        }

        if (!strcmp(p, "}")) {

            if (!valid_ip(current.ip) ||
                current.server_port < 1 ||
                current.client_port < 1 ||
                current.server_port == current.client_port ||
                (!strcasecmp(current.protocol, "TCP") &&
                 !strcasecmp(current.protocol, "UDP"))) {
            }

            if (!valid_ip(current.ip) ||
                current.server_port < 1 ||
                current.server_port > 65535 ||
                current.client_port < 1 ||
                current.client_port > 65535 ||
                current.server_port == current.client_port ||
                (strcasecmp(current.protocol, "TCP") != 0 &&
                 strcasecmp(current.protocol, "UDP") != 0)) {

                fprintf(stderr, "[ERROR] Invalid peer: %s\n",
                        current.name);
                fclose(f);
                return -1;
            }

            for (int i = 0; i < peer_count; i++) {
                Peer *old = &peers[i];

                if (!strcmp(old->ip, current.ip) &&
                    !strcasecmp(old->protocol, current.protocol) &&
                    (old->server_port == current.server_port ||
                     old->server_port == current.client_port ||
                     old->client_port == current.server_port ||
                     old->client_port == current.client_port)) {

                    fprintf(stderr, "[ERROR] Port conflict: %s\n",
                            current.name);
                    fclose(f);
                    return -1;
                }
            }

            peers[peer_count++] = current;
            inside = 0;
            continue;
        }

        char *colon = strchr(p, ':');

        if (!colon) {
            fprintf(stderr, "[ERROR] Invalid configuration line\n");
            fclose(f);
            return -1;
        }

        *colon = '\0';

        char *key = trim(p);
        char *value = trim(colon + 1);

        char *semi = strchr(value, ';');
        if (semi)
            *semi = '\0';

        value = trim(value);

        if (!strcasecmp(key, "ip")) {
            if (strlen(value) >= MAX_IP)
                goto invalid;
            strcpy(current.ip, value);
        }
        else if (!strcasecmp(key, "ServerPort")) {
            if (!valid_port(value))
                goto invalid;
            current.server_port = atoi(value);
        }
        else if (!strcasecmp(key, "Client Port")) {
            if (!valid_port(value))
                goto invalid;
            current.client_port = atoi(value);
        }
        else if (!strcasecmp(key, "Protocol")) {
            if (strlen(value) >= sizeof(current.protocol))
                goto invalid;
            strcpy(current.protocol, value);
        }
        else {
            fprintf(stderr, "[ERROR] Unknown key: %s\n", key);
            fclose(f);
            return -1;
        }

        continue;

invalid:
        fprintf(stderr, "[ERROR] Invalid field in peer %s\n",
                current.name);
        fclose(f);
        return -1;
    }

    fclose(f);

    if (inside || peer_count == 0) {
        fprintf(stderr, "[ERROR] Unclosed peer or no peers configured\n");
        return -1;
    }

    return peer_count;
}



static int create_socket(Peer *p, int port, int type)
{
    int fd = socket(AF_INET, type, 0);

    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    struct sockaddr_in addr = {0};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, p->ip, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (type == SOCK_STREAM && listen(fd, 10) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}




static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, buf + sent,
                         len - sent, MSG_NOSIGNAL);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0)
            return -1;

        sent += n;
    }

    return 0;
}

static void tcp_bridge(Peer *p, int backend, int client)
{
    char buffer[BUFFER_SIZE];

    int open_backend = 1;
    int open_client = 1;

    printf("[%s] TCP bridge established\n", p->name);

    while (running && (open_backend || open_client)) {

        struct pollfd fds[2] = {
            { .fd = backend, .events = open_backend ? POLLIN : 0 },
            { .fd = client, .events = open_client ? POLLIN : 0 }
        };

        int r = poll(fds, 2, 1000);

        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (r == 0)
            continue;

            if (open_backend &&
            (fds[0].revents & (POLLIN | POLLHUP))) {

            ssize_t n = recv(backend, buffer,
                             sizeof(buffer), 0);

            if (n <= 0) {
                open_backend = 0;
                shutdown(client, SHUT_WR);
            }
            else if (send_all(client, buffer, n) < 0) {
                break;
            }
        }

        if (open_client &&
            (fds[1].revents & (POLLIN | POLLHUP))) {

            ssize_t n = recv(client, buffer,
                             sizeof(buffer), 0);

            if (n <= 0) {
                open_client = 0;
                shutdown(backend, SHUT_WR);
            }
            else if (send_all(backend, buffer, n) < 0) {
                break;
            }
        }

        if ((fds[0].revents & (POLLERR | POLLNVAL)) ||
            (fds[1].revents & (POLLERR | POLLNVAL)))
            break;
    }

    close(backend);
    close(client);

    printf("[%s] TCP bridge disconnected\n", p->name);
}



static void run_tcp_peer(Peer *p)
{
    int server_fd = create_socket(p, p->server_port, SOCK_STREAM);
    int client_fd = create_socket(p, p->client_port, SOCK_STREAM);

    if (server_fd < 0 || client_fd < 0) {
        printf("[%s] TCP startup failed\n", p->name);
        if (server_fd >= 0) close(server_fd);
        if (client_fd >= 0) close(client_fd);
        return;
    }

    printf("[%s] TCP listening: backend=%d client=%d\n",
           p->name, p->server_port, p->client_port);

    int backend = -1;
    int client = -1;

    while (running) {

        struct pollfd fds[2] = {
            { .fd = backend < 0 ? server_fd : -1,
              .events = POLLIN },
            { .fd = client < 0 ? client_fd : -1,
              .events = POLLIN }
        };

        int r = poll(fds, 2, 1000);

        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (backend < 0 && (fds[0].revents & POLLIN)) {
            backend = accept(server_fd, NULL, NULL);

            if (backend >= 0)
                printf("[%s] Backend connected\n", p->name);
        }

        if (client < 0 && (fds[1].revents & POLLIN)) {
            client = accept(client_fd, NULL, NULL);

            if (client >= 0)
                printf("[%s] Client connected\n", p->name);
        }

        if (backend >= 0 && client >= 0) {
            tcp_bridge(p, backend, client);
            backend = -1;
            client = -1;
        }
    }

    if (backend >= 0) close(backend);
    if (client >= 0) close(client);

    close(server_fd);
    close(client_fd);

    printf("[%s] TCP stopped\n", p->name);
}



static void run_udp_peer(Peer *p)
{
    int server_fd = create_socket(p, p->server_port, SOCK_DGRAM);
    int client_fd = create_socket(p, p->client_port, SOCK_DGRAM);

    if (server_fd < 0 || client_fd < 0) {
        printf("[%s] UDP startup failed\n", p->name);
        if (server_fd >= 0) close(server_fd);
        if (client_fd >= 0) close(client_fd);
        return;
    }

    printf("[%s] UDP listening: backend=%d client=%d\n",
           p->name, p->server_port, p->client_port);

    struct sockaddr_in backend_addr = {0};
    struct sockaddr_in client_addr = {0};

    socklen_t backend_len = sizeof(backend_addr);
    socklen_t client_len = sizeof(client_addr);

    int backend_known = 0;
    int client_known = 0;

    char buffer[BUFFER_SIZE];

    while (running) {

        struct pollfd fds[2] = {
            { .fd = server_fd, .events = POLLIN },
            { .fd = client_fd, .events = POLLIN }
        };

        int r = poll(fds, 2, 1000);

        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (r == 0)
            continue;

        if (fds[0].revents & POLLIN) {

            struct sockaddr_in source = {0};
            socklen_t source_len = sizeof(source);

            ssize_t n = recvfrom(server_fd, buffer,
                                 sizeof(buffer), 0,
                                 (struct sockaddr *)&source,
                                 &source_len);

            if (n > 0) {

                if (!backend_known ||
                    source.sin_addr.s_addr != backend_addr.sin_addr.s_addr ||
                    source.sin_port != backend_addr.sin_port) {

                    backend_addr = source;
                    backend_len = source_len;
                    backend_known = 1;

                    printf("[%s] UDP backend registered: %s:%d\n",
                           p->name,
                           inet_ntoa(source.sin_addr),
                           ntohs(source.sin_port));
                }

                if (client_known) {
                    sendto(client_fd, buffer, n, 0,
                           (struct sockaddr *)&client_addr,
                           client_len);
                }
            }
        }

        if (fds[1].revents & POLLIN) {

            struct sockaddr_in source = {0};
            socklen_t source_len = sizeof(source);

            ssize_t n = recvfrom(client_fd, buffer,
                                 sizeof(buffer), 0,
                                 (struct sockaddr *)&source,
                                 &source_len);

            if (n > 0) {

                client_addr = source;
                client_len = source_len;
                client_known = 1;

                if (backend_known) {
                    sendto(server_fd, buffer, n, 0,
                           (struct sockaddr *)&backend_addr,
                           backend_len);
                }
            }
        }
    }

    close(server_fd);
    close(client_fd);

    printf("[%s] UDP stopped\n", p->name);
}



static void *peer_worker(void *arg)
{
    Peer *p = arg;

    if (!strcasecmp(p->protocol, "TCP"))
        run_tcp_peer(p);
    else
        run_udp_peer(p);

    return NULL;
}



int main(void)
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    printf("====================================\n");
    printf("        PEER MANAGER     \n");
    printf("====================================\n");

    printf("[*] Loading Peer.conf...\n");

    int result = load_config("Peer.conf");

    if (result < 0) {
        printf("[!] Configuration loading failed\n");
        return 1;
    }

    printf("[+] Loaded %d peers\n", peer_count);

    pthread_t threads[MAX_PEERS];
    int started[MAX_PEERS] = {0};

    for (int i = 0; i < peer_count; i++) {

        if (pthread_create(&threads[i], NULL,
                           peer_worker, &peers[i]) == 0) {

            started[i] = 1;

        } else {
            printf("[ERROR] Cannot start peer %s\n",
                   peers[i].name);
        }
    }


    while (running)
        sleep(1);

    printf("\n[*] Shutting down peers...\n");

    for (int i = 0; i < peer_count; i++) {
        if (started[i])
            pthread_join(threads[i], NULL);
    }

    printf("[+] All peers stopped\n");

    return 0;
}
