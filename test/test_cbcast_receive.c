#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include <arpa/inet.h>

#include "cbcast.h"
#include "lib/stb_ds.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>

// Pass shared state as a struct
typedef struct {
  cbcast_t *cbc;
  int sender_socket;
} TestState;

static int setup_cbcast_test_group(void **state) {
  cbcast_t *cbc = NULL;
  cbcast_peer_t *mock_peer = NULL;
  struct sockaddr_in *mock_addr = NULL;
  int cbc_socket = -1;
  int sender_socket = -1;

  // Allocate cbcast structure
  cbc = calloc(1, sizeof(cbcast_t));
  if (!cbc) {
    perror("Failed to allocate cbcast_t");
    goto cleanup;
  }

  // Step 1: Create and bind the receiver socket
  cbc_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (cbc_socket < 0) {
    perror("Failed to create receiver socket");
    goto cleanup;
  }

  struct sockaddr_in recv_addr = {0};
  recv_addr.sin_family = AF_INET;
  recv_addr.sin_port = htons(12345); // Bind to port 12345
  recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(cbc_socket, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
    perror("Failed to bind receiver socket");
    goto cleanup;
  }

  // Initialize cbcast structure
  cbc->socket_fd = cbc_socket;
  cbc->delivery_queue = NULL;
  cbc->held_buf = NULL;
  cbc->vclock = result_unwrap(vc_init(2));
  if (!cbc->vclock) {
    perror("Failed to initialize vector clock");
    goto cleanup;
  }

  // Step 2: Set up a peer
  mock_peer = malloc(sizeof(cbcast_peer_t));
  if (!mock_peer) {
    perror("Failed to allocate mock_peer");
    goto cleanup;
  }

  mock_addr = malloc(sizeof(struct sockaddr_in));
  if (!mock_addr) {
    perror("Failed to allocate mock_addr");
    goto cleanup;
  }

  mock_peer->pid = 1;
  mock_peer->addr = mock_addr;
  mock_addr->sin_family = AF_INET;
  mock_addr->sin_port = htons(54321); // Peer sending from port 54321
  mock_addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  arrput(cbc->peers, mock_peer);

  // Step 3: Create a sender socket
  sender_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (sender_socket < 0) {
    perror("Failed to create sender socket");
    goto cleanup;
  }

  struct sockaddr_in sender_addr = {0};
  sender_addr.sin_family = AF_INET;
  sender_addr.sin_port = htons(54321); // Sender's port
  sender_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(sender_socket, (struct sockaddr *)&sender_addr,
           sizeof(sender_addr)) < 0) {
    perror("Failed to bind sender socket");
    goto cleanup;
  }

  // Step 4: Allocate and set up test state
  TestState *test_state = malloc(sizeof(TestState));
  if (!test_state) {
    perror("Failed to allocate TestState");
    goto cleanup;
  }

  test_state->cbc = cbc;
  test_state->sender_socket = sender_socket;
  *state = test_state;

  return 0;

cleanup:
  // Centralized cleanup
  if (mock_peer) {
    free(mock_peer);
  }
  if (mock_addr) {
    free(mock_addr);
  }
  if (cbc) {
    if (cbc->vclock) {
      vc_free(cbc->vclock);
    }
    if (cbc_socket >= 0) {
      close(cbc_socket);
    }
    arrfree(cbc->peers);
    free(cbc);
  }
  if (sender_socket >= 0) {
    close(sender_socket);
  }
  return -1;
}

static int teardown_cbcast_test_group(void **state) {
  TestState *test_state = *state;
  if (!test_state) {
    return 0;
  }

  cbcast_t *cbc = test_state->cbc;
  if (cbc) {
    vc_free(cbc->vclock);
    arrfree(cbc->peers);
    close(cbc->socket_fd);
    free(cbc);
  }

  close(test_state->sender_socket);
  free(test_state);

  return 0;
}

static void test_cbcast_rcv_deliverable(void **state) {
  TestState *test_state = *state;
  cbcast_t *cbc = test_state->cbc;
  int sender_socket = test_state->sender_socket;

  char *msg = "Hello, World! 1";

  cbcast_msg_t *message =
      result_unwrap(cbc_msg_create(CBC_DATA, msg, strlen(msg)));
  message->header->clock = 1;

  size_t ser_size = 0;
  char *msg_bytes = cbc_msg_serialize(message, &ser_size);
  assert_non_null(msg_bytes);

  // Send a message using the pre-configured sender socket
  struct sockaddr_in recv_addr = {0};
  recv_addr.sin_family = AF_INET;
  recv_addr.sin_port = htons(12345); // Receiver's port
  recv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  assert_int_equal(sendto(sender_socket, msg_bytes, ser_size, 0,
                          (struct sockaddr *)&recv_addr, sizeof(recv_addr)),
                   ser_size);

  // Call cbc_rcv
  cbcast_received_msg_t *delivered_message = cbc_receive(cbc);

  // Validate results
  assert_non_null(delivered_message);
  assert_non_null(delivered_message->message);
  assert_string_equal(delivered_message->message->payload, "Hello, World! 1");

  // Cleanup
  free(delivered_message);
}

static void test_cbcast_rcv_held(void **state) {
  TestState *test_state = *state;
  cbcast_t *cbc = test_state->cbc;
  int sender_socket = test_state->sender_socket;

  char *msg = "Hello, World! 3";
  cbcast_msg_t *message =
      result_unwrap(cbc_msg_create(CBC_DATA, msg, strlen(msg)));
  message->header->clock = 3;

  size_t ser_size = 0;
  char *msg_bytes = cbc_msg_serialize(message, &ser_size);
  assert_non_null(msg_bytes);

  // Send a message using the pre-configured sender socket
  struct sockaddr_in recv_addr = {0};
  recv_addr.sin_family = AF_INET;
  recv_addr.sin_port = htons(12345); // Receiver's port
  recv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  assert_int_equal(sendto(sender_socket, msg_bytes, ser_size, 0,
                          (struct sockaddr *)&recv_addr, sizeof(recv_addr)),
                   ser_size);
  // Call cbc_rcv
  cbcast_received_msg_t *delivered_message = cbc_receive(cbc);
  assert_null(delivered_message);
  assert_int_equal(arrlen(cbc->held_buf), 1);
  assert_non_null(cbc->held_buf[0]);
  assert_int_equal(cbc->held_buf[0]->message->header->clock, 3);
  assert_string_equal(cbc->held_buf[0]->message->payload, msg);
}

static void test_cbcast_rcv_release_from_held(void **state) {
  TestState *test_state = *state;
  cbcast_t *cbc = test_state->cbc;
  int sender_socket = test_state->sender_socket;

  char *msg = "Hello, World! 2";
  cbcast_msg_t *message =
      result_unwrap(cbc_msg_create(CBC_DATA, msg, strlen(msg)));
  message->header->clock = 2;

  size_t ser_size = 0;
  char *msg_bytes = cbc_msg_serialize(message, &ser_size);
  assert_non_null(msg_bytes);

  // Send a message using the pre-configured sender socket
  struct sockaddr_in recv_addr = {0};
  recv_addr.sin_family = AF_INET;
  recv_addr.sin_port = htons(12345); // Receiver's port
  recv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  assert_int_equal(sendto(sender_socket, msg_bytes, ser_size, 0,
                          (struct sockaddr *)&recv_addr, sizeof(recv_addr)),
                   ser_size);
  // Call cbc_rcv
  cbcast_received_msg_t *delivered_message = cbc_receive(cbc);
  assert_non_null(delivered_message);
  assert_string_equal(delivered_message->message->payload, msg);
  assert_int_equal(arrlen(cbc->delivery_queue), 1);
  assert_non_null(cbc->delivery_queue[0]);
  assert_string_equal(cbc->delivery_queue[0]->message->payload,
                      "Hello, World! 3");
  free(delivered_message);

  delivered_message = cbc_receive(cbc);
  assert_non_null(delivered_message);
  assert_string_equal(delivered_message->message->payload, "Hello, World! 3");
  assert_int_equal(arrlen(cbc->delivery_queue), 0);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_cbcast_rcv_deliverable),
      cmocka_unit_test(test_cbcast_rcv_held),
      cmocka_unit_test(test_cbcast_rcv_release_from_held),
  };

  return cmocka_run_group_tests(tests, setup_cbcast_test_group,
                                teardown_cbcast_test_group);
}
