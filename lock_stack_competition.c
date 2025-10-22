// --------------- Preprocessor directives ---------------------------------- //

// #define DEBUG

// --------------- Headers -------------------------------------------------- //

#include "lock_stack_competition.h"
#include <bits/pthreadtypes.h>
#include <bits/types/stack_t.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

// --------------- Constants ------------------------------------------------ //

#define DEFAULT_LEN 2048 // 2^13

// --------------- Structs -------------------------------------------------- //

struct lstack_t {
  pthread_mutex_t mutex;
  void **stack;
  atomic_int top;
  atomic_int bottom;
  atomic_int capacity;
};

// --------------- Declaration of internal functions ------------------------ //

bool stack_is_full(const lstack_t *stack);

static void stack_grow(lstack_t *stack);

void stack_print(lstack_t *stack);

// --------------- Definition of external functions ------------------------- //

lstack_t *stack_create(void) {
  lstack_t *stack = malloc(sizeof(lstack_t));

  pthread_mutex_init(&stack->mutex, NULL);
  stack->stack = calloc(DEFAULT_LEN, sizeof(void *));
  atomic_init(&stack->top, 0);
  atomic_init(&stack->bottom, 0);
  atomic_init(&stack->capacity, DEFAULT_LEN);

  return stack;
}

void stack_destroy(lstack_t *stack) {
  free(stack->stack);
  pthread_mutex_destroy(&stack->mutex);
  free(stack);
}

void stack_push(lstack_t *q, void *arg) {
  pthread_mutex_lock(&q->mutex);

#ifdef DEBUG
  fprintf(stderr, "pushing: %p\n", arg);
#endif /* ifdef DEBUG */

  if (q->top == q->capacity) { // full
    stack_grow(q);
  }

  q->stack[q->top++] = arg;
  pthread_mutex_unlock(&q->mutex);
}

void *stack_pop(lstack_t *q) {
  pthread_mutex_lock(&q->mutex);

  if (q->bottom == q->top) {
    pthread_mutex_unlock(&q->mutex);
    return NULL;
  }

  void *arg = q->stack[q->bottom++];

#ifdef DEBUG
  fprintf(stderr, "popping: %p\n", arg);
#endif /* ifdef DEBUG */

  pthread_mutex_unlock(&q->mutex);

  return arg;
}

bool stack_is_empty(lstack_t *q) {
  pthread_mutex_lock(&q->mutex);
  bool empty = q->top == q->bottom;
  pthread_mutex_unlock(&q->mutex);

  return empty;
}
// --------------- Definition of internal functions -------------------------

void stack_grow(lstack_t *stack) {
  stack->capacity *= 2;

  stack->stack = realloc(stack->stack, stack->capacity * sizeof(void *));
}
