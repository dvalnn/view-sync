#include "result.h"

#include <stdio.h>
#include <stdlib.h>

Result *result_new_ok(void *value) {
  Result *r = (Result *)malloc(sizeof(Result));
  r->_is_ok = true;
  r->ok = value;
  r->err = nullptr;
  return r;
}

Result *result_new_err(const char *error) {
  Result *r = (Result *)malloc(sizeof(Result));
  r->_is_ok = false;
  r->ok = nullptr;
  r->err = error;
  return r;
}
void *_result_unwrap(Result *r, const int line, const char *func,
                     const char *file) {
  if (result_is_err(r)) {
    fprintf(stderr, "[UNWRAP] Error in %s:%s() line %d: %s\n", file, func, line,
            r->err);
    exit(EXIT_FAILURE);
  }
  void *value = r->ok;
  result_free(r);
  return value;
}

void *result_expect(Result *r, const char *msg) {
  if (result_is_err(r)) {
    fprintf(stderr, "[EXPECT] %s: %s\n", msg, r->err);
    exit(EXIT_FAILURE);
  }
  void *value = r->ok;
  result_free(r);
  return value;
}

void result_free(Result *r) { free(r); }
