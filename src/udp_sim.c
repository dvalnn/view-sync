#include "udp_sim.h"

typedef struct {
  double packet_loss_rate;
  delay_dist_t delay_distribution;
} sim_state_t;

sim_state_t sim_state = {0};

int udp_sim_init(double packet_loss_rate, delay_dist_t delay_distribution) {
  sim_state = (sim_state_t){.packet_loss_rate = packet_loss_rate,
                            .delay_distribution = delay_distribution};
  return 0;
}

int udp_sim_create_socket(int *sockfd) {
  (void)sockfd;

  return 0;
}

int udp_sim_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  (void)sockfd;
  (void)addr;
  (void)addrlen;

  return 0;
}

int udp_sim_sendto(int sockfd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen) {
  (void)sockfd;
  (void)buf;
  (void)len;
  (void)flags;
  (void)dest_addr;
  (void)addrlen;

  return 0;
}

int udp_sim_recvfrom(int sockfd, void *buf, size_t len, int flags,
                     struct sockaddr *src_addr, socklen_t *addrlen) {
  (void)sockfd;
  (void)buf;
  (void)len;
  (void)flags;
  (void)src_addr;
  (void)addrlen;

  return 0;
}

int udp_sim_close(int sockfd) {
  (void)sockfd;

  return 0;
}

// sim config functions
void udp_sim_set_packet_loss_rate(double rate) { (void)rate; }

void udp_sim_set_delay_distribution(delay_dist_t dist) {
  (void)dist;
}
