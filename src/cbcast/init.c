#include "cbcast.h"
#include "lib/stb_ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// ************** Public Functions ***************
//
Result *cbc_init(uint64_t pid, uint64_t max_p, uint16_t port) {
  // Validate arguments
  if (!max_p || pid >= max_p) {
    return result_new_err("[cbc_init] args");
  }

  // Allocate memory for the cbcast_t struct
  cbcast_t *cbc = calloc(1, sizeof(cbcast_t));
  if (!cbc) {
    return result_new_err("[cbc_init] failed to malloc cbcast_t");
  }

#ifdef STATISTICS
  cbc->stats = calloc(1, sizeof(cbcast_stats_t));
  if (!cbc->stats) {
    cbc_free(cbc);
    return result_new_err("[cbc_init] failed to malloc stats");
  }
#endif

  cbc->pid = pid;
  cbc->vclock =
      result_expect(vc_init(max_p), "[cbc_init] failed to init vclock");

  // TODO: Make socket options configurable
  cbc->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (cbc->socket_fd < 0) {
    vc_free(cbc->vclock);
    free(cbc);
    return result_new_err("[cbc_init] failed to create socket");
  }

  // TODO: Make addr configurable
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(cbc->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(cbc->socket_fd);
    vc_free(cbc->vclock);
    free(cbc);
    return result_new_err("[cbc_init] failed to bind socket");
  }

  // Init stb-ds dynamic arrays to NULL
  {
    cbc->peers = NULL;

    cbc->send_queue = NULL;
    cbc->sent_msg_buffer = NULL;

    cbc->held_msg_buffer = NULL;
    cbc->delivery_queue = NULL;
  }

  // Init peer lock
  {
    pthread_mutexattr_t peer_lock_attr;
    pthread_mutexattr_init(&peer_lock_attr);
    pthread_mutex_init(&cbc->peer_lock, &peer_lock_attr);
  }

  // Init send thread lock and condition
  {
    pthread_mutexattr_t send_lock_attr;
    pthread_mutexattr_init(&send_lock_attr);
    pthread_mutex_init(&cbc->send_lock, &send_lock_attr);

    pthread_condattr_t send_cond_attr;
    pthread_condattr_init(&send_cond_attr);
    pthread_cond_init(&cbc->send_cond, &send_cond_attr);
  }

  // Init recv thread lock
  {
    pthread_mutexattr_t recv_lock_attr;
    pthread_mutexattr_init(&recv_lock_attr);
    pthread_mutex_init(&cbc->recv_lock, &recv_lock_attr);
  }

#ifdef STATISTICS
  pthread_mutexattr_t stats_lock_attr;
  pthread_mutexattr_init(&stats_lock_attr);
  pthread_mutex_init(&cbc->stats_lock, &stats_lock_attr);
#endif

  return result_new_ok(cbc);
}

Result *cbc_start(cbcast_t *cbc) {
  if (!cbc) {
    return result_new_err("[cbc_start] Invalid arguments");
  }

  // Start the send and receive threads
  pthread_attr_t send_thread_attr;
  pthread_attr_init(&send_thread_attr);
  pthread_create(&cbc->send_thread, &send_thread_attr, cbc_send_thread, cbc);

  pthread_attr_t recv_thread_attr;
  pthread_attr_init(&recv_thread_attr);
  pthread_create(&cbc->recv_thread, &recv_thread_attr, cbc_recv_thread, cbc);

  return result_new_ok(NULL);
}

void cbc_free(cbcast_t *cbc) {
  if (!cbc) {
    return;
  }

  // Free vector clock
  if (cbc->vclock) {
    vc_free(cbc->vclock);
  }

  // Free peers array
  if (cbc->peers) {
    for (int i = 0; i < arrlen(cbc->peers); i++) {
      if (cbc->peers[i]) {
        free(cbc->peers[i]->addr); // Free dynamically allocated address
        free(cbc->peers[i]);       // Free the peer struct
      }
    }
    arrfree(cbc->peers);
  }

  // Free send queue
  if (cbc->send_queue) {
    for (int i = 0; i < arrlen(cbc->send_queue); i++) {
      if (cbc->send_queue[i]) {
        cbc_outgoing_msg_free(cbc->send_queue[i]);
      }
    }
    arrfree(cbc->send_queue);
  }

  // Free retransmit buffer
  if (cbc->sent_msg_buffer) {
    for (int i = 0; i < arrlen(cbc->sent_msg_buffer); i++) {
      if (cbc->sent_msg_buffer[i]) {
        cbc_sent_msg_free(cbc->sent_msg_buffer[i]);
      }
    }
    arrfree(cbc->sent_msg_buffer);
  }

  // Free held buffer
  if (cbc->held_msg_buffer) {
    for (int i = 0; i < arrlen(cbc->held_msg_buffer); i++) {
      cbc_received_msg_free(cbc->held_msg_buffer[i]);
    }
    arrfree(cbc->held_msg_buffer);
  }

  // Free delivery queue
  if (cbc->delivery_queue) {
    for (int i = 0; i < arrlen(cbc->delivery_queue); i++) {
      cbc_received_msg_free(cbc->delivery_queue[i]);
    }
    arrfree(cbc->delivery_queue);
  }

  // Close socket if valid
  if (cbc->socket_fd >= 0) {
    close(cbc->socket_fd);
  }

#ifdef STATISTICS
  if (cbc->stats) {
    if (cbc->stats->vector_clock_snapshot)
      free(cbc->stats->vector_clock_snapshot);

    free(cbc->stats);
  }
  pthread_mutex_destroy(&cbc->stats_lock);
#endif

  // Destroy locks
  pthread_mutex_destroy(&cbc->send_lock);
  pthread_mutex_destroy(&cbc->recv_lock);
  pthread_mutex_destroy(&cbc->peer_lock);

  pthread_cond_destroy(&cbc->send_cond);

  // Free the cbcast_t structure
  free(cbc);
}

void cbc_stop(cbcast_t *cbc) {
  if (!cbc) {
    return;
  }

  // kill send and receive threads
  pthread_cancel(cbc->send_thread);
  pthread_cancel(cbc->recv_thread);

  pthread_join(cbc->send_thread, NULL);
  pthread_join(cbc->recv_thread, NULL);
}
