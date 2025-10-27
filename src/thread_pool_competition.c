/**
 * This module is used to create a thread pool. A thread pool will create a
 * given amount of threads then execute functions given from tpool_add_work().
 * It was implemeted for the mdu competition in the course C Programming and
 * Unix (5DV088).
 *
 * @file thread_pool_competition.c
 * @author Elias Svensson (c24esn@cs.umu.se)
 * @date 2025-10-23
 */

// --------------- Preprocessor directives ---------------------------------- //

// #define DEBUG

// --------------- Headers -------------------------------------------------- //

#include "thread_pool_competition.h"
#include "stack_competition.h"
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <threads.h>

// --------------- Structs -------------------------------------------------- //

/**
 * @typedef worker_t
 * @brief Local information to a thread
 *
 */
typedef struct worker_t {
  tpool_t *restrict pool;
  stack_t *restrict job_stack;
  short id;
} worker_t;

struct tpool_t {
  sem_t new_job;
  sem_t done;

  stack_t *global_stack;
  worker_t **workers;
  pthread_t *threads;
  atomic_int balance_queues;

  short nr_thrds;
  atomic_int nr_working_thrds;
  atomic_bool stop;

  void *(*func)(void *);
};

// --------------- Declaration of internal functions ------------------------ //

/**
 * @brief Worker function for threads
 *
 * @param arg       a pointer to a struct of type worker_t
 */
void *worker(void *arg);

/**
 * @brief Try to steal a job from a worker. Will try all workers queues
 *
 * @param pool      a pointer to a struct of type pool_t
 * @param wid       the id of the worker trying to steal
 */
static void *tpool_steal_job(tpool_t *restrict pool, const short wid);

/**
 * @brief See if there are any jobs left
 *
 * @param pool   a pointer to a struct of type pool_t
 *
 * @return      True if there are no jobs in any queue for POOL. Else false
 */
static bool tpool_no_jobs(tpool_t *restrict pool);

/**
 * @brief Allocate the memory required for a worker and init all variables. The
 * memory allocated needs to be freed by calling worker_destroy()
 *
 * @param     pool a pointer to a struct of type pool_t
 * @param     id a unique id for this worker
 *
 * @return    a pointer to a struct of type worker_t
 */
static worker_t *worker_create(tpool_t *restrict pool, const short id);

/**
 * @brief Deallocate all memory allocated for a worker
 *
 * @param worker    a pointer to a struct of type worker_t
 */
static void worker_destroy(worker_t *restrict worker);

// --------------- Thread local vars ---------------------------------------- //

thread_local short thread_id = -1;

#ifdef DEBUG
#include <stdio.h>
thread_local short jobs_done = 0;
atomic_int tot_jobs;
atomic_int tot_stolen_jobs;
#endif /* ifdef DEBUG */

// --------------- Definition of external functions ------------------------- //

tpool_t *tpool_create(const short nr_threads, void *(*func)(void *)) {
  tpool_t *pool = malloc(sizeof(tpool_t));

  sem_init(&pool->done, 0, 0);
  sem_init(&pool->new_job, 0, 0);
  atomic_init(&pool->stop, false);
  atomic_init(&pool->nr_working_thrds, 0);
  atomic_init(&pool->balance_queues, 0);
  pool->nr_thrds = nr_threads;
  pool->func = func;

#ifdef DEBUG
  atomic_init(&tot_jobs, 0);
  atomic_init(&tot_stolen_jobs, 0);
#endif /* ifdef DEBUG */

  pool->global_stack = stack_create();
  pool->workers = calloc(nr_threads, sizeof(worker_t *));
  pool->threads = calloc(nr_threads, sizeof(pthread_t));

  for (short i = 0; i < nr_threads; i++) {
    pool->workers[i] = worker_create(pool, i);
    pthread_create(&pool->threads[i], NULL, worker, pool->workers[i]);
  }

  return pool;
}

void tpool_destroy(tpool_t *pool) {

#ifdef DEBUG
  fprintf(stderr, "\n\n------ STATS ------\n\n");
  fprintf(stderr, "Number of jobs: %d\n", atomic_load(&tot_jobs));
  fprintf(stderr, "Number of stolen jobs: %d\n", atomic_load(&tot_stolen_jobs));
  fprintf(stderr, "\n");
#endif /* ifdef DEBUG */

  if (!pool) {
    return;
  }

  if (pool->threads) {
    atomic_store(&pool->stop, true);

    // wake all threads
    for (short i = 0; i < pool->nr_thrds; i++) {
      sem_post(&pool->new_job);
    }

    // kill threads
    for (short i = 0; i < pool->nr_thrds; i++) {
      pthread_join(pool->threads[i], NULL);
      worker_destroy(pool->workers[i]);
    }

    free(pool->workers);
    free(pool->threads);
    stack_destroy(pool->global_stack);
  }

  sem_destroy(&pool->done);
  sem_destroy(&pool->new_job);

  free(pool);
}

void tpool_add_work(tpool_t *restrict pool, void *restrict arg) {
  if (thread_id != -1) {
    stack_push(pool->workers[thread_id]->job_stack, arg);
  } else {
    stack_push(pool->global_stack, arg);
  }

  sem_post(&pool->new_job);
}

void tpool_wait(tpool_t *restrict pool) {
#ifdef DEBUG
  fprintf(stderr, "[*] waiting...\n");
#endif /* ifdef DEBUG */
  sem_wait(&pool->done);
#ifdef DEBUG
  fprintf(stderr, "[~] done\n");
#endif /* ifdef DEBUG */

  return;
}

// --------------- Definition of internal functions ------------------------- //

void *worker(void *arg) {
  worker_t *w = (worker_t *)arg;
  tpool_t *p = w->pool;
  thread_id = w->id;

  while (!atomic_load(&p->stop)) {
#ifdef DEBUG
    int tmp;
    sem_getvalue(&p->new_job, &tmp);
    fprintf(stderr, "[~] tpool_worker: %d waiting with %d jobs\n", thread_id,
            tmp);
#endif /* ifdef DEBUG */
    sem_wait(&p->new_job);

    if (atomic_load(&p->stop)) {
      break;
    }

    atomic_fetch_add(&p->nr_working_thrds, 1);

    void *job = stack_pop(w->job_stack);

    if (!job) {
      job = tpool_steal_job(p, w->id);
    }

    if (!job) {
      job = stack_pop(p->global_stack);
    }

    if (job) {
      p->func(job);

#ifdef DEBUG
      atomic_fetch_add(&tot_jobs, 1);
#endif /* ifdef DEBUG */
    }

    atomic_fetch_sub(&p->nr_working_thrds, 1);

    // Only signal done if no worker is active and all stacks empty
    if (atomic_load(&p->nr_working_thrds) == 0 && tpool_no_jobs(p)) {
#ifdef DEBUG
      fprintf(stderr, "[~] all work done\n");
#endif /* ifdef DEBUG */
      sem_post(&p->done);
    }
  }

#ifdef DEBUG
  // fprintf(stderr, "%d did %d jobs\n", thread_id, jobs_done);
#endif /* ifdef DEBUG */
  return NULL;
}

void *tpool_steal_job(tpool_t *restrict pool, const short wid) {
  void *job = NULL;

  for (short i = 0; i < pool->nr_thrds; i++) {
    // offset with wid to not have all threads steal from 0
    short target = (i + wid) % pool->nr_thrds;
    job = stack_pop(pool->workers[target]->job_stack);
    if (job) {
#ifdef DEBUG
      atomic_fetch_add(&tot_stolen_jobs, 1);
#endif /* ifdef DEBUG */
      break;
    }
  }

  return job;
}

static bool tpool_no_jobs(tpool_t *restrict pool) {
  // Check global queue
  if (!stack_is_empty(pool->global_stack)) {
    // fprintf(stderr, "glob");
    return false;
  }

  // Check worker stacks
  for (short i = 0; i < pool->nr_thrds; i++) {
    if (!stack_is_empty(pool->workers[i]->job_stack)) {
      // fprintf(stderr, "%d", i);
      sem_post(&pool->new_job);
      return false;
    }
  }

  return true;
}

static worker_t *worker_create(tpool_t *restrict pool, const short id) {
  worker_t *worker = malloc(sizeof(worker_t));

  worker->pool = pool;
  worker->id = id;
  worker->job_stack = stack_create();

  return worker;
}

static void worker_destroy(worker_t *restrict w) {
  stack_destroy(w->job_stack);

  free(w);
}
