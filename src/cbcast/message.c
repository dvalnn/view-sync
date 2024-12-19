#include "cbcast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ************** Function Declaration ***************
//
static char *serialize_header_only(const cbcast_msg_t *msg, size_t *out_size);

static char *serialize_full(const cbcast_msg_t *msg, size_t *out_size);

static Result *create_msg_with_header(const cbcast_msg_kind_t kind);

static Result *create_msg_with_payload(const cbcast_msg_kind_t kind,
                                       const char *payload,
                                       const uint16_t payload_len);

// ************** Public Functions ***************
//

Result *cbc_msg_create(const cbcast_msg_kind_t kind, const char *payload,
                       const uint16_t payload_len) {
  switch (kind) {
  case CBC_ACK:
  case CBC_RETRANSMIT_REQ:
  case CBC_HEARTBEAT:
    return create_msg_with_header(kind);

  case CBC_DATA:
  case CBC_RETRANSMIT:
    return create_msg_with_payload(kind, payload, payload_len);
  }

  return RESULT_UNREACHABLE;
}

void cbc_msg_free(cbcast_msg_t *msg) {
  if (!msg) {
    return;
  }
  if (msg->header) {
    free(msg->header);
  }
  if (msg->payload) {
    free(msg->payload);
  }
  free(msg);
}

char *cbc_msg_serialize(const cbcast_msg_t *msg, size_t *out_size) {
  if (!msg || !msg->header) {
    fprintf(stderr, "[cbc_msg_serialize] Invalid message or header\n");
    return NULL;
  }

  switch (msg->header->kind) {
  case CBC_DATA:
    return serialize_full(msg, out_size);
  case CBC_ACK:
    return serialize_header_only(msg, out_size);
  case CBC_RETRANSMIT_REQ:
    return serialize_header_only(msg, out_size);
  case CBC_RETRANSMIT:
    return RESULT_UNIMPLEMENTED;
  case CBC_HEARTBEAT:
    return serialize_header_only(msg, out_size);
  }

  return RESULT_UNREACHABLE;
}

Result *cbc_msg_deserialize(const char *bytes) {
  if (!bytes) {
    return result_new_err("[cbc_msg_deserialize] Null input bytes");
  }

  // Allocate memory for the message structure
  cbcast_msg_t *msg = calloc(1, sizeof(cbcast_msg_t));
  if (!msg) {
    return result_new_err(
        "[cbc_msg_deserialize] Memory allocation failed for message");
  }

  // Allocate and copy the header
  msg->header = calloc(1, sizeof(cbcast_msg_hdr_t));
  if (!msg->header) {
    free(msg);
    return result_new_err(
        "[cbc_msg_deserialize] Memory allocation failed for header");
  }
  memcpy(msg->header, bytes, sizeof(cbcast_msg_hdr_t));

  if (msg->header->kind == CBC_HEARTBEAT || msg->header->kind == CBC_ACK) {
    return result_new_ok(msg); // no payload
  }

  // Validate header length to prevent overflows
  if (msg->header->len == 0) {
    free(msg->header);
    free(msg);
    return result_new_err("[cbc_msg_deserialize] Invalid header length");
  }

  // Allocate and copy the payload
  msg->payload =
      calloc(msg->header->len + 1, sizeof(char)); // Include null terminator
  if (!msg->payload) {
    free(msg->header);
    free(msg);
    return result_new_err(
        "[cbc_msg_deserialize] Memory allocation failed for payload");
  }
  memcpy(msg->payload, bytes + sizeof(cbcast_msg_hdr_t), msg->header->len);
  msg->payload[msg->header->len] = '\0'; // Ensure null termination

  // Debug output
  /* printf("[cbc_msg_deserialize] Deserialized message: kind=%d, " */
  /*        "clock=%d, len=%d, payload=\"%s\"\n", */
  /*        msg->header->kind, msg->header->clock, msg->header->len,
   * msg->payload); */

  return result_new_ok(msg);
}

// ************** Private Functions ***************
//
static char *serialize_header_only(const cbcast_msg_t *msg, size_t *out_size) {
  size_t total_size = sizeof(cbcast_msg_hdr_t);
  char *serialized = calloc(total_size, sizeof(char));
  if (!serialized) {
    fprintf(stderr, "[cbc_msg_serialize_heartbeat] Memory allocation failed\n");
    return NULL;
  }

  memcpy(serialized, msg->header, sizeof(cbcast_msg_hdr_t));
  *out_size = total_size;
  return serialized;
}

static char *serialize_full(const cbcast_msg_t *msg, size_t *out_size) {
  if (!msg->header->len || !msg->payload) {
    fprintf(stderr, "[cbc_msg_serialize_data] Invalid message\n");
    return NULL;
  }

  char *serialized = serialize_header_only(msg, out_size);
  if (!serialized) {
    fprintf(stderr, "[cbc_msg_serialize_data] serialize_header_only failed\n");
    return NULL;
  }

  size_t total_size = *out_size + msg->header->len + 1;
  memcpy(serialized + *out_size, msg->payload, msg->header->len + 1);
  *out_size = total_size;
  return serialized;
}

static Result *create_msg_with_payload(const cbcast_msg_kind_t kind,
                                       const char *payload,
                                       const uint16_t payload_len);

static Result *create_msg_with_header(const cbcast_msg_kind_t kind) {
  cbcast_msg_hdr_t *hdr = calloc(1, sizeof(cbcast_msg_hdr_t));
  if (!hdr) {
    return result_new_err("[cbc_create_msg_header] Alloc failed");
  }

  cbcast_msg_t *message = calloc(1, sizeof(cbcast_msg_t));
  if (!message) {
    free(hdr);
    return result_new_err("[cbc_create_message] Alloc failed");
  }

  hdr->kind = kind;
  message->header = hdr;
  message->payload = NULL;
  return result_new_ok(message);
}

static Result *create_msg_with_payload(const cbcast_msg_kind_t kind,
                                       const char *payload,
                                       const uint16_t payload_len) {
  if (!payload || !payload_len) {
    return result_new_err("[cbc_create_message] Invalid header or payload");
  }

  Result *with_header_res = create_msg_with_header(kind);
  if (result_is_err(with_header_res)) {
    return with_header_res;
  }

  cbcast_msg_t *message = result_expect(with_header_res, "unfallible expect");
  message->header->len = payload_len;
  // +1 to include null term
  message->payload = calloc(payload_len + 1, sizeof(char));
  if (!message->payload) {
    cbc_msg_free(message);
    return result_new_err("[cbc_create_message] strdup failed");
  }

  memcpy(message->payload, payload, payload_len);
  message->payload[payload_len] = '\0'; // Ensure null termination
  return result_new_ok(message);
}
