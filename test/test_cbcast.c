#include <setjmp.h>

#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "cbcast.h"
#include "lib/stb_ds.h"
#include "result.h"

static void test_cbcast_init(void **state) {
  (void)state;

  uint64_t valid_pid = 1;
  uint64_t max_p = 3;
  uint16_t valid_port = 8080;

  // Test valid initialization
  Result *result = cbc_init(valid_pid, max_p, valid_port);
  assert_non_null(result);
  assert_true(result_is_ok(result));

  cbcast_t *cbc = (cbcast_t *)result_unwrap(result);
  assert_non_null(cbc);
  assert_int_equal(cbc->pid, valid_pid);
  assert_non_null(cbc->vclock);
  assert_int_not_equal(cbc->socket_fd, -1);
  assert_null(cbc->peers);
  assert_null(cbc->held_buf);
  assert_null(cbc->delivery_queue);
  assert_null(cbc->sent_buf);

  // Cleanup
  cbc_free(cbc);

  // Test invalid arguments: pid >= max_p
  result = cbc_init(max_p, max_p, valid_port);
  assert_non_null(result);
  assert_true(result_is_err(result));
  result_free(result);

  // Test invalid arguments: max_p = 0
  result = cbc_init(valid_pid, 0, valid_port);
  assert_non_null(result);
  assert_true(result_is_err(result));
  result_free(result);
}

static void test_cbcast_send(void **state) {
  (void)state;

  uint64_t valid_pid = 0;
  uint64_t max_p = 2;
  uint16_t valid_port = 9090;

  // Initialize cbcast_t
  Result *result = cbc_init(valid_pid, max_p, valid_port);
  assert_non_null(result);
  assert_true(result_is_ok(result));
  cbcast_t *cbc = (cbcast_t *)result_unwrap(result);

  // Add mock peers
  cbcast_peer_t *peer1 = malloc(sizeof(cbcast_peer_t));
  cbcast_peer_t *peer2 = malloc(sizeof(cbcast_peer_t));
  assert_non_null(peer1);
  assert_non_null(peer2);

  struct sockaddr_in *addr1 = malloc(sizeof(struct sockaddr_in));
  struct sockaddr_in *addr2 = malloc(sizeof(struct sockaddr_in));
  assert_non_null(addr1);
  assert_non_null(addr2);

  addr1->sin_family = AF_INET;
  addr1->sin_port = htons(9001);
  addr1->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  addr2->sin_family = AF_INET;
  addr2->sin_port = htons(9002);
  addr2->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  peer1->pid = 1;
  peer1->addr = addr1;

  peer2->pid = 2;
  peer2->addr = addr2;

  arrput(cbc->peers, peer1);
  arrput(cbc->peers, peer2);

  // Test sending a message
  char *message = "Test message";
  cbc_send(cbc, message, strlen(message));

  // Check sent_buf contains the message
  assert_int_equal(arrlen(cbc->sent_buf), 1);
  cbcast_out_msg_t *sent_msg = cbc->sent_buf[0];
  assert_non_null(sent_msg);
  assert_non_null(sent_msg->payload);
  assert_int_equal(sent_msg->payload_len,
                   strlen(message) + 2 * sizeof(uint64_t));
  assert_int_equal(arrlen(cbc->peers), 2);

  uint64_t ts = *(uint64_t *)sent_msg->payload;
  uint64_t len = *(uint64_t *)(sent_msg->payload + sizeof(uint64_t));
  char *sent = (sent_msg->payload + 2 * sizeof(uint64_t));

  assert_int_equal(ts, 1);
  assert_int_equal(len, strlen(message));
  assert_string_equal(sent, message);

  // Check confirms are initialized
  for (int i = 0; i < arrlen(cbc->peers); i++) {
    assert_int_equal(sent_msg->confirms[i], 0);
  }

  // Cleanup
  cbc_free(cbc);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_cbcast_init),
      cmocka_unit_test(test_cbcast_send),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
