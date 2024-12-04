#ifndef UDP_SIM_H
#define UDP_SIM_H

#include "netinet/in.h"

typedef enum {
  DL_NORMAL,
  DL_UNIFORM,
} delay_dist_t;

typedef struct {
  char data[1024]; // Max packet size
  int len;
  struct sockaddr_in addr;
} udp_packet_t;

// lib functions
int udp_sim_init(double packet_loss_rate, delay_dist_t delay_distribution);

int udp_sim_create_socket(int *sockfd);
int udp_sim_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int udp_sim_sendto(int sockfd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen);

int udp_sim_recvfrom(int sockfd, void *buf, size_t len, int flags,
                     struct sockaddr *src_addr, socklen_t *addrlen);

int udp_sim_close(int sockfd);

// sim config functions
void udp_sim_set_packet_loss_rate(double rate);
void udp_sim_set_delay_distribution(delay_dist_t dist); // Ex: normal, uniform

#endif
