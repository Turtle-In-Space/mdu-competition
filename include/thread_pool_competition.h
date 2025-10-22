/**
 * This module is used to create a thread pool. A thread pool will create a
 * given amount of threads then execute functions given from tpool_add_work().
 * It was implemeted for the mdu assignment in the course C Programming and Unix
 * (5DV088).
 *
 * @file thread_pool.h
 * @author Elias Svensson (c24esn@cs.umu.se)
 * @date 2025-10-10
 */

#ifndef __THREAD_POOL_H
#define __THREAD_POOL_H

#include <pthread.h>

// --------------- Structs -------------------------------------------------- //

/**
 * @typedef tpool_t
 * @brief a pool of threads. Will complete all work added though
 * tpool_add_work()
 *
 */
typedef struct tpool_t tpool_t;

// --------------- Declaration of external functions ------------------------ //

/**
 * @brief Initlize a thread pool and allocate memory for it. Memory allocated
 * needs to be freed by calling tpool_destroy()
 *
 * @param nr_threads      the amount of threads to start
 * @return                a pointer to a struct of type tpool_t. Null if there
 * was an error
 */
tpool_t *tpool_create(const short nr_threads, void *(*func)(void *));

/**
 * @brief Deallocate all memory for a thread pool.
 *
 * @param pool      a pointer to a struct of type tpool_t
 */
void tpool_destroy(tpool_t *pool);

/**
 * @brief Add work to a thread pool. The FUNC will be called with ARG by a
 * thread when available. A non-null return value will be treated as an error.
 *
 * @param pool      a pointer to a struct of type tpool_t
 * @param func      a pointer to a function
 * @param arg       a pointer to a argument
 */
void tpool_add_work(tpool_t *restrict pool, void *restrict arg);

/**
 * @brief Wait for all work inside a thread pool to complete
 *
 * @param pool       a pointer to a struct of type tpool_t
 */
void tpool_wait(tpool_t *pool);

#endif // !__THREAD_POOL_H
