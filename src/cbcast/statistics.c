#include "cbcast.h"
#include "lib/stb_ds.h"

#include <cjson/cJSON.h>
#include <sys/param.h>
#include <time.h>

char *get_current_timestamp() {
  time_t now = time(NULL);
  struct tm *t = gmtime(&now);
  char *timestamp = malloc(25); // "YYYY-MM-DDTHH:MM:SSZ" format
  if (timestamp) {
    strftime(timestamp, 25, "%Y-%m-%dT%H:%M:%SZ", t);
  }
  return timestamp;
}

char *serialize_statistics_to_json(const uint64_t id,
                                   const cbcast_stats_t *stats) {
  // Create a new JSON object
  cJSON *json = cJSON_CreateObject();

  char *timestamp = get_current_timestamp();
  if (timestamp) {
    cJSON_AddStringToObject(json, "timestamp", timestamp);
    free(timestamp);
  }

  cJSON_AddNumberToObject(json, "worker", id);

  // Add scalar fields to the JSON object
  cJSON_AddNumberToObject(json, "recv_msg_count", stats->recv_msg_count);
  cJSON_AddNumberToObject(json, "sent_msg_count", stats->sent_msg_count);
  cJSON_AddNumberToObject(json, "sent_ack_count", stats->sent_ack_count);
  cJSON_AddNumberToObject(json, "sent_retransmit_req_count",
                          stats->sent_retransmit_req_count);
  cJSON_AddNumberToObject(json, "sent_retransmit_count",
                          stats->sent_retransmit_count);

  cJSON_AddNumberToObject(json, "dropped_msg_count", stats->dropped_msg_count);
  cJSON_AddNumberToObject(json, "dropped_ack_count", stats->dropped_ack_count);
  cJSON_AddNumberToObject(json, "dropped_retransmit_req_count",
                          stats->dropped_retransmit_req_count);
  cJSON_AddNumberToObject(json, "dropped_retransmit_count",
                          stats->dropped_retransmit_count);

  cJSON_AddNumberToObject(json, "delivered_msg_count",
                          stats->delivered_msg_count);
  cJSON_AddNumberToObject(json, "delivery_queue_size",
                          stats->delivery_queue_size);
  cJSON_AddNumberToObject(json, "delivery_queue_max_size",
                          stats->delivery_queue_max_size);

  cJSON_AddNumberToObject(json, "sent_msg_buffer_size",
                          stats->sent_msg_buffer_size);
  cJSON_AddNumberToObject(json, "sent_msg_buffer_max_size",
                          stats->sent_msg_buffer_max_size);

  cJSON_AddNumberToObject(json, "held_msg_buffer_size",
                          stats->held_msg_buffer_size);
  cJSON_AddNumberToObject(json, "held_msg_buffer_max_size",
                          stats->held_msg_buffer_max_size);

  // Add the vector clock as an array
  cJSON *vector_clock_array = cJSON_CreateArray();
  for (uint64_t i = 0; i < stats->num_peers; i++) {
    cJSON_AddItemToArray(vector_clock_array,
                         cJSON_CreateNumber(stats->vector_clock_snapshot[i]));
  }
  cJSON_AddItemToObject(json, "vector_clock_snapshot", vector_clock_array);

  /* cJSON_AddNumberToObject(json, "num_peers", stats->num_peers); */

  // Convert the JSON object to a string
  char *json_string = cJSON_Print(json);

  // Clean up the cJSON object
  cJSON_Delete(json);

  return json_string;
}

char *cbc_collect_statistics(cbcast_t *cbc) {
  pthread_mutex_lock(&cbc->send_lock);
  size_t sent_buffer_size = arrlen(cbc->sent_msg_buffer);
  pthread_mutex_unlock(&cbc->send_lock);

  pthread_mutex_lock(&cbc->recv_lock);
  size_t held_buffer_size = arrlen(cbc->held_msg_buffer);
  size_t delivery_queue_size = arrlen(cbc->delivery_queue);
  pthread_mutex_unlock(&cbc->recv_lock);

  uint64_t *vclock_snapshot = vc_snapshot(cbc->vclock);

  // Lock the statistics mutex
  pthread_mutex_lock(&cbc->stats_lock);
  cbc->stats->sent_msg_buffer_max_size =
      MAX(cbc->stats->sent_msg_buffer_max_size, sent_buffer_size);

  cbc->stats->held_msg_buffer_max_size =
      MAX(cbc->stats->held_msg_buffer_max_size, held_buffer_size);
  cbc->stats->delivery_queue_max_size =
      MAX(cbc->stats->delivery_queue_max_size, delivery_queue_size);

  // update buffer sizes
  cbc->stats->sent_msg_buffer_size = sent_buffer_size;
  cbc->stats->held_msg_buffer_size = held_buffer_size;
  cbc->stats->delivery_queue_size = delivery_queue_size;

  // update vector clock snapshot
  free(cbc->stats->vector_clock_snapshot); // free the old snapshot
  cbc->stats->vector_clock_snapshot = vclock_snapshot;
  // TODO: fix this 
  cbc->stats->num_peers = 4;

  char *json = serialize_statistics_to_json(cbc->pid, cbc->stats);
  pthread_mutex_unlock(&cbc->stats_lock);

  return json;
}
