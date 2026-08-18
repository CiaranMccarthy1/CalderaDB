#include "calderadb/network/server.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>

struct tcp_server {
    int listen_fd;
    int port;
    int max_connections;
    atomic_bool running;
    request_handler_fn handler;
    void* ctx;
    pthread_t accept_thread;
    int* client_fds;
    size_t client_count;
    pthread_mutex_t client_mutex;
};

static void* accept_loop(void* arg);
static void handle_client(int fd, request_handler_fn handler, void* ctx);

tcp_server_t* tcp_server_create(int port, int max_connections, request_handler_fn handler, void* ctx) {
    tcp_server_t* server = calloc(1, sizeof(tcp_server_t));
    if (!server) return NULL;
    
    server->port = port;
    server->max_connections = max_connections;
    server->handler = handler;
    server->ctx = ctx;
    server->running = false;
    
    pthread_mutex_init(&server->client_mutex, NULL);
    
    server->client_fds = calloc(max_connections, sizeof(int));
    if (!server->client_fds) {
        free(server);
        return NULL;
    }
    
    /* Create socket */
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        free(server->client_fds);
        free(server);
        return NULL;
    }
    
    /* Set SO_REUSEADDR */
    int opt = 1;
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    /* Bind */
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server->listen_fd);
        free(server->client_fds);
        free(server);
        return NULL;
    }
    
    /* Listen */
    if (listen(server->listen_fd, 128) < 0) {
        close(server->listen_fd);
        free(server->client_fds);
        free(server);
        return NULL;
    }
    
    return server;
}

void tcp_server_destroy(tcp_server_t* server) {
    if (!server) return;
    
    tcp_server_stop(server);
    pthread_mutex_destroy(&server->client_mutex);
    close(server->listen_fd);
    free(server->client_fds);
    free(server);
}

void tcp_server_run(tcp_server_t* server) {
    if (!server) return;
    
    server->running = true;
    pthread_create(&server->accept_thread, NULL, accept_loop, server);
    pthread_join(server->accept_thread, NULL);
}

void tcp_server_stop(tcp_server_t* server) {
    if (!server) return;
    server->running = false;
    /* Force accept thread to exit */
    pthread_cancel(server->accept_thread);
}

static void* accept_loop(void* arg) {
    tcp_server_t* server = (tcp_server_t*)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (server->running) {
        /* Non-blocking check with select */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server->listen_fd, &readfds);
        
        struct timeval timeout = {0, 100000}; /* 100ms */
        int ret = select(server->listen_fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;
        
        int client_fd = accept(server->listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;
        
        /* Handle client connection (simple: one request, then close) */
        handle_client(client_fd, server->handler, server->ctx);
        close(client_fd);
    }
    
    return NULL;
}

static void handle_client(int fd, request_handler_fn handler, void* ctx) {
    if (!handler) return;
    
    uint8_t buffer[4096];
    ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n <= 0) return;
    
    size_t resp_len = 0;
    uint8_t* response = handler(ctx, buffer, (size_t)n, &resp_len);
    if (response && resp_len > 0) {
        write(fd, response, resp_len);
    }
}

int tcp_server_connections(tcp_server_t* server) {
    if (!server) return 0;
    return (int)server->client_count;
}