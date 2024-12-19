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

#ifndef CBC_Q_LEN
#define CBC_Q_LEN 20
#endif

enum CBcastMessageType {
  CBC_DATA = 1,
  CBC_ACK,
  CBC_RETRANSMIT_REQ,
  CBC_RETRANSMIT,
  CBC_HEARTBEAT,
};
typedef enum CBcastMessageType cbcast_msg_kind_t;

struct __attribute__((packed)) CBcastMessageHeader {
  cbcast_msg_kind_t kind;
  uint16_t clock;
  uint16_t len;
};
typedef struct CBcastMessageHeader cbcast_msg_hdr_t;

struct CBcastMessage {
  cbcast_msg_hdr_t *header;
  char *payload;
};
typedef struct CBcastMessage cbcast_msg_t;

struct CBcastReceivedMessage {
  cbcast_msg_t *message;
  uint16_t sender_pid;
  uint16_t sender_idx;
};
typedef struct CBcastReceivedMessage cbcast_received_msg_t;

struct CBcastSentMessage {
  cbcast_msg_t *message;
  char *confirms;
};
typedef struct CBcastSentMessage cbcast_sent_msg_t;

struct CBcast {
  int socket_fd;
  uint64_t pid;

  vector_clock_t *vclock;

  struct CBCPeer {
    uint64_t pid;
    size_t pos;
    struct sockaddr_in *addr;
  } **peers;

  cbcast_sent_msg_t **sent_buf;
  cbcast_received_msg_t **delivery_queue;
  cbcast_received_msg_t **held_buf;
};
typedef struct CBcast cbcast_t;
typedef struct CBCPeer cbcast_peer_t;

// common.c
Result *cbc_init(uint64_t pid, uint64_t max_p, uint16_t port);
void cbc_free(cbcast_t *cbc);

Result *cbc_add_peer(cbcast_t *cbc, const uint64_t pid, const char *ipv4,
                     const uint16_t port);

// message.c
Result *cbc_msg_create_header(cbcast_msg_kind_t kind, uint16_t len);

Result *cbc_msg_create(cbcast_msg_hdr_t *header, char *payload);
void cbc_msg_free(cbcast_msg_t *msg);

char *cbc_msg_serialize(const cbcast_msg_t *msg, size_t *out_size);
Result *cbc_msg_deserialize(const char *bytes);

// receive.c
cbcast_received_msg_t *cbc_receive(cbcast_t *cbc);
void cbc_received_message_free(cbcast_received_msg_t *msg);

// send.c
// TODO: Fix normalize return types
void cbc_send(cbcast_t *cbc, cbcast_msg_t *msg);
Result *cbc_send_to_peer(const cbcast_t *cbc, const char *payload,
                         const size_t payload_len, const int peer_idx,
                         const int flags);
#endif
