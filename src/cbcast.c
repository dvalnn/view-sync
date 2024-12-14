#include "cbcast.h"
#include "lib/stb_ds.h"
#include "unistd.h"
#include <stdio.h>

// ************************************************
// ************************************************
// ************** Private Functions ***************
// ************************************************
// ************************************************

// Updates the vector clock and returns a snapshot as a Result
static Result *cbc_update_vclock(cbcast_t *cbc) {
  pthread_mutex_lock(&cbc->vclock.clock_mtx);

  cbc->vclock.clock[cbc->self_pid] += 1;

  uint64_t *snapshot = NULL;
  for (size_t i = 0; i < (size_t)arrlen(cbc->vclock.clock); i++) {
    arrput(snapshot, cbc->vclock.clock[i]);
  }

  pthread_mutex_unlock(&cbc->vclock.clock_mtx);

  if (!snapshot) {
    return result_new_err(
        "[cbc_update_vclock] Failed to create vector clock snapshot");
  }

  return result_new_ok(snapshot);
}

// Creates the payload and returns it as a Result
static Result *cbc_create_payload(uint64_t *vclock_snapshot, size_t vclock_len,
                                  char *msg, size_t msg_len) {
  size_t payload_len = vclock_len * sizeof(uint64_t) + msg_len;
  char *payload = malloc(payload_len);
  if (!payload) {
    return result_new_err(
        "[cbc_create_payload] Failed to allocate memory for payload");
  }

  // Copy vector clock and message into the payload
  memcpy(payload, vclock_snapshot, vclock_len * sizeof(uint64_t));
  memcpy(payload + vclock_len * sizeof(uint64_t), msg, msg_len);

  return result_new_ok(payload);
}

static Result *cbc_send_to_peer(int socket_fd, cbcast_peer_t *peer,
                                char *payload, size_t payload_len) {
  if (!peer || !peer->addr) {
    return result_new_err("[cbc_send_to_peer] Invalid peer or address");
  }

  ssize_t sent_bytes =
      sendto(socket_fd, payload, payload_len, 0, (struct sockaddr *)peer->addr,
             sizeof(struct sockaddr_in));
  if (sent_bytes < 0) {
    perror("[cbc_send_to_peer] sendto failed");
    return result_new_err("[cbc_send_to_peer] sendto failed");
  }

  return result_new_ok(NULL);
}

// Stores the sent message in sent_msgs and returns a Result
/* static Result *cbc_store_sent_message(cbcast_t *cbc, char *payload, */
/*                                       size_t payload_len) { */
/*   struct msghdr *sent_msg = malloc(sizeof(struct msghdr)); */
/*   if (!sent_msg) { */
/*     return result_new_err("[cbc_store_sent_message] Failed to allocate
 * msghdr"); */
/*   } */
/**/
/*   sent_msg->msg_iov = malloc(sizeof(struct iovec)); */
/*   if (!sent_msg->msg_iov) { */
/*     free(sent_msg); */
/*     return result_new_err("[cbc_store_sent_message] Failed to allocate
 * iovec"); */
/*   } */
/**/
/*   sent_msg->msg_iov->iov_base = malloc(payload_len); */
/*   if (!sent_msg->msg_iov->iov_base) { */
/*     free(sent_msg->msg_iov); */
/*     free(sent_msg); */
/*     return result_new_err( */
/*         "[cbc_store_sent_message] Failed to allocate payload copy"); */
/*   } */
/**/
/*   memcpy(sent_msg->msg_iov->iov_base, payload, payload_len); */
/*   sent_msg->msg_iov->iov_len = payload_len; */
/*   sent_msg->msg_iovlen = 1; */
/*   sent_msg->msg_name = NULL; */
/*   sent_msg->msg_namelen = 0; */
/**/
/*   arrput(cbc->sent_msgs, sent_msg); */
/*   return result_new_ok(NULL); */
/* } */

// ************************************************
// ************************************************
// ************** Public Functions ***************
// ************************************************
// ************************************************

Result *cbc_init(uint64_t pid, uint64_t max_p, uint16_t port) {
  if (pid < 0 || max_p <= 0 || pid >= max_p) {
    return result_new_err("[cbc_init] Invalid PID or max_p");
  }

  // Allocate the CBcast structure
  cbcast_t *new = malloc(sizeof(cbcast_t));
  if (!new) {
    return result_new_err("[cbc_init] Failed to allocate cbcast_t");
  }

  // Initialize mutex attributes
  pthread_mutexattr_t mtx_attrs;
  pthread_mutexattr_init(&mtx_attrs);
  if (pthread_mutex_init(&new->vclock.clock_mtx, &mtx_attrs) != 0) {
    free(new);
    return result_new_err("[cbc_init] Failed to initialize vector clock mutex");
  }

  // Initialize vector clock
  new->self_pid = pid;
  new->vclock.clock = NULL;
  for (uint64_t i = 0; i < max_p; i++) {
    arrput(new->vclock.clock, 0);
  }

  if ((uint64_t)arrlen(new->vclock.clock) != max_p) {
    pthread_mutex_destroy(&new->vclock.clock_mtx);
    free(new);
    return result_new_err("[cbc_init] Failed to initialize vector clock");
  }

  // Initialize peer and message arrays
  new->peers = NULL;
  new->held_msgs = NULL;
  new->ready_msgs = NULL;
  new->sent_msgs = NULL;

  // Create and configure the UDP socket
  int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    pthread_mutex_destroy(&new->vclock.clock_mtx);
    arrfree(new->vclock.clock);
    free(new);
    return result_new_err("[cbc_init] Failed to create socket");
  }

  // Bind the socket to the given port
  struct sockaddr_in local_addr = {0};
  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = INADDR_ANY; // Bind to all local interfaces
  local_addr.sin_port = htons(port);

  if (bind(socket_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
    close(socket_fd);
    pthread_mutex_destroy(&new->vclock.clock_mtx);
    arrfree(new->vclock.clock);
    free(new);
    return result_new_err("[cbc_init] Failed to bind socket");
  }

  // Store the socket in the CBcast structure
  new->socket_fd = socket_fd;

  // Return the initialized CBcast object wrapped in a Result
  return result_new_ok(new);
}

void cbc_free(cbcast_t *cbc) {
  if (!cbc)
    return;

  close(cbc->socket_fd);
  pthread_mutex_destroy(&cbc->vclock.clock_mtx);

  // Free peers' dynamically allocated memory.
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    free(cbc->peers[i]->addr);
    free(cbc->peers[i]);
  }
  arrfree(cbc->peers);

  // Free message arrays.
  for (size_t i = 0; i < (size_t)arrlen(cbc->held_msgs); i++) {
    free(cbc->held_msgs[i]);
  }
  arrfree(cbc->held_msgs);

  for (size_t i = 0; i < (size_t)arrlen(cbc->ready_msgs); i++) {
    free(cbc->ready_msgs[i]);
  }
  arrfree(cbc->ready_msgs);

  for (size_t i = 0; i < (size_t)arrlen(cbc->sent_msgs); i++) {
    free(cbc->sent_msgs[i]);
  }
  arrfree(cbc->sent_msgs);

  // Free vector clock.
  arrfree(cbc->vclock.clock);

  free(cbc);
}

void cbc_send(cbcast_t *cbc, char *msg, size_t msg_len) {
  if (!cbc || !msg || msg_len == 0) {
    fprintf(stderr, "[cbc_send] Invalid arguments\n");
    return;
  }

  // Step 1: Update vector clock and get a snapshot
  Result *vclock_res = cbc_update_vclock(cbc);
  if (result_is_err(vclock_res)) {
    fprintf(stderr, "%s\n", vclock_res->err);
    result_free(vclock_res);
    return;
  }
  uint64_t *vclock_snapshot = result_unwrap(vclock_res);

  // Step 2: Create payload
  Result *payload_res = cbc_create_payload(
      vclock_snapshot, arrlen(vclock_snapshot), msg, msg_len);
  arrfree(vclock_snapshot); // Free snapshot after use

  if (result_is_err(payload_res)) {
    fprintf(stderr, "%s\n", payload_res->err);
    result_free(payload_res);
    return;
  }
  char *payload = result_unwrap(payload_res);

  // Step 3: Send payload to all peers
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    cbcast_peer_t *peer = cbc->peers[i];
    Result *send_res = cbc_send_to_peer(
        cbc->socket_fd, peer, payload,
        msg_len + (arrlen(vclock_snapshot) * sizeof(uint64_t)));
    if (result_is_err(send_res)) {
      fprintf(stderr, "%s\n", send_res->err);
    }
    result_free(send_res);
  }

  // Step 4: Store sent message
  /* Result *store_res = cbc_store_sent_message( */
  /*     cbc, payload, msg_len + (arrlen(vclock_snapshot) * sizeof(uint64_t)));
   */
  /* if (result_is_err(store_res)) { */
  /*   fprintf(stderr, "%s\n", store_res->err); */
  /*   result_free(store_res); */
  /* } */

  // Cleanup
  free(payload);
  result_free(payload_res);
}

Result *cbc_receive_raw(cbcast_t *cbc, char **buffer, size_t *recv_len);
Result *cbc_parse_message(cbcast_t *cbc, char *buffer, size_t recv_len,
                          uint64_t **vclock, char **msg, size_t *msg_len);
Result *cbc_handle_message(cbcast_t *cbc, uint64_t *vclock, char *msg,
                           size_t msg_len);
Result *cbc_enqueue_message(struct msghdr ***queue, uint64_t *vclock, char *msg,
                            size_t msg_len);
Result *cbc_update_local_vclock(cbcast_t *cbc, uint64_t *vclock);

char *cbc_rcv(cbcast_t *cbc) {
  if (!cbc) {
    return NULL; // Invalid input
  }

  char *buffer = NULL;
  size_t recv_len = 0;
  uint64_t *vclock = NULL;
  char *msg = NULL;
  size_t msg_len = 0;

  // Step 1: Receive raw data
  Result *recv_res = cbc_receive_raw(cbc, &buffer, &recv_len);
  if (result_is_err(recv_res)) {
    fprintf(stderr, "[cbc_rcv] %s\n", recv_res->err);
    result_free(recv_res);
    return NULL;
  }
  result_free(recv_res);

  // Step 2: Parse the message
  Result *parse_res =
      cbc_parse_message(cbc, buffer, recv_len, &vclock, &msg, &msg_len);
  free(buffer); // Clean up buffer after parsing
  if (result_is_err(parse_res)) {
    fprintf(stderr, "[cbc_rcv] %s\n", parse_res->err);
    result_free(parse_res);
    return NULL;
  }
  result_free(parse_res);

  // Step 3: Handle the message based on causality
  /* Result *handle_res = cbc_handle_message(cbc, vclock, msg, msg_len); */
  /* if (result_is_err(handle_res)) { */
  /*   fprintf(stderr, "[cbc_rcv] %s\n", handle_res->err); */
  /*   result_free(handle_res); */
  /*   free(msg);  // Clean up the message if handling fails */
  /*   return NULL; */
  /* } */
  /* result_free(handle_res); */

  // Return the message (for demonstration, can be omitted if just queueing)
  return msg;
}

Result *cbc_receive_raw(cbcast_t *cbc, char **buffer, size_t *recv_len) {
  if (!cbc || !buffer || !recv_len) {
    return result_new_err("[cbc_receive_raw] Invalid arguments");
  }

  size_t buffer_size = 1024; // Adjust as needed
  *buffer = malloc(buffer_size);
  if (!*buffer) {
    return result_new_err("[cbc_receive_raw] Failed to allocate buffer");
  }

  struct sockaddr_in sender_addr;
  socklen_t addr_len = sizeof(sender_addr);
  ssize_t len = recvfrom(cbc->socket_fd, *buffer, buffer_size, 0,
                         (struct sockaddr *)&sender_addr, &addr_len);
  if (len < 0) {
    free(*buffer);
    return result_new_err("[cbc_receive_raw] recvfrom failed");
  }

  *recv_len = len;
  return result_new_ok(NULL);
}

Result *cbc_parse_message(cbcast_t *cbc, char *buffer, size_t recv_len,
                          uint64_t **vclock, char **msg, size_t *msg_len) {
  if (!cbc || !buffer || !vclock || !msg || !msg_len) {
    return result_new_err("[cbc_parse_message] Invalid arguments");
  }

  size_t vclock_len = arrlen(cbc->vclock.clock);
  if (recv_len < vclock_len * sizeof(uint64_t)) {
    return result_new_err(
        "[cbc_parse_message] Received message too short for vector clock");
  }

  *vclock = (uint64_t *)buffer;
  *msg = buffer + vclock_len * sizeof(uint64_t);
  *msg_len = recv_len - vclock_len * sizeof(uint64_t);

  return result_new_ok(NULL);
}

Result *cbc_handle_message(cbcast_t *cbc, uint64_t *vclock, char *msg,
                           size_t msg_len) {
  if (!cbc || !vclock || !msg) {
    return result_new_err("[cbc_handle_message] Invalid arguments");
  }

  pthread_mutex_lock(&cbc->vclock.clock_mtx);

  // Determine readiness
  int is_ready = 1;
  for (size_t i = 0; i < (size_t)arrlen(cbc->vclock.clock); i++) {
    if (i == cbc->self_pid) {
      if (vclock[i] != cbc->vclock.clock[i] + 1) {
        is_ready = 0;
        break;
      }
    } else if (vclock[i] > cbc->vclock.clock[i]) {
      is_ready = 0;
      break;
    }
  }

  // Update queue and local clock
  Result *res;
  if (is_ready) {
    res = cbc_update_local_vclock(cbc, vclock);
    if (result_is_err(res)) {
      pthread_mutex_unlock(&cbc->vclock.clock_mtx);
      return res; // Pass error up
    }
    result_free(res);

    res = cbc_enqueue_message(&cbc->ready_msgs, vclock, msg, msg_len);
  } else {
    res = cbc_enqueue_message(&cbc->held_msgs, vclock, msg, msg_len);
  }

  pthread_mutex_unlock(&cbc->vclock.clock_mtx);
  return res;
}

Result *cbc_enqueue_message(struct msghdr ***queue, uint64_t *vclock, char *msg,
                            size_t msg_len) {
  if (!queue || !msg) {
    return result_new_err("[cbc_enqueue_message] Invalid arguments");
  }

  struct msghdr *new_msg = malloc(sizeof(struct msghdr));
  if (!new_msg) {
    return result_new_err(
        "[cbc_enqueue_message] Failed to allocate message header");
  }

  memset(new_msg, 0, sizeof(struct msghdr));
  new_msg->msg_iov = malloc(sizeof(struct iovec));
  if (!new_msg->msg_iov) {
    free(new_msg);
    return result_new_err("[cbc_enqueue_message] Failed to allocate iovec");
  }

  new_msg->msg_iov->iov_base = strndup(msg, msg_len);
  new_msg->msg_iov->iov_len = msg_len;
  new_msg->msg_iovlen = 1;

  arrput(*queue, new_msg);
  return result_new_ok(NULL);
}

Result *cbc_update_local_vclock(cbcast_t *cbc, uint64_t *vclock) {
  if (!cbc || !vclock) {
    return result_new_err("[cbc_update_local_vclock] Invalid arguments");
  }

  for (size_t i = 0; i < (size_t)arrlen(cbc->vclock.clock); i++) {
    if (vclock[i] > cbc->vclock.clock[i]) {
      cbc->vclock.clock[i] = vclock[i];
    }
  }

  return result_new_ok(NULL);
}
