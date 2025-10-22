/**
 * This program is a "du"-like program which utilzes threads.
 * It was implemeted for the mdu assignment in the course C Programming and Unix
 * (5DV088).
 *
 * @file mdu.c
 * @author Elias Svensson (c24esn@cs.umu.se)
 * @date 2025-10-10
 */

// --------------- Preprocessor directives ---------------------------------- //

// #define DEBUG

// --------------- Headers -------------------------------------------------- //

#include "thread_pool_competition.h"
#include <bits/getopt_core.h>
#include <dirent.h>
#include <ftw.h>
#include <linux/limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// --------------- Constants ------------------------------------------------ //

#define NR_DEFAULT_THREADS 1
#define MAX_NAME_COUNT 139108
#define MAX_NAME_LEN 200

// --------------- Structs -------------------------------------------------- //

/**
 * @typedef settings
 * @brief stores each cmdline option
 *
 */
typedef struct settings {
  int nr_threads;
  char **targets;
} settings;

/**
 * @typedef target_info
 * @brief stores all information to add a file size
 *
 */
typedef struct target_info {
  char *filename;
  atomic_int *sum;
  tpool_t *pool;
  bool *error;
} target_info;

// --------------- Declaration of internal functions ------------------------ //

int write_to_buf(char *filename);

void *sum_target_size(void *arg);

int add_dir(int idx);

target_info *tinfo_create(tpool_t *pool, char *filename, atomic_int *sum,
                          bool *error);

void tinfo_destroy(target_info *info);

settings *set_settings(int argc, char *argv[]);

void free_settings(settings *s);

void cleanup_and_exit(settings *opts, tpool_t *pool, int exit_code);

// --------------- Definitions of internal functions ------------------------ //

#ifdef DEBUG
atomic_int max_name_len;
char *max_name;
#endif /* ifdef DEBUG */

tpool_t *pool;
atomic_int sum;
char *name_buf;
atomic_int buf_head;

int main(int argc, char *argv[]) {
  int exit_code = EXIT_SUCCESS;

  settings *opts = set_settings(argc, argv);
  pool = tpool_create(opts->nr_threads, sum_target_size);
  atomic_init(&buf_head, 1);

#ifdef DEBUG
  atomic_init(&max_name_len, 0);
#endif /* ifdef DEBUG */

  name_buf = malloc(INT32_MAX);

  for (int i = 0; opts->targets[i] != NULL; i++) {
    atomic_init(&sum, 0);
    int head = write_to_buf(opts->targets[i]);

    tpool_add_work(pool, (void *)(intptr_t)head);

    tpool_wait(pool);

    printf("%d\t%s\n", atomic_load(&sum), opts->targets[i]);
  }

#ifdef DEBUG
  printf("\n\n----- STATS -----\n");
  printf("Buf head: %d\n", atomic_load(&buf_head));
  printf("Max name len: %d\n", atomic_load(&max_name_len));
  printf("Max name: %s\n", max_name);
#endif /* ifdef DEBUG */

  cleanup_and_exit(opts, pool, exit_code);
}

void *sum_target_size(void *arg) {
  int index = (int)(intptr_t)arg;
  char *filename = &name_buf[index];

#ifdef DEBUG
  fprintf(stderr, "sum file: %s\n", filename);
#endif /* ifdef DEBUG */

  struct stat filestat;
  if (lstat(filename, &filestat)) {
#ifdef DEBUG
    fprintf(stderr, "mdu: stat '%s': ", filename);
    perror(NULL);
    return (void *)1;
#endif /* ifdef DEBUG */
    return NULL;
  }

  atomic_fetch_add(&sum, filestat.st_blocks);

  if (S_ISDIR(filestat.st_mode)) {
    int status = add_dir(index);

    if (status) {
      // free(filename);
      return (void *)1;
    }
  }

  // free(filename);
  return NULL;
}

int add_dir(int idx) {
  char *filename = &name_buf[idx];
  DIR *dir = opendir(filename);
  if (dir == NULL) {
#ifdef DEBUG
    fprintf(stderr, "mdu: cannot open directory '%s': ", filename);
    perror(NULL);
#endif /* ifdef DEBUG */

    return 1;
  }
#ifdef DEBUG
  fprintf(stderr, "add_dir file: %s\n", filename);
#endif /* ifdef DEBUG */

  struct dirent *de;
  while ((de = readdir(dir)) != NULL) {
    if (strcmp("..", de->d_name) == 0 || strcmp(".", de->d_name) == 0) {
      continue; // dont count parent dir or current dir
    }
    char new_file[PATH_MAX]; // temporary buffer
    strcpy(new_file, filename);

    if (filename[strlen(filename) - 1] != '/')
      strcat(new_file, "/");

    strcat(new_file, de->d_name);

    // write to global buffer and add work
    int head = write_to_buf(new_file);

    // int base_len = strlen(filename);
    // int name_len = strlen(de->d_name);
    // int tot_len = name_len + base_len + 2;
    // char *new_file = malloc(tot_len * sizeof(char));
    //
    // memcpy(new_file, filename, base_len);
    //
    // if (filename[base_len - 1] != '/')
    //   new_file[base_len++] = '/';
    //
    // memcpy(new_file + base_len, de->d_name, name_len);
    // new_file[base_len + name_len] = '\0';

    if (de->d_type == DT_DIR) {
      tpool_add_work(pool, (void *)(intptr_t)head);
      continue;
    }

    struct stat filestat;
    lstat(new_file, &filestat);
    atomic_fetch_add(&sum, filestat.st_blocks);
  }

  closedir(dir);
  return 0;
}

int write_to_buf(char *filename) {
  int len = strlen(filename) + 1;
  int head = atomic_fetch_add(&buf_head, len);

  strcpy(&name_buf[head], filename);

  // memcpy(&name_buf[head], filename, len);
  //
  // name_buf[head + len] = '\0';

#ifdef DEBUG
  int max = atomic_load(&max_name_len);
  if (max < len) {
    atomic_store(&max_name_len, len);
    max_name = filename;
  }

#endif /* ifdef DEBUG                                                            \
    int max = atomic_load(&max_name_len);                                        \
    if (max < len) {                                                             \
      atomic_store(&max_name_len, len);                                          \
    }                                                                            \
                                                                               \ \
  */

  return head;
}

settings *set_settings(int argc, char *argv[]) {
  settings *opts = malloc(sizeof(settings));

  opts->nr_threads = NR_DEFAULT_THREADS;

  // set flags
  int opt;
  while ((opt = getopt(argc, argv, "j:")) != -1) {
    if (opt == 'j') {
      opts->nr_threads = atoi(optarg);
    } else {
      free(opts);
      return NULL;
    }
  }

  // set targets
  int len = argc - optind;
  if (len == 0) { // no targets given
    fprintf(stderr, "usage: %s [FILE]...\n", argv[0]);
    free(opts);
    return NULL;
  }

  opts->targets = calloc(len + 1, sizeof(char *));

  int k = 0;
  for (int i = optind; i < argc; i++) {
    opts->targets[k] = argv[i];

    k++;
  }
  opts->targets[len] = NULL; // make it null terminated to help with iteration

  return opts;
}

void free_settings(settings *s) {
  if (!s) {
    return;
  }

  if (s->targets) {
    free(s->targets);
  }

  free(s);
}

void cleanup_and_exit(settings *s, tpool_t *p, int exit_code) {
  free_settings(s);
  tpool_destroy(p);

  exit(exit_code);
}
