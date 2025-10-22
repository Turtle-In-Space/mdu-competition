/**
 * This module is used to create a thread pool. A thread pool will create a
 * given amount of threads then execute functions given from tpool_add_work().
 * It was implemeted for the mdu assignment in the course C Programming and Unix
 * (5DV088).
 *
 * @file thread_pool.c
 * @author Elias Svensson (c24esn@cs.umu.se)
 * @date 2025-10-10
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

typedef struct worker_t {
  tpool_t *restrict pool;
  stack_t *restrict job_stack;
  sem_t new_job;
  short id;
} worker_t;

struct tpool_t {
  pthread_mutex_t access_pool;
  sem_t new_job;
  sem_t done;

  stack_t *restrict global_stack;
  worker_t **workers;
  pthread_t *restrict threads;
  atomic_int balance_queues;

  short nr_thrds;
  atomic_int nr_working_thrds;
  atomic_bool error;
  atomic_bool stop;

  void *(*func)(void *);
};

// --------------- Declaration of internal functions ------------------------ //

void *worker(void *arg);

void *tpool_steal_job(tpool_t *restrict pool, const short wid);

static bool tpool_no_jobs(const tpool_t *restrict pool);

static worker_t *worker_create(tpool_t *restrict pool, const short id);

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

  pthread_mutex_init(&pool->access_pool, NULL);
  sem_init(&pool->done, 0, 0);
  sem_init(&pool->new_job, 0, 0);
  atomic_init(&pool->error, false);
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
      sem_post(&pool->workers[i]->new_job);
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
  pthread_mutex_destroy(&pool->access_pool);

  free(pool);
}

void tpool_add_work(tpool_t *restrict pool, void *restrict arg) {
  const short worker =
      atomic_fetch_add(&pool->balance_queues, 1) % pool->nr_thrds;
  worker_t *w = pool->workers[worker];

  stack_push(w->job_stack, arg);
#ifdef DEBUG
  fprintf(stderr, "[*] added work to: %d\n", w->id);
#endif /* if DEBUG */

  sem_post(&pool->workers[worker]->new_job);
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
    void *job = NULL;

    if (stack_is_empty(w->job_stack)) {
      job = tpool_steal_job(p, w->id);

      if (!job) {
        sem_wait(&w->new_job);
      }
    }
    atomic_fetch_add(&p->nr_working_thrds, 1);

    if (atomic_load(&p->stop)) {
      break;
    }

    if (!job) {
#ifdef DEBUG
      fprintf(stderr, "[*] %d tries self queue\n", thread_id);
#endif /* ifdef DEBUG */
      job = stack_pop(w->job_stack);
    }

    //     if (!job) { // Try stealing from other workers
    //       job = tpool_steal_job(p, w->id);
    // #ifdef DEBUG
    //       fprintf(stderr, "[*] %d will now steal\n", thread_id);
    //       if (job) {
    //         atomic_fetch_add(&tot_stolen_jobs, 1);
    //       }
    // #endif /* ifdef DEBUG */
    //     }

    if (job) {
      void *status = p->func(job);
      if (status) {
        atomic_store(&p->error, true);
      }
#ifdef DEBUG
      fprintf(stderr, "[~] %d found work\n", thread_id);
      ++jobs_done;
      atomic_fetch_add(&tot_jobs, 1);
#endif /* ifdef DEBUG */
    } else {
#ifdef DEBUG
      fprintf(stderr, "[!] %d found no work\n", thread_id);
#endif /* ifdef */
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
  fprintf(stderr, "%d did %d jobs\n", thread_id, jobs_done);
#endif /* ifdef DEBUG */
  return NULL;
}

void *tpool_steal_job(tpool_t *restrict pool, const short wid) {
  short tid = atomic_load(&pool->balance_queues) % pool->nr_thrds;
  if (tid == wid) {
    tid = (tid + 1) % pool->nr_thrds;
  }

  worker_t *target = pool->workers[tid];
  // try to steal job from target
  return stack_pop(target->job_stack);
}

static bool tpool_no_jobs(const tpool_t *restrict pool) {
  // Check global queue
  if (!stack_is_empty(pool->global_stack)) {
    // fprintf(stderr, "glob");
    return false;
  }

  // Check worker stacks
  for (short i = 0; i < pool->nr_thrds; i++) {
    if (!stack_is_empty(pool->workers[i]->job_stack)) {
      // fprintf(stderr, "%d", i);
      sem_post(&pool->workers[i]->new_job);
      return false;
    }
  }

  return true;
}

static worker_t *worker_create(tpool_t *restrict pool, const short id) {
  worker_t *worker = malloc(sizeof(worker_t));

  sem_init(&worker->new_job, 0, 0);
  worker->pool = pool;
  worker->id = id;
  worker->job_stack = stack_create();

  return worker;
}

static void worker_destroy(worker_t *restrict w) {
  stack_destroy(w->job_stack);

  free(w);
}
