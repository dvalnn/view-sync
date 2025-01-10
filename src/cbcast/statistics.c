#include "cbcast.h"
#include "lib/stb_ds.h"

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <sys/param.h>
#include <time.h>

// Helper function to get the current timestamp in nanoseconds
char *get_current_timestamp_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  char *timestamp = malloc(21); // Enough space for nanoseconds timestamp
  if (timestamp) {
    snprintf(timestamp, 21, "%ld%09ld", ts.tv_sec, ts.tv_nsec);
  }
  return timestamp;
}

// Serialize statistics to JSON format compatible with Loki
char *serialize_statistics_to_loki_json(const uint64_t id,
                                        const cbcast_stats_t *stats) {
  // Get the current timestamp
  char *timestamp_ns = get_current_timestamp_ns();
  if (!timestamp_ns) {
    fprintf(stderr, "Failed to allocate memory for timestamp.\n");
    return NULL;
  }

  // Create the JSON payload for Loki
  cJSON *root = cJSON_CreateObject();
  cJSON *streams = cJSON_AddArrayToObject(root, "streams");
  cJSON *stream = cJSON_CreateObject();
  cJSON *stream_labels = cJSON_CreateObject();
  cJSON *values = cJSON_CreateArray();

  // Set labels
  cJSON_AddStringToObject(stream_labels, "label", "cbcast_logs");
  cJSON_AddStringToObject(stream_labels, "host", "localhost");
  cJSON_AddStringToObject(stream_labels, "application", "cbc");
  cJSON_AddStringToObject(stream_labels, "level", "info");

  // Add labels to the stream
  cJSON_AddItemToObject(stream, "stream", stream_labels);

  // Serialize statistics as a log line
  cJSON *log_entry = cJSON_CreateArray();
  cJSON_AddItemToArray(log_entry, cJSON_CreateString(timestamp_ns));
  free(timestamp_ns);

  cJSON *log_message = cJSON_CreateObject();
  cJSON_AddNumberToObject(log_message, "worker", id);
  cJSON_AddNumberToObject(log_message, "recv_msg_count", stats->recv_msg_count);
  cJSON_AddNumberToObject(log_message, "sent_msg_count", stats->sent_msg_count);
  cJSON_AddNumberToObject(log_message, "sent_ack_count", stats->sent_ack_count);
  cJSON_AddNumberToObject(log_message, "sent_retransmit_req_count",
                          stats->sent_retransmit_req_count);
  cJSON_AddNumberToObject(log_message, "sent_retransmit_count",
                          stats->sent_retransmit_count);
  cJSON_AddNumberToObject(log_message, "dropped_msg_count",
                          stats->dropped_msg_count);
  cJSON_AddNumberToObject(log_message, "dropped_ack_count",
                          stats->dropped_ack_count);
  cJSON_AddNumberToObject(log_message, "dropped_retransmit_req_count",
                          stats->dropped_retransmit_req_count);
  cJSON_AddNumberToObject(log_message, "dropped_retransmit_count",
                          stats->dropped_retransmit_count);
  cJSON_AddNumberToObject(log_message, "delivered_msg_count",
                          stats->delivered_msg_count);
  cJSON_AddNumberToObject(log_message, "delivery_queue_size",
                          stats->delivery_queue_size);
  cJSON_AddNumberToObject(log_message, "delivery_queue_max_size",
                          stats->delivery_queue_max_size);
  cJSON_AddNumberToObject(log_message, "sent_msg_buffer_size",
                          stats->sent_msg_buffer_size);
  cJSON_AddNumberToObject(log_message, "sent_msg_buffer_max_size",
                          stats->sent_msg_buffer_max_size);
  cJSON_AddNumberToObject(log_message, "held_msg_buffer_size",
                          stats->held_msg_buffer_size);
  cJSON_AddNumberToObject(log_message, "held_msg_buffer_max_size",
                          stats->held_msg_buffer_max_size);

  cJSON *vector_clock = cJSON_CreateArray();
  for (uint64_t i = 0; i < stats->num_peers; i++) {
    cJSON_AddItemToArray(vector_clock,
                         cJSON_CreateNumber(stats->vector_clock_snapshot[i]));
  }
  cJSON_AddItemToObject(log_message, "vector_clock_snapshot", vector_clock);

  char *log_message_str = cJSON_PrintUnformatted(log_message);
  cJSON_AddItemToArray(log_entry, cJSON_CreateString(log_message_str));
  cJSON_AddItemToArray(values, log_entry);
  free(log_message_str);

  cJSON_AddItemToObject(stream, "values", values);
  cJSON_AddItemToArray(streams, stream);

  char *payload = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  return payload;
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

  char *json = serialize_statistics_to_loki_json(cbc->pid, cbc->stats);
  pthread_mutex_unlock(&cbc->stats_lock);

  return json;
}

// Send the log payload to Grafana Loki
void send_stats_to_loki(const char *json_payload, const char *loki_url) {
  CURL *curl = curl_easy_init();
  if (curl) {
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, loki_url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
  }
}
