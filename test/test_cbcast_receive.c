#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include <arpa/inet.h>

#include "cbcast.h"
#include "lib/stb_ds.h"
#include <arpa/inet.h> // For socket functions and sockaddr_in
#include <unistd.h>    // For close()

static void test_cbcast_rcv_with_real_socket(void **state) {
  (void)state;

  cbcast_t cbc = {0};
  int sender_socket = -1;

  // Step 1: Create and bind the receiver socket
  cbc.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  assert_true(cbc.socket_fd >= 0);

  struct sockaddr_in recv_addr = {0};
  recv_addr.sin_family = AF_INET;
  recv_addr.sin_port = htons(12345); // Bind to port 12345
  recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  assert_int_equal(
      bind(cbc.socket_fd, (struct sockaddr *)&recv_addr, sizeof(recv_addr)), 0);

  // Step 2: Initialize cbcast structure
  cbc.delivery_queue = NULL;
  cbc.held_buf = NULL;
  cbc.vclock = result_unwrap(vc_init(2));

  // Set up a peer
  cbcast_peer_t mock_peer = {0};
  struct sockaddr_in mock_addr = {0};
  mock_peer.pid = 1;
  mock_peer.addr = &mock_addr;
  mock_addr.sin_family = AF_INET;
  mock_addr.sin_port = htons(54321); // Peer sending from port 54321
  inet_pton(AF_INET, "127.0.0.1", &(mock_addr.sin_addr));
  arrput(cbc.peers, &mock_peer);

  // Step 3: Send a message using another socket
  sender_socket = socket(AF_INET, SOCK_DGRAM, 0);
  assert_true(sender_socket >= 0);

  struct sockaddr_in sender_addr = {0};
  sender_addr.sin_family = AF_INET;
  sender_addr.sin_port = htons(54321); // Sender's port
  sender_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  assert_int_equal(
      bind(sender_socket, (struct sockaddr *)&sender_addr, sizeof(sender_addr)),
      0);

  uint64_t timestamp = 1;
  uint64_t msg_len = 13; // Length of "Hello, World!"
  char message[32] = {0};
  memcpy(message, &timestamp, sizeof(uint64_t));
  memcpy(message + sizeof(uint64_t), &msg_len, sizeof(uint64_t));
  memcpy(message + 2 * sizeof(uint64_t), "Hello, World!", 13);

  assert_int_equal(sendto(sender_socket, message, sizeof(message), 0,
                          (struct sockaddr *)&recv_addr, sizeof(recv_addr)),
                   sizeof(message));

  // Step 5: Call cbc_rcv
  char *delivered_message = cbc_rcv(&cbc);

  // Validate results
  assert_non_null(delivered_message);
  assert_string_equal(delivered_message, "Hello, World!");

  // Cleanup
  free(delivered_message);
  vc_free(cbc.vclock);
  arrfree(cbc.peers);
  close(cbc.socket_fd);
  close(sender_socket);
}

/* static void test_cbcast_rcv_held(void **state) { */
/*   (void)state; */
/*   cbcast_t cbc = {0}; */
/**/
/*   // Mock initialization of vector clock and peers */
/*   cbc.vclock = mock_type(vector_clock_t *); */
/*   cbc.peers = NULL; */
/*   cbc.delivery_queue = NULL; */
/*   cbc.held_buf = NULL; */
/**/
/*   // Mock the causality check to return 1 (causality not satisfied) */
/*   will_return(vc_check_causality, 1); */
/**/
/*   // Mock the socket FD and receiving a valid message */
/*   cbc.socket_fd = mock_type(int); */
/*   expect_value(recvfrom, socket_fd, cbc.socket_fd); */
/*   will_return(recvfrom, 32); // Simulate a valid received message length */
/**/
/*   // Create a mock message */
/*   char mock_message[32] = {0}; */
/*   uint64_t timestamp = 1234; */
/*   uint64_t msg_len = 16; */
/*   memcpy(mock_message, &timestamp, sizeof(uint64_t)); */
/*   memcpy(mock_message + 8, &msg_len, sizeof(uint64_t)); */
/*   memcpy(mock_message + 16, "Hello, World!", 13); */
/**/
/*   // Simulate receiving the mock message */
/*   will_return(__wrap_recvfrom, mock_message); */
/**/
/*   // Call the function under test */
/*   char *delivered_message = cbc_rcv(&cbc); */
/**/
/*   // Validate that no message is delivered immediately */
/*   assert_null(delivered_message); */
/**/
/*   // Check the held buffer for the message */
/*   assert_int_equal(arrlen(cbc.held_buf), 1); */
/*   assert_string_equal(cbc.held_buf[0]->payload, "Hello, World!"); */
/**/
/*   // Cleanup */
/*   for (size_t i = 0; i < (size_t)arrlen(cbc.held_buf); i++) { */
/*     free(cbc.held_buf[i]->payload); */
/*     free(cbc.held_buf[i]); */
/*   } */
/*   arrfree(cbc.held_buf); */
/* } */

/* static void test_cbcast_rcv_release_from_held(void **state) { */
/*   (void)state; */
/*   cbcast_t cbc = {0}; */
/**/
/*   // Mock initialization of vector clock and peers */
/*   cbc.vclock = mock_type(vector_clock_t *); */
/*   cbc.peers = NULL; */
/*   cbc.delivery_queue = NULL; */
/*   cbc.held_buf = NULL; */
/**/
/*   // Insert a message into the held buffer */
/*   cbcast_in_msg_t *held_msg = malloc(sizeof(cbcast_in_msg_t)); */
/*   assert_non_null(held_msg); */
/**/
/*   held_msg->pid = 1; */
/*   held_msg->timestamp = 1234; */
/*   held_msg->payload = strdup("Held Message"); */
/*   arrput(cbc.held_buf, held_msg); */
/**/
/*   // Mock the causality check to return 0 (causality satisfied) */
/*   will_return(vc_check_causality, 0); */
/**/
/*   // Call the function under test */
/*   char *delivered_message = cbc_rcv(&cbc); */
/**/
/*   // Validate that the held message is delivered */
/*   assert_non_null(delivered_message); */
/*   assert_string_equal(delivered_message, "Held Message"); */
/**/
/*   // Check that the held buffer is empty */
/*   assert_int_equal(arrlen(cbc.held_buf), 0); */
/**/
/*   // Cleanup */
/*   free(delivered_message); */
/*   arrfree(cbc.held_buf); */
/* } */

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_cbcast_rcv_with_real_socket),
      /* cmocka_unit_test(test_cbcast_rcv_held), */
      /* cmocka_unit_test(test_cbcast_rcv_release_from_held), */
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
