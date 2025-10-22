// --------------- Preprocessor directives ---------------------------------- //

// #define DEBUG

// --------------- Headers -------------------------------------------------- //

#include "stack_competition.h"
#include <stdatomic.h>
#include <stdlib.h>

// --------------- Structs -------------------------------------------------- //

typedef struct node_t {
  void *val;
  _Atomic(struct node_t *) next;
} node_t;

struct stack_t {
  _Atomic(node_t *) head;
  // tail to pop from?
};

// --------------- Declaration of internal functions ------------------------ //

node_t *node_create(void *val, node_t *next);

void node_destroy(node_t *node);

// --------------- Definition of external functions ------------------------- //

#ifdef DEBUG
thread_local int len = 0;
thread_local int max_len = 0;
#endif /* ifdef DEBUG */

stack_t *stack_create(void) {
  stack_t *t = malloc(sizeof(stack_t));
  atomic_init(&t->head, NULL);

  return t;
}

void stack_destroy(stack_t *s) {
  if (!s) {
    return;
  }

  while (!stack_is_empty(s)) {
    node_destroy(stack_pop(s));
  }

  free(s);
}

void stack_push(stack_t *s, void *arg) {
  node_t *new_head = node_create(arg, NULL);
  node_t *old_head;

  do {
    old_head = atomic_load_explicit(&s->head, memory_order_acquire);
    new_head->next = old_head;
  } while (!atomic_compare_exchange_weak_explicit(&s->head, &old_head, new_head,
                                                  memory_order_release,
                                                  memory_order_relaxed));

#ifdef DEBUG
  ++len;
#endif /* ifdef DEBUG */
}

void *stack_pop(stack_t *s) {
  node_t *new_head;
  node_t *old_head;

  do {
    old_head = atomic_load_explicit(&s->head, memory_order_acquire);
    if (!old_head) {
      return NULL;
    }
    new_head = old_head->next;
  } while (!atomic_compare_exchange_weak_explicit(&s->head, &old_head, new_head,
                                                  memory_order_release,
                                                  memory_order_relaxed));

#ifdef DEBUG
  if (len > max_len) {
    max_len = len;
    fprintf(stderr, "[*] stack: id: %lu\tmax: %d\n", pthread_self(), max_len);
  }
  --len;
#endif /* ifdef DEBUG */

  void *val = old_head->val;
  free(old_head);
  return val;
}

int stack_is_empty(stack_t *s) {
  return atomic_load_explicit(&s->head, memory_order_acquire) == NULL;
}

// --------------- Definition of internal functions ------------------------- //

node_t *node_create(void *val, node_t *next) {
  node_t *n = malloc(sizeof(node_t));

  n->val = val;
  n->next = next;

  return n;
}

void node_destroy(node_t *n) { free(n); }
