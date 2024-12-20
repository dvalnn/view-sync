#include "cbcast.h"
#include "unistd.h"
#include <stdio.h>
#include <stdlib.h>

Result *cbc_send_to_peer(const cbcast_t *cbc, const cbcast_peer_t *peer,
                         const char *payload, const size_t payload_len,
                         const int flags) {
  if (!peer || !peer->addr) {
    return result_new_err("[cbc_send_to_peer] Invalid peer or address");
  }

  if (!payload || !payload_len) {
    return result_new_err("[cbc_send_to_peer] Invalid payload");
  }

  /* cbcast_msg_hdr_t *header = (cbcast_msg_hdr_t *)payload; */
  /* printf("[cbc_send_to_peer] Worker %lu sending message type %d with clock %d " */
  /*        "to peer %lu\n", */
  /*        cbc->pid, header->kind, header->clock, peer->pid); */

  ssize_t sent_bytes =
      sendto(cbc->socket_fd, payload, payload_len, flags,
             (struct sockaddr *)peer->addr, sizeof(struct sockaddr_in));

  if (sent_bytes < 0) {
    return result_new_err("[cbc_send_to_peer] sendto failed");
  }

  return result_new_ok(NULL);
}

// ************** Network Sim wrappers ***************
//
#ifndef PACKET_LOSS_PROBABILITY
#define PACKET_LOSS_PROBABILITY 0.1
#endif

static void seed_random() {
  static int seeded = 0;
  if (!seeded) {
    int seed = time(NULL) ^ getpid();
    srand(seed); // Combine time and process ID for unique seed
    seeded = 1;
  }
}

Result *__wrap_cbc_send_to_peer(const cbcast_t *cbc, const cbcast_peer_t *peer,
                                const char *payload, const size_t payload_len,
                                const int flags) {
  static int drop_count = 0;
  static int send_count = 0;

  // Quick hack to check the kind of message, as it is the first value in the
  // header; We will only drop data packets for now
  cbcast_msg_kind_t msg_kind = *(cbcast_msg_kind_t *)(payload);
  uint16_t clock = *(uint16_t *)(payload + sizeof(cbcast_msg_kind_t));

  seed_random();
  double random_value =
      (double)rand() / RAND_MAX; // Generate random value in [0, 1)

  if (msg_kind != CBC_DATA) {
    return cbc_send_to_peer(cbc, peer, payload, payload_len, flags);
  }

  if (random_value < PACKET_LOSS_PROBABILITY) {
    drop_count++;
    // Simulate packet drop
    printf("[__wrap_cbc_send_to_peer] Worker %lu dropping data packet clock %d "
           "(%d dropped / %d sent - drop probability %.2f)\n",
           cbc->pid, clock, drop_count, send_count, PACKET_LOSS_PROBABILITY);

    return result_new_ok(NULL);
  }

  send_count++;
  return cbc_send_to_peer(cbc, peer, payload, payload_len, flags);
}
