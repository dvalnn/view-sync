#include "cbcast.h"
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/stb_ds.h"

#include <cmocka.h>

// Mock the external functions
extern int vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                              uint64_t timestamp);
extern void vc_update(vector_clock_t *vclock, uint64_t pid, uint64_t timestamp);

// Test setup and teardown
static int test_setup(void **state) {
  cbcast_t *cbc = malloc(sizeof(cbcast_t));
  if (!cbc) {
    return -1;
  }

  // Initialize with some values for the test
  cbc->socket_fd = 0;
  cbc->pid = 1;
  cbc->peers = NULL; // We'll mock peer initialization
  cbc->vclock = malloc(sizeof(vector_clock_t)); // Mock vector clock
  cbc->delivery_queue = NULL;
  cbc->held_buf = NULL;

  *state = cbc; // Provide the cbcast_t to the test

  return 0;
}

static int test_teardown(void **state) {
  cbcast_t *cbc = (cbcast_t *)*state;
  if (cbc->vclock) {
    free(cbc->vclock);
  }
  free(cbc);
  return 0;
}

// Mock for vc_check_causality
int vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                       uint64_t timestamp) {
  (void)vclock;
  (void)pid;
  (void)timestamp;
  // Return 0 to simulate causality satisfied (can be adjusted for testing)
  return 0;
}

// Mock for vc_update
void vc_update(vector_clock_t *vclock, uint64_t pid, uint64_t timestamp) {
  (void)vclock;
  (void)pid;
  (void)timestamp;
  // Simulate vector clock update
}

// Test receiving a valid message
static void test_cbc_rcv_valid_message(void **state) {
  cbcast_t *cbc = (cbcast_t *)*state;

  // Mock received message
  char *recv_msg = "Test message";
  size_t msg_len = strlen(recv_msg);
  uint64_t timestamp = 10;
  uint64_t sender_pid = 2;
  (void)sender_pid;

  // Prepare test input for cbc_rcv (mocked socket, sender, etc.)
  struct sockaddr_in sender_addr = {0};
  sender_addr.sin_family = AF_INET;
  sender_addr.sin_port = htons(12345);
  sender_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  (void)sender_addr;

  // Simulate a valid message being received and parsed
  // Normally, you would need to use real socket mechanisms or mocks for
  // recvfrom For simplicity, we'll simulate directly calling cbc_rcv

  char *buffer =
      malloc(msg_len + 16); // 8 bytes for timestamp + 8 bytes for msg_len
  memcpy(buffer, &timestamp, sizeof(uint64_t));   // Add timestamp
  memcpy(buffer + 8, &msg_len, sizeof(uint64_t)); // Add message length
  memcpy(buffer + 16, recv_msg, msg_len); // Add the actual message content

  // Call the cbc_rcv function with the mocked state
  char *received_msg = cbc_rcv(cbc);

  // Verify that the message is delivered
  assert_non_null(received_msg);
  assert_string_equal(received_msg, recv_msg);

  // Clean up
  free(buffer);
}

// Test handling held messages after delivering a message
static void test_cbc_rcv_with_held_messages(void **state) {
  cbcast_t *cbc = (cbcast_t *)*state;

  // Setup initial conditions
  uint64_t sender_pid = 2;
  uint64_t timestamp = 10;
  char *msg = "Test message";
  size_t msg_len = strlen(msg);

  // Simulate held messages
  cbcast_in_msg_t *held_msg = malloc(sizeof(cbcast_in_msg_t));
  held_msg->pid = sender_pid;
  held_msg->timestamp =
      timestamp - 1; // To simulate a message that should be held
  held_msg->payload = strdup(msg);

  // Add to held buffer
  arrput(cbc->held_buf, held_msg);

  // Now, simulate receiving a message with a causally correct timestamp
  char *recv_msg = "Test message after deliver";
  timestamp++;                         // Causally valid to deliver this message
  char *buffer = malloc(msg_len + 16); // Same size as before
  memcpy(buffer, &timestamp, sizeof(uint64_t));   // Add timestamp
  memcpy(buffer + 8, &msg_len, sizeof(uint64_t)); // Add message length
  memcpy(buffer + 16, recv_msg, msg_len); // Add the actual message content

  // Call cbc_rcv to simulate message reception and check causality
  char *received_msg = cbc_rcv(cbc);

  // Verify that the received message is delivered
  assert_non_null(received_msg);
  assert_string_equal(received_msg, recv_msg);

  // Check that the held message was delivered as well
  assert_int_equal(arrlen(cbc->delivery_queue),
                   2); // Should now have 2 messages

  // Clean up
  free(buffer);
}

// Test invalid inputs (e.g., null cbc)
static void test_cbc_rcv_invalid(void **state) {
  cbcast_t *cbc = (cbcast_t *)*state;
  (void)cbc;

  // Test when cbc is NULL
  char *result = cbc_rcv(NULL);
  assert_null(result);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_cbc_rcv_valid_message),
      cmocka_unit_test(test_cbc_rcv_with_held_messages),
      cmocka_unit_test(test_cbc_rcv_invalid),
  };

  return cmocka_run_group_tests(tests, test_setup, test_teardown);
}
