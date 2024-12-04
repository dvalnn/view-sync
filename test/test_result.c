#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "result.h"

static void test_ok_result(void **state) {
  (void)state; // unused

  Result *r = result_new_ok("hello");
  assert_true(result_is_ok(r));
  assert_string_equal(result_unwrap(r), "hello");
}

static void test_err_result(void **state) {
  (void)state; // unused

  Result *r = result_new_err("error message");
  assert_false(result_is_ok(r));
}

static void test_expect_ok(void **state) {
  (void)state; // unused

  (void)state;
  Result *r = result_new_ok("expected value");
  assert_string_equal(result_expect(r, "Unexpected error"), "expected value");
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_expect_ok),
      cmocka_unit_test(test_ok_result),
      cmocka_unit_test(test_err_result),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
