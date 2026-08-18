#ifndef CALDERADB_SERVER_H
#define CALDERADB_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "calderadb/core/types.h"

/* Opaque type */
typedef struct tcp_server tcp_server_t;

/* Request handler callback */
typedef uint8_t* (*request_handler_fn)(void* ctx, const uint8_t* req, size_t req_len, size_t* resp_len);

/* Create/destroy */
tcp_server_t* tcp_server_create(int port, int max_connections, request_handler_fn handler, void* ctx);
void tcp_server_destroy(tcp_server_t* server);

/* Run (blocking) */
void tcp_server_run(tcp_server_t* server);

/* Stop */
void tcp_server_stop(tcp_server_t* server);

/* Statistics */
int tcp_server_connections(tcp_server_t* server);

#endif /* CALDERADB_SERVER_H */