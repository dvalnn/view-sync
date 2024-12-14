#ifndef _CBCAST_H_
#define _CBCAST_H_

#include <stdint.h>

#include <netinet/udp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <threads.h>

#include "result.h"

struct CBcast {
  uint64_t self_pid;

  struct CBCPeer {
    uint64_t pid;
    struct sockaddr_in *addr;
  } **peers;

  struct CBCVectorClock {
    uint64_t *clock;
    pthread_mutex_t clock_mtx;
  } vclock;

  struct msghdr **ready_msgs;
  struct msghdr **held_msgs;
  struct msghdr **sent_msgs;
};

typedef struct CBcast cbcast_t;
typedef struct CBCPeer cbcast_peer_t;
typedef struct CBCVectorClock cbcast_vclock_t;

Result *cbc_init(uint64_t pid, uint64_t max_p);
void cbc_free(cbcast_t *cbc);

void cbc_send(cbcast_t *cbc, char *msg);
char *cbc_rcv(cbcast_t *cbc);

#endif
