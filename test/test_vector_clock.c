#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "result.h"
#include "vector_clock.h"

static void test_vc_init(void **state) {
  (void)state;

  Result *res = vc_init(10);
  assert_false(result_is_err(res));
  result_free(res);
}

static void test_vc_inc(void **state) {
  (void)state;

  vector_clock_t *vc = result_unwrap(vc_init(10));
  (void)result_unwrap(vc_inc(vc, 6));
  assert_int_equal(vc->clock[6], 1);
}

static void test_vc_snapshot(void **state) {
  (void)state;

  vector_clock_t *vc_a = result_unwrap(vc_init(10));
  (void)result_unwrap(vc_inc(vc_a, 6));
  (void)result_unwrap(vc_inc(vc_a, 1));
  (void)result_unwrap(vc_inc(vc_a, 2));
  (void)result_unwrap(vc_inc(vc_a, 8));

  uint64_t *snapshot = result_unwrap(vc_snapshot(vc_a));
  assert_memory_equal(snapshot, vc_a->clock, vc_a->len);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_vc_init),
      cmocka_unit_test(test_vc_inc),
      cmocka_unit_test(test_vc_snapshot),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
