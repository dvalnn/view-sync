#include "cbcast.h"
#include "lib/stb_ds.h"
#include "unistd.h"
#include <stdio.h>
#include <stdlib.h>

Result *cbc_init(uint64_t pid, uint64_t max_p, uint16_t port) {
  // Validate arguments
  if (!max_p || pid >= max_p) {
    return result_new_err("[cbc_init] args");
  }

  // Allocate memory for the cbcast_t struct
  cbcast_t *cbc = malloc(sizeof(cbcast_t));
  if (!cbc) {
    return result_new_err("[cbc_init] malloc failed");
  }

  // Initialize the vector clock
  cbc->vclock =
      result_expect(vc_init(max_p), "[cbc_init] failed to init vclock");

  cbc->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (cbc->socket_fd < 0) {
    vc_free(cbc->vclock);
    free(cbc);
    return result_new_err("[cbc_init] failed to create socket");
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(cbc->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(cbc->socket_fd);
    vc_free(cbc->vclock);
    free(cbc);
    return result_new_err("[cbc_init] failed to bind socket");
  }

  cbc->pid = pid;
  cbc->peers = NULL;
  cbc->held_buf = NULL;
  cbc->delivery_queue = NULL;
  cbc->sent_buf = NULL;

  return result_new_ok(cbc);
}

void cbc_free(cbcast_t *cbc) {
  if (!cbc)
    return;

  // Free vector clock
  vc_free(cbc->vclock);

  // Free peers
  for (int i = 0; i < arrlen(cbc->peers); i++) {
    free(cbc->peers[i]->addr); // Free dynamically allocated address
    free(cbc->peers[i]);       // Free the peer struct
  }
  arrfree(cbc->peers);

  // Free held buffer
  for (int i = 0; i < arrlen(cbc->held_buf); i++) {
    free(cbc->held_buf[i]->payload); // Free payload string
    free(cbc->held_buf[i]);          // Free the message struct
  }
  arrfree(cbc->held_buf);

  // Free delivery queue
  for (int i = 0; i < arrlen(cbc->delivery_queue); i++) {
    free(cbc->delivery_queue[i]); // Free each message in the delivery queue
  }
  arrfree(cbc->delivery_queue);

  // Free retransmit buffer
  for (int i = 0; i < arrlen(cbc->sent_buf); i++) {
    free(cbc->sent_buf[i]->payload);  // Free payload
    free(cbc->sent_buf[i]->confirms); // Free confirms
    free(cbc->sent_buf[i]);           // Free the retransmit message struct
  }
  arrfree(cbc->sent_buf);

  // Close the socket
  close(cbc->socket_fd);

  // Free the cbcast_t structure
  free(cbc);
}
