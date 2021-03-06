#include <signal.h>
#include <assert.h>
#include "sr_nat.h"
#include <unistd.h>
#include <time.h>

int sr_nat_init(struct sr_nat *nat) { /* Initializes the nat */

  assert(nat);

  /* Acquire mutex lock */
  pthread_mutexattr_init(&(nat->attr));
  pthread_mutexattr_settype(&(nat->attr), PTHREAD_MUTEX_RECURSIVE);
  int success = pthread_mutex_init(&(nat->lock), &(nat->attr));

  /* Initialize timeout thread */

  pthread_attr_init(&(nat->thread_attr));
  pthread_attr_setdetachstate(&(nat->thread_attr), PTHREAD_CREATE_JOINABLE);
  pthread_attr_setscope(&(nat->thread_attr), PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setscope(&(nat->thread_attr), PTHREAD_SCOPE_SYSTEM);
  pthread_create(&(nat->thread), &(nat->thread_attr), sr_nat_timeout, nat);

  /* CAREFUL MODIFYING CODE ABOVE THIS LINE! */

  nat->mappings = NULL;
  /* Initialize any variables here */

  return success;
}


int sr_nat_destroy(struct sr_nat *nat) {  /* Destroys the nat (free memory) */

  pthread_mutex_lock(&(nat->lock));

  /* free nat memory here */

  pthread_kill(nat->thread, SIGKILL);
  return pthread_mutex_destroy(&(nat->lock)) &&
    pthread_mutexattr_destroy(&(nat->attr));

}

void *sr_nat_timeout(void *nat_ptr) {  /* Periodic Timout handling */
  struct sr_nat *nat = (struct sr_nat *) nat_ptr;

  while (1) {
    sleep(1.0);
    pthread_mutex_lock(&(nat->lock));

    time_t curtime = time(NULL);

    /* handle periodic tasks here */
    struct sr_nat_mapping *currMapping, *nextMapping;
    currMapping = nat->mappings;

    while (currMapping != NULL) {
      nextMapping = currMapping->next;

      if (currMapping->type == nat_mapping_icmp) { /* ICMP */
        if (difftime(curtime, currMapping->last_updated) > nat->icmp_query_timeout) {
          destroy_nat_mapping(nat, currMapping);
        }
      } else if (currMapping->type == nat_mapping_tcp) { /* TCP */
        check_tcp_conns(nat, currMapping);
        if (currMapping->conns == NULL && difftime(curtime, currMapping->last_updated) > 0.5) {
          destroy_nat_mapping(nat, currMapping);
        }
      }
      currMapping = nextMapping;
    }
    pthread_mutex_unlock(&(nat->lock));
  }
  return NULL;
}

/* Get the mapping associated with given external port.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_external(struct sr_nat *nat,
  uint16_t aux_ext, sr_nat_mapping_type type ) {

  pthread_mutex_lock(&(nat->lock));

  /* handle lookup here, malloc and assign to copy */
  /* struct sr_nat_mapping *copy = NULL; */
  struct sr_nat_mapping *currMapping, *foundMapping = NULL;
  currMapping = nat->mappings;

  while (currMapping != NULL) {
    if (currMapping->type == type && currMapping->aux_ext == aux_ext) {
      foundMapping = currMapping;
      break;
    }
    currMapping = currMapping->next;
  }

  pthread_mutex_unlock(&(nat->lock));
  return foundMapping;
}

/* Get the mapping associated with given internal (ip, port) pair.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_internal(struct sr_nat *nat,
  uint32_t ip_int, uint16_t aux_int, sr_nat_mapping_type type ) {

  pthread_mutex_lock(&(nat->lock));

  /* handle lookup here, malloc and assign to copy. */
  /* struct sr_nat_mapping *copy = NULL; */
  struct sr_nat_mapping *currMapping, *foundMapping = NULL;
  currMapping = nat->mappings;

  while (currMapping != NULL) {
    if (currMapping->type == type && currMapping->aux_int == aux_int && currMapping->ip_int == ip_int) {
      foundMapping = currMapping;
      break;
    }
    currMapping = currMapping->next;
  }

  pthread_mutex_unlock(&(nat->lock));
  return foundMapping;
}

/* Insert a new mapping into the nat's mapping table.
   Actually returns a copy to the new mapping, for thread safety.
 */
struct sr_nat_mapping *sr_nat_insert_mapping(struct sr_nat *nat,
  uint32_t ip_int, uint16_t aux_int, sr_nat_mapping_type type ) {

  pthread_mutex_lock(&(nat->lock));

  /* handle insert here, create a mapping, and then return a copy of it */
  struct sr_nat_mapping *newMapping = malloc(sizeof(struct sr_nat_mapping)); 
  assert(newMapping != NULL);

  newMapping->type = type;
  newMapping->last_updated = time(NULL);
  newMapping->ip_int = ip_int;
  newMapping->aux_int = aux_int;
  newMapping->conns = NULL;

  struct sr_nat_mapping *currMapping = nat->mappings;
  nat->mappings = newMapping;
  newMapping->next = currMapping;

  pthread_mutex_unlock(&(nat->lock));
  return newMapping;
}

int sr_nat_is_iface_internal(char *iface) {
  return strcmp(iface, NAT_INTERNAL_IFACE) == 0 ? 1 : 0;
}

/* Generate a unique port in context of being available in O(n) complexity. */
int generate_unique_port(struct sr_nat *nat) {

  pthread_mutex_lock(&(nat->lock));

  uint16_t *available_ports = nat->available_ports;
  int i;

  for (i = MIN_PORT; i <= TOTAL_PORTS; i++) {
    if (available_ports[i] == 0) {
      available_ports[i] = 1;
      printf("Allocated port: %d\n", i);

      pthread_mutex_unlock(&(nat->lock));
      return i;
    }
  }

  pthread_mutex_unlock(&(nat->lock));
  return -1;
}

/* Generate a unique icmp identifier in context of being available in O(n) complexity. */
int generate_unique_icmp_identifier(struct sr_nat *nat) {

  pthread_mutex_lock(&(nat->lock));

  uint16_t *available_icmp_identifiers = nat->available_icmp_identifiers;
  int i;

  for (i = MIN_ICMP_IDENTIFIER; i <= TOTAL_ICMP_IDENTIFIERS; i++) {
    if (available_icmp_identifiers[i] == 0) {
      available_icmp_identifiers[i] = 1;
      printf("Allocated ICMP identifier: %d\n", i);

      pthread_mutex_unlock(&(nat->lock));
      return i;
    }
  }

  pthread_mutex_unlock(&(nat->lock));
  return -1;
}

/* Get the connection associated with the given IP in the NAT entry. */
struct sr_nat_connection *sr_nat_lookup_tcp_con(struct sr_nat_mapping *mapping, uint32_t ip_con) {
  struct sr_nat_connection *currConn = mapping->conns;

  while (currConn != NULL) {
    if (currConn->ip == ip_con) {
      return currConn;
    }
    currConn = currConn->next;
  }

  return NULL;
}

/* Insert a new connection associated with the given IP in the NAT entry. */
struct sr_nat_connection *sr_nat_insert_tcp_con(struct sr_nat_mapping *mapping, uint32_t ip_con) {
  struct sr_nat_connection *newConn = malloc(sizeof(struct sr_nat_connection));
  assert(newConn != NULL);
  memset(newConn, 0, sizeof(struct sr_nat_connection));

  newConn->last_updated = time(NULL);
  newConn->ip = ip_con;
  newConn->tcp_state = CLOSED;

  struct sr_nat_connection *currConn = mapping->conns;

  mapping->conns = newConn;
  newConn->next = currConn;

  return newConn;
}

void check_tcp_conns(struct sr_nat *nat, struct sr_nat_mapping *nat_mapping) {
  struct sr_nat_connection *currConn, *nextConn;
  time_t curtime = time(NULL);

  currConn = nat_mapping->conns;

  while (currConn != NULL) {
    nextConn = currConn->next;
    /* print_tcp_state(currConn->tcp_state); */

    if (currConn->tcp_state == ESTABLISHED) {
      if (difftime(curtime, currConn->last_updated) > nat->tcp_estb_timeout) {
        destroy_tcp_conn(nat_mapping, currConn);
      }
    } else {
      if (difftime(curtime, currConn->last_updated) > nat->tcp_trns_timeout) {
        destroy_tcp_conn(nat_mapping, currConn);
      }
    }

    currConn = nextConn;
  }
}

void destroy_tcp_conn(struct sr_nat_mapping *mapping, struct sr_nat_connection *conn) {
  printf("[REMOVE] TCP connection\n");
  struct sr_nat_connection *prevConn = mapping->conns;

  if (prevConn != NULL) {
    if (prevConn == conn) {
      mapping->conns = conn->next;
    } else {
      for (; prevConn->next != NULL && prevConn->next != conn; prevConn = prevConn->next) {}
        if (prevConn == NULL) { return; }
      prevConn->next = conn->next;
    }
    free(conn);
  }
}

void destroy_nat_mapping(struct sr_nat *nat, struct sr_nat_mapping *nat_mapping) {
  printf("[REMOVE] nat mapping\n");

  struct sr_nat_mapping *prevMapping = nat->mappings;

  if (prevMapping != NULL) {
    if (prevMapping == nat_mapping) {
      nat->mappings = nat_mapping->next;
    } else {
      for (; prevMapping->next != NULL && prevMapping->next != nat_mapping; prevMapping = prevMapping->next) {}
        if (prevMapping == NULL) {return;}
      prevMapping->next = nat_mapping->next;
    }

    if (nat_mapping->type == nat_mapping_icmp) { /* ICMP */
      nat->available_icmp_identifiers[nat_mapping->aux_ext] = 0;
    } else if (nat_mapping->type == nat_mapping_tcp) { /* TCP */
      nat->available_ports[nat_mapping->aux_ext] = 0;
    }

    struct sr_nat_connection *currConn, *nextConn;
    currConn = nat_mapping->conns;

    while (currConn != NULL) {
      nextConn = currConn->next;
      free(currConn);
      currConn = nextConn;
    }
    free(nat_mapping);
  }
}

