#ifndef _CBCAST_H_
#define _CBCAST_H_

#include <stdint.h>

#include <netinet/in.h>
#include <netinet/udp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <threads.h>

#include "cjson/cJSON.h"
#include "result.h"
#include "vector_clock.h"

#define NETWORK_SIMULATION
#define NETWORK_SIMULATION_DROP 10

#define STATISTICS

enum CBcastMessageType {
  CBC_DATA = 1,
  CBC_ACK,
  CBC_RETRANSMIT_REQ,
  CBC_RETRANSMIT,
  CBC_HEARTBEAT, // UNUSED
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
};
typedef struct CBcastReceivedMessage cbcast_received_msg_t;

struct CBcastOutgoingMessage {
  cbcast_msg_t *message;
  struct sockaddr_in *addr;
  int socket_flags;
};
typedef struct CBcastOutgoingMessage cbcast_outgoing_msg_t;

struct CBcastSentMessage {
  cbcast_msg_t *message;
  uint64_t ack_target;
  uint64_t ack_bitmap;
};
typedef struct CBcastSentMessage cbcast_sent_msg_t;

struct CBcastStats {
  uint64_t recv_msg_count;

  uint64_t sent_msg_count;
  uint64_t dropped_msg_count;

  uint64_t sent_ack_count;
  uint64_t dropped_ack_count;

  uint64_t sent_retransmit_req_count;
  uint64_t dropped_retransmit_req_count;

  uint64_t sent_retransmit_count;
  uint64_t dropped_retransmit_count;

  uint64_t delivered_msg_count;
  uint64_t delivery_queue_size;
  uint64_t delivery_queue_max_size;

  uint64_t sent_msg_buffer_size;
  uint64_t sent_msg_buffer_max_size;

  uint64_t held_msg_buffer_size;
  uint64_t held_msg_buffer_max_size;

  uint64_t *vector_clock_snapshot;
  uint64_t num_peers;
};
typedef struct CBcastStats cbcast_stats_t;

struct CBcast {
  int socket_fd;
  uint64_t pid;

  vector_clock_t *vclock;

  struct CBCPeer {
    uint64_t pid;
    size_t pos;
    struct sockaddr_in *addr;
  } **peers;
  pthread_mutex_t peer_lock;

  pthread_t send_thread;
  pthread_mutex_t send_lock;
  pthread_cond_t send_cond;
  cbcast_outgoing_msg_t **send_queue;
  cbcast_sent_msg_t **sent_msg_buffer;

  pthread_t recv_thread;
  pthread_mutex_t recv_lock;
  cbcast_received_msg_t **held_msg_buffer;
  cbcast_received_msg_t **delivery_queue;

#ifdef STATISTICS
  pthread_mutex_t stats_lock;
  cbcast_stats_t *stats;
#endif
};

typedef struct CBcast cbcast_t;
typedef struct CBCPeer cbcast_peer_t;

// cbc_init.c
Result *cbc_init(uint64_t pid, uint64_t max_p, uint16_t port);
void cbc_free(cbcast_t *cbc);

Result *cbc_start(cbcast_t *cbc);
void cbc_stop(cbcast_t *cbc);

// cbc_peer.c
Result *cbc_add_peer(cbcast_t *cbc, const uint64_t pid, const char *ipv4,
                     const uint16_t port);
void cbc_remove_peer(cbcast_t *cbc, const uint64_t pid);

uint16_t cbc_peer_find_by_addr(cbcast_t *cbc, struct sockaddr_in *addr);
struct sockaddr_in *cbc_peer_get_addr_copy(cbcast_t *cbc, const uint64_t pid);

// message.c
Result *cbc_msg_create(const cbcast_msg_kind_t kind, const char *payload,
                       const uint16_t payload_len);
void cbc_msg_free(cbcast_msg_t *msg);

char *cbc_msg_serialize(const cbcast_msg_t *msg, size_t *out_size);
Result *cbc_msg_deserialize(const char *bytes);

Result *cbc_outgoing_msg_create(cbcast_msg_t *msg, struct sockaddr_in *addr);
void cbc_outgoing_msg_free(cbcast_outgoing_msg_t *msg);

Result *cbc_sent_msg_create(cbcast_msg_t *msg, uint16_t ack_target);
void cbc_sent_msg_free(cbcast_sent_msg_t *msg);
Result *cbc_received_msg_create(cbcast_msg_t *msg, uint16_t sender_pid);
void cbc_received_msg_free(cbcast_received_msg_t *msg);

// receive.c
cbcast_received_msg_t *cbc_receive(cbcast_t *cbc);
void *cbc_recv_thread(void *arg);

// send.c
Result *cbc_send(cbcast_t *cbc, const char *payload, const size_t payload_len);
void *cbc_send_thread(void *arg);

#ifdef STATISTICS
cJSON *cbc_collect_statistics(cbcast_t *cbc);
cJSON *create_loki_log(cJSON *log_message);
void send_stats_to_loki(char *json_payload, const char *loki_url);
#endif

#endif
