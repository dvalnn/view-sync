#include "cbcast.h"
#include "lib/stb_ds.h"
#include <stdio.h>

void *cbc_send_thread(void *arg) {
  cbcast_t *cbc = (cbcast_t *)arg;

#ifdef NETWORK_SIMULATION
#include "unistd.h"
  int seed = time(NULL) ^ getpid();
  srand(seed); // Combine time and process ID for unique seed
#endif

  for (;;) {
    pthread_mutex_lock(&cbc->send_lock);
    while (arrlen(cbc->send_queue) == 0) {
      pthread_cond_wait(&cbc->send_cond, &cbc->send_lock);
    }

    cbcast_outgoing_msg_t *outgoing = arrpop(cbc->send_queue);
    pthread_mutex_unlock(&cbc->send_lock);

    cbcast_msg_t *msg = outgoing->message;
    outgoing->message = NULL;
    cbc_outgoing_msg_free(outgoing);

    size_t msg_size = 0;
    char *msg_bytes = cbc_msg_serialize(msg, &msg_size);

#ifdef NETWORK_SIMULATION
#ifdef NETWORK_SIMULATION_DROP
    if (rand() % 100 < NETWORK_SIMULATION_DROP) {
      continue;
    }
#endif

#ifdef NETWORK_SIMULATION_DELAY
    usleep(NETWORK_SIMULATION_DELAY);
#endif
#endif

    sendto(cbc->socket_fd, msg_bytes, msg_size, outgoing->flags,
           (struct sockaddr *)outgoing->addr, sizeof(*outgoing->addr));
    free(msg_bytes);

    cbcast_sent_msg_t *sent = result_expect(cbc_sent_msg_create(msg),
                                            "Failed to create sent message");
    // NOTE: may need a mutex here
    //   Store the sent message
    arrput(cbc->sent_msg_buffer, sent);
  }

  return NULL;
}

void *cbc_recv_thread(void *arg) {
  cbcast_t *cbc = (cbcast_t *)arg;
  char header_buffer[sizeof(cbcast_msg_hdr_t)];
  char *full_msg_buffer = NULL;

  struct sockaddr sender_addr;
  for (;;) {
    memset(&sender_addr, 0, sizeof(sender_addr));
    socklen_t addr_len = sizeof(struct sockaddr_in);

    uint64_t recv_len =
        (uint64_t)recvfrom(cbc->socket_fd, header_buffer, sizeof(header_buffer),
                           MSG_PEEK, &sender_addr, &addr_len);

    if (recv_len < sizeof(cbcast_msg_hdr_t)) {
      continue; // Not enough data
    }

    cbcast_msg_hdr_t *header = (cbcast_msg_hdr_t *)header_buffer;
    size_t full_msg_size = sizeof(cbcast_msg_hdr_t) + header->len;
    full_msg_buffer = calloc(full_msg_size, sizeof(char));
    if (!full_msg_buffer) {
      fprintf(stderr, "[cbc_recv] Memory allocation failed\n");
      continue;
    }

    recv_len = (uint64_t)recvfrom(cbc->socket_fd, full_msg_buffer,
                                  full_msg_size, 0, &sender_addr, &addr_len);
    if (recv_len != full_msg_size) {
      free(full_msg_buffer);
      continue;
    }

    uint16_t sender_pid =
        cbc_peer_find_by_addr(cbc, (struct sockaddr_in *)&sender_addr);
    if (sender_pid == UINT16_MAX) {
      free(full_msg_buffer);
      continue;
    }

    Result *msg_deserialize_raw = cbc_msg_deserialize(full_msg_buffer);
    if (result_is_err(msg_deserialize_raw)) {
      free(full_msg_buffer);
      continue;
    }
    cbcast_msg_t *msg = result_expect(msg_deserialize_raw, "unreachable");
    cbcast_received_msg_t *received =
        result_expect(cbc_received_msg_create(msg, sender_pid),
                      "converting to received message should not fail");

    pthread_mutex_lock(&cbc->recv_lock);
    arrput(cbc->received_queue, received);
    pthread_mutex_unlock(&cbc->recv_lock);
  }

  return NULL;
}
