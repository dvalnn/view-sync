#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: ./view_sync <n_child_proc>\n");
    return -1;
  }

  // Convert argument to integer
  int n_child_proc = atoi(argv[1]);
  if (n_child_proc <= 0) {
    printf("Error: Number of child processes must be a positive integer.\n");
    return -1;
  }

  printf("[Parent] Spawning %d children\n", n_child_proc);
  for (int i = 0; i < n_child_proc; i++) {
    pid_t pid = fork();
    if (pid == -1) {
      perror("[Parent] fork failed");
      return -1;
    }
    if (pid == 0) {
      // Child process
      printf("[Child %d] Hello world!\n", getpid());
      return 0; // Exit child process
    } else {
      // Parent process
      printf("[Parent] Created child with PID %d\n", pid);
    }
  }

  // Parent waits for all child processes
  for (int i = 0; i < n_child_proc; i++) {
    int status;
    pid_t child_pid = wait(&status);
    if (child_pid == -1) {
      perror("[Parent] wait failed");
      return -1;
    }
    if (WIFEXITED(status)) {
      printf("[Parent] Child %d exited with status %d\n", child_pid,
             WEXITSTATUS(status));
    } else {
      printf("[Parent] Child %d terminated abnormally\n", child_pid);
    }
  }

  return 0;
}
