#ifndef _CBCAST_H_
#define _CBCAST_H_

#include <stdint.h>

#include <netinet/in.h>
#include <netinet/udp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <threads.h>

#include "result.h"
#include "vector_clock.h"

#define CBC_Q_LEN 20

struct CBcastInMessage {
  uint64_t pid;
  uint64_t timestamp;
  char *payload;
};
typedef struct CBcastInMessage cbcast_in_msg_t;

struct CBcastOutMessage {
  char *confirms;
  char *payload;
  size_t payload_len;
};
typedef struct CBcastOutMessage cbcast_out_msg_t;

struct CBcast {
  int socket_fd;
  uint64_t pid;

  vector_clock_t *vclock;

  struct CBCPeer {
    uint64_t pid;
    struct sockaddr_in *addr;
  } **peers;

  char **delivery_queue;
  cbcast_in_msg_t **held_buf;
  cbcast_out_msg_t **sent_buf;
};
typedef struct CBcast cbcast_t;
typedef struct CBCPeer cbcast_peer_t;

Result *cbc_init(uint64_t pid, uint64_t max_p, uint16_t port);
void cbc_free(cbcast_t *cbc);

void cbc_send(cbcast_t *cbc, char *msg, size_t msg_len);
char *cbc_rcv(cbcast_t *cbc);

Result *cbc_add_peer(cbcast_t *cbc, const uint64_t pid, const char *ipv4,
                     const uint16_t port);

#endif
