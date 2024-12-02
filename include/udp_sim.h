#ifndef UDP_SIM_H
#define UDP_SIM_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Estrutura para representar um pacote UDP
typedef struct {
    char data[1024]; // Tamanho máximo do pacote
    int len;
    struct sockaddr_in addr;
} udp_packet_t;

// Funções da biblioteca
int udp_sim_init();
int udp_sim_create_socket(int *sockfd);
int udp_sim_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int udp_sim_sendto(int sockfd, const void *buf, size_t len, int flags,
                  const struct sockaddr *dest_addr, socklen_t addrlen);
int udp_sim_recvfrom(int sockfd, void *buf, size_t len, int flags,
                   struct sockaddr *src_addr, socklen_t *addrlen);
int udp_sim_close(int sockfd);

// Funções para configurar a simulação
void udp_sim_set_packet_loss_rate(double rate);
void udp_sim_set_delay_distribution(char *distribution); // Ex: normal, uniform

#endif