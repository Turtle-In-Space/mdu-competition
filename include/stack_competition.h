
// a thread safe stack

typedef struct stack_t stack_t;

stack_t *stack_create(void);

void stack_destroy(stack_t *stack);

void stack_push(stack_t *stack, void *arg);

void *stack_pop(stack_t *stack);

int stack_is_empty(stack_t *stack);
