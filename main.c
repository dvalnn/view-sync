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

// Worker function for broadcasting and receiving messages
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

  int cycle = 0;
  int received_counter = 0;
  while (running) {
    /* printf("----\nWorker %lu cycle %d start.\n", cbc->pid, ++cycle); */

    cbcast_received_msg_t *received_msg = cbc_receive(cbc);
    if (received_msg) {
      printf("[main] Worker %lu received: \"%s\" %d from peer %d\n", cbc->pid,
             received_msg->message->payload, received_counter,
             received_msg->sender_pid);
      cbc_received_message_free(received_msg);
    }

    // Broadcast a message periodically
    static int counter = 0;
    if (++counter % 10 == 0) {
      char message[64];
      snprintf(message, sizeof(message), "Hello from worker %lu!", cbc->pid);
      result_expect(cbc_send(cbc, message, strlen(message)),
                    "[main] Failed to broadcast message");
    }

    printf("Worker %lu cycle %d summary:\n"
           "DelivQ\tSize: %td\n"
           "HeldQ\tSize: %td\n"
           "SentQ\tSize: %td\n"
           "----\n",
           cbc->pid, ++cycle, arrlen(cbc->delivery_queue), arrlen(cbc->held_buf),
           arrlen(cbc->sent_buf));

    usleep(250000); // Sleep to simulate processing
  }

  printf("Worker %lu shutting down.\n", cbc->pid);
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

  if (ftruncate(shm_fd, NUM_WORKERS * sizeof(int)) < 0) {
    perror("ftruncate");
    shm_unlink(SHM_NAME);
    exit(EXIT_FAILURE);
  }

  sync_state = mmap(NULL, NUM_WORKERS * sizeof(int), PROT_READ | PROT_WRITE,
                    MAP_SHARED, shm_fd, 0);
  if (sync_state == MAP_FAILED) {
    perror("mmap");
    shm_unlink(SHM_NAME);
    exit(EXIT_FAILURE);
  }

  memset((void *)sync_state, 0,
         NUM_WORKERS * sizeof(int)); // Initialize sync state

  // Step 2: Fork worker processes
  for (uint64_t i = 0; i < NUM_WORKERS; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      // Child process: Initialize cbcast instance
      uint16_t port = BASE_PORT + i;
      Result *result = cbc_init(i, NUM_WORKERS, port);
      if (!result_is_ok(result)) {
        fprintf(stderr, "Failed to initialize cbcast for worker %lu\n", i);
        exit(EXIT_FAILURE);
      }

      cbcast_t *cbc = result_unwrap(result);

      // Add peers to this cbcast instance
      for (uint64_t j = 0; j < NUM_WORKERS; j++) {
        if (j == i)
          continue; // Skip self
        result_unwrap(cbc_add_peer(cbc, j, "127.0.0.1", BASE_PORT + j));
      }

      worker_process(cbc, sync_state); // Start the worker process
      return EXIT_SUCCESS;

    } else if (pid > 0) {
      pids[i] = pid; // Store child PID for later cleanup
    } else {
      perror("fork");
      shm_unlink(SHM_NAME);
      exit(EXIT_FAILURE);
    }
  }

  // Step 3: Parent process waits for termination
  printf("Press Ctrl+C to stop workers...\n");
  while (running) {
    pause(); // Wait for a signal
  }

  printf("Terminating workers...\n");
  for (int i = 0; i < NUM_WORKERS; i++) {
    if (pids[i] > 0) {
      kill(pids[i], SIGINT);     // Send SIGINT to each child
      waitpid(pids[i], NULL, 0); // Wait for each child to exit
    }
  }

  // Cleanup shared memory
  munmap((void *)sync_state, NUM_WORKERS * sizeof(int));
  shm_unlink(SHM_NAME);

  printf("All workers terminated.\n");
  return 0;
}
