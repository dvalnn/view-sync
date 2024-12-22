#include <arpa/inet.h>
#include <stdlib.h>

#include "cbcast.h"
#include "lib/stb_ds.h"

Result *create_peer(const uint pid, const char *ipv4, const uint16_t port) {
  cbcast_peer_t *peer = calloc(1, sizeof(cbcast_peer_t));
  if (!peer) {
    return result_new_err("[cbc_add_peer] failed to allocate peer");
  }

  peer->pid = pid;

  peer->addr = calloc(1, sizeof(struct sockaddr_in));
  if (!peer->addr) {
    free(peer);
    return result_new_err("[cbc_add_peer] failed to allocate peer addr");
  }

  peer->addr->sin_family = AF_INET;
  peer->addr->sin_port = htons(port);
  peer->addr->sin_addr.s_addr = inet_addr(ipv4);
  if (peer->addr->sin_addr.s_addr == INADDR_NONE) {
    free(peer->addr);
    free(peer);
    return result_new_err("[cbc_add_peer] invalid ipv4 address");
  }

  return result_new_ok(peer);
}

// @brief Add a peer to the list of peers
// @param cbc The broadcast context
// @param pid The peer's ID
// @param ipv4 The peer's IPv4 address
// @param port The peer's port
// @return A result object indicating success or failure
//         If successful, the result's data is NULL
// @note The caller is responsible for freeing the result
//
Result *cbc_add_peer(cbcast_t *cbc, const uint64_t pid, const char *ipv4,
                     const uint16_t port) {

  if (!cbc || !ipv4 || !port) {
    return result_new_err("[cbc_add_peer] invalid arguments");
  }

  Result *peer_res = create_peer(pid, ipv4, port);
  if (result_is_err(peer_res)) {
    return peer_res;
  }

  cbcast_peer_t *peer = result_expect(peer_res, "unreachable");

  pthread_mutex_lock(&cbc->peer_lock);
  arrput(cbc->peers, peer);
  pthread_mutex_unlock(&cbc->peer_lock);

  return result_new_ok(NULL);
}

// @brief Remove a peer from the list of peers. Frees the removed peer.
// @param cbc The broadcast context
// @param pid The peer's ID
// @note If the peer is not found, the function does nothing
void cbc_remove_peer(cbcast_t *cbc, const uint64_t pid) {
  if (!cbc) {
    return;
  }

  pthread_mutex_lock(&cbc->peer_lock);
  for (int i = 0; i < arrlen(cbc->peers); i++) {
    if (cbc->peers[i]->pid == pid) {
      free(cbc->peers[i]->addr);
      free(cbc->peers[i]);
      arrdel(cbc->peers, i);
      break;
    }
  }
  pthread_mutex_unlock(&cbc->peer_lock);
}

// @brief Find a peer by its address
// @param cbc The broadcast context
// @param addr The peer's address
uint16_t cbc_peer_find_by_addr(cbcast_t *cbc, struct sockaddr_in *addr) {
  if (!cbc || !addr) {
    return UINT16_MAX;
  }

  uint16_t pid = UINT16_MAX;
  pthread_mutex_lock(&cbc->peer_lock);
  for (int i = 0; i < arrlen(cbc->peers); i++) {
    if (memcmp(cbc->peers[i]->addr, addr, sizeof(struct sockaddr_in)) == 0) {
      pid = cbc->peers[i]->pid;
    }
  }
  pthread_mutex_unlock(&cbc->peer_lock);

  return pid;
}

// @brief Get a copy of a peer's address
// @param cbc The broadcast context
// @param pid The peer's ID
// @return A copy of the peer's address, or NULL if the peer is not found
struct sockaddr_in *cbc_peer_get_addr_copy(cbcast_t *cbc, const uint64_t pid) {
  if (!cbc) {
    return NULL;
  }

  char found = 0;
  struct sockaddr_in *addr_copy = calloc(1, sizeof(struct sockaddr_in));
  pthread_mutex_lock(&cbc->peer_lock);
  for (int i = 0; i < arrlen(cbc->peers); i++) {
    if (cbc->peers[i]->pid == pid) {
      found = 1;
      memcpy(addr_copy, cbc->peers[i]->addr, sizeof(struct sockaddr_in));
      break;
    }
  }
  pthread_mutex_unlock(&cbc->peer_lock);

  if (!found) {
    free(addr_copy);
    return NULL;
  }

  return addr_copy;
}
