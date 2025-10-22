#include <pthread.h>
#include <stdbool.h>

typedef struct lstack_t lstack_t;

lstack_t *stack_create(void);

void stack_destroy(lstack_t *stack);

void stack_push(lstack_t *restrict stack, void *restrict elem);

void *stack_pop(lstack_t *stack);

bool stack_is_empty(lstack_t *stack);

void stack_print(lstack_t *stack);
