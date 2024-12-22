#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cbcast.h"
#include "lib/stb_ds.h"

#define NUM_WORKERS 5
#define BASE_PORT 12345
#define SHM_NAME "/cbc_sync"

static volatile int running = 1;

void sigint_handler(int sig) {
  (void)sig;
  running = 0;
}
void worker_process(cbcast_t *cbc, volatile int *sync_state) {
  printf("Worker %lu starting (PID: %d)\n", cbc->pid, getpid());

  // Step 1: Notify setup completion
  sync_state[cbc->pid] = 1;

  // Step 2: Wait for all workers to complete setup
  while (running) {
    int all_ready = 1;
    for (int i = 0; i < NUM_WORKERS; i++) {
      if (sync_state[i] == 0) {
        all_ready = 0;
        break;
      }
    }
    if (all_ready)
      break;
    usleep(10000); // Sleep for 10ms to avoid busy-waiting
  }

  printf("Worker %lu starting broadcast phase.\n", cbc->pid);
  result_unwrap(cbc_start(cbc));

  int received_counter = 0;
  static int counter = 0;

  while (running) {
    cbcast_received_msg_t *received_msg = cbc_receive(cbc);
    if (received_msg) {
      printf("[main] Worker %lu received: \"%s\" %d from peer %d\n", cbc->pid,
             received_msg->message->payload, ++received_counter,
             received_msg->sender_pid);
      cbc_received_msg_free(received_msg);
    }

    if (++counter % 10 == 0) {
      char message[64];
      snprintf(message, sizeof(message), "Hello from worker %lu!", cbc->pid);
      result_expect(cbc_send(cbc, message, strlen(message)),
                    "[main] Failed to broadcast message");
    }

    usleep(250000); // Simulate processing delay
  }

  // Step 3: Wait for turn to print state
  while ((uint64_t)sync_state[NUM_WORKERS] != cbc->pid) {
    usleep(10000); // Sleep for 10ms
  }

  // Step 4: Print state
  printf("Worker %lu shutting down.\n", cbc->pid);
  printf("Worker %lu final state:\n"
         "  - sent messages: %lu\n"
         "  - delivery queue length: %td\n"
         "  - hold buffer length: %td\n"
         "  - sent buffer length: %td\n",
         cbc->pid, cbc->vclock->clock[cbc->pid], arrlen(cbc->delivery_queue),
         arrlen(cbc->held_msg_buffer), arrlen(cbc->sent_msg_buffer));

  // Dump delivery queue state
  printf("Worker %lu delivery queue:\n", cbc->pid);
  for (size_t i = 0; i < (size_t)arrlen(cbc->delivery_queue); i++) {
    cbcast_received_msg_t *msg = cbc->delivery_queue[i];
    printf("  - %lu: \"%s\" clock %d from peer %d\n", i, msg->message->payload,
           msg->message->header->clock, msg->sender_pid);
  }

  // Dump held buffer state
  printf("Worker %lu held buffer:\n", cbc->pid);
  for (size_t i = 0; i < (size_t)arrlen(cbc->held_msg_buffer); i++) {
    cbcast_received_msg_t *msg = cbc->held_msg_buffer[i];
    printf("  - %lu: \"%s\" clock %d from peer %d\n", i, msg->message->payload,
           msg->message->header->clock, msg->sender_pid);
  }

  // Dump sent buffer state
  printf("Worker %lu sent buffer:\n", cbc->pid);
  for (size_t i = 0; i < (size_t)arrlen(cbc->sent_msg_buffer); i++) {
    cbcast_sent_msg_t *msg = cbc->sent_msg_buffer[i];
    printf("  - %lu: \"%s\" clock %d ack target %lu ack state %lu\n", i,
           msg->message->payload, msg->message->header->clock, msg->ack_target,
           msg->ack_bitmap);
  }

  // Step 5: Pass control to the next worker
  sync_state[NUM_WORKERS] = (sync_state[NUM_WORKERS] + 1) % NUM_WORKERS;

  cbc_stop(cbc);
  cbc_free(cbc);
  exit(0);
}
int main() {
  signal(SIGINT, sigint_handler); // Handle Ctrl+C to stop workers

  pid_t pids[NUM_WORKERS] = {0};
  int shm_fd = -1;
  volatile int *sync_state = NULL;

  // Step 1: Create shared memory for synchronization
  shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
  if (shm_fd < 0) {
    perror("shm_open");
    exit(EXIT_FAILURE);
  }

  if (ftruncate(shm_fd, (NUM_WORKERS + 1) * sizeof(int)) < 0) {
    perror("ftruncate");
    shm_unlink(SHM_NAME);
    exit(EXIT_FAILURE);
  }

  sync_state = mmap(NULL, (NUM_WORKERS + 1) * sizeof(int),
                    PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (sync_state == MAP_FAILED) {
    perror("mmap");
    shm_unlink(SHM_NAME);
    exit(EXIT_FAILURE);
  }

  memset((void *)sync_state, 0, (NUM_WORKERS + 1) * sizeof(int));
  sync_state[NUM_WORKERS] = 0; // Initialize the first worker allowed to log

  // Step 2: Fork worker processes
  for (uint64_t i = 0; i < NUM_WORKERS; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      uint16_t port = BASE_PORT + i;
      Result *result = cbc_init(i, NUM_WORKERS, port);
      if (!result_is_ok(result)) {
        fprintf(stderr, "Failed to initialize cbcast for worker %lu\n", i);
        exit(EXIT_FAILURE);
      }

      cbcast_t *cbc = result_unwrap(result);

      for (uint64_t j = 0; j < NUM_WORKERS; j++) {
        if (j == i)
          continue;
        result_unwrap(cbc_add_peer(cbc, j, "127.0.0.1", BASE_PORT + j));
      }

      worker_process(cbc, sync_state);
      return EXIT_SUCCESS;
    } else if (pid > 0) {
      pids[i] = pid;
    } else {
      perror("fork");
      shm_unlink(SHM_NAME);
      exit(EXIT_FAILURE);
    }
  }

  // Parent process waits for termination
  printf("Press Ctrl+C to stop workers...\n");
  while (running) {
    pause();
  }

  printf("Terminating workers...\n");
  for (int i = 0; i < NUM_WORKERS; i++) {
    if (pids[i] > 0) {
      kill(pids[i], SIGINT);
      waitpid(pids[i], NULL, 0);
    }
  }

  munmap((void *)sync_state, (NUM_WORKERS + 1) * sizeof(int));
  shm_unlink(SHM_NAME);

  printf("All workers terminated.\n");
  return 0;
}
