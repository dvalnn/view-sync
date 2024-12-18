#include "cbcast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ************** Function Declaration ***************
//
static char *cbc_msg_serialize_heartbeat(const cbcast_msg_t *msg,
                                         size_t *out_size);

static char *cbc_msg_serialize_data(const cbcast_msg_t *msg, size_t *out_size);

Result *cbc_msg_create_header(cbcast_msg_kind_t kind, uint16_t len) {
  cbcast_msg_hdr_t *hdr = calloc(1, sizeof(cbcast_msg_hdr_t));
  if (!hdr) {
    return result_new_err("[cbc_create_msg_header] Alloc failed");
  }
  hdr->kind = kind;
  hdr->len = len;
  return result_new_ok(hdr);
}

// ************** Public Functions ***************
//
Result *cbc_msg_create(cbcast_msg_hdr_t *header, char *payload) {
  if (!header || !payload) {
    return result_new_err("[cbc_create_message] Invalid arguments");
  }

  cbcast_msg_t *message = calloc(1, sizeof(cbcast_msg_t));
  if (!message) {
    return result_new_err("[cbc_create_message] Alloc failed");
  }

  message->header = header;
  if (header->kind == CBC_HEARTBEAT) {
    message->header->len = 0;
    return result_new_ok(message);
  }

  message->payload = calloc(strlen(payload) + 1, sizeof(char));
  if (!message->payload) {
    return result_new_err("[cbc_create_message] Payload alloc failed");
  }

  memcpy(message->payload, payload, strlen(payload) + 1);

  return result_new_ok(message);
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

  case CBC_HEARTBEAT:
    return cbc_msg_serialize_heartbeat(msg, out_size);
  case CBC_RETRANSMIT:
    RESULT_UNIMPLEMENTED;
    return NULL;
  case CBC_DATA:
    return cbc_msg_serialize_data(msg, out_size);
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

  if (msg->header->kind == CBC_HEARTBEAT) {
    return result_new_ok(msg); // hearbeat has no payload
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
  printf("[cbc_msg_deserialize] Deserialized message: kind=%d, "
         "clock=%d, len=%d, payload=\"%s\"\n",
         msg->header->kind, msg->header->clock, msg->header->len, msg->payload);

  return result_new_ok(msg);
}

// ************** Private Functions ***************
//
static char *cbc_msg_serialize_heartbeat(const cbcast_msg_t *msg,
                                         size_t *out_size) {
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

static char *cbc_msg_serialize_data(const cbcast_msg_t *msg, size_t *out_size) {
  if (!msg->header->len || !msg->payload) {
    fprintf(stderr, "[cbc_msg_serialize_data] Invalid message\n");
    return NULL;
  }

  // Calculate total size of the serialized message
  size_t total_size = sizeof(cbcast_msg_hdr_t) + msg->header->len +
                      1; // Include null terminator
  char *serialized = calloc(total_size, sizeof(char));
  if (!serialized) {
    fprintf(stderr, "[cbc_msg_serialize_data] Memory allocation failed\n");
    return NULL;
  }

  // Copy the header and payload into the serialized buffer
  memcpy(serialized, msg->header, sizeof(cbcast_msg_hdr_t));
  memcpy(serialized + sizeof(cbcast_msg_hdr_t), msg->payload,
         msg->header->len + 1);

  // Debug output
  printf("[cbc_msg_serialize_data] Serialized message: total_size=%zu, "
         "header_size=%zu, payload_size=%d, clock=%d\n",
         total_size, sizeof(cbcast_msg_hdr_t), msg->header->len + 1,
         msg->header->clock);

  *out_size = total_size;
  return serialized;
}
