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

#define DEBUG

// --------------- Headers -------------------------------------------------- //

#include "thread_pool_competition.h"
#include <bits/getopt_core.h>
#include <dirent.h>
#include <fcntl.h>
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
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

// --------------- Constants ------------------------------------------------ //

#define NR_DEFAULT_THREADS 1
#define MAX_NAME_LEN 70
#define BUF_LEN 2048
#define NO_PAR_FD -31

// --------------- Structs -------------------------------------------------- //

typedef struct linux_dirent64 {
  int64_t d_ino;  /* 64-bit inode number */
  int64_t d_off;  /* Not an offset; see getdents() */
  short d_reclen; /* Size of this dirent */
  char d_type;    /* File type */
  char d_name[];  /* Filename (null-terminated) */
} linux_dirent64;

/**
 * @typedef settings
 * @brief stores each cmdline option
 *
 */
typedef struct settings {
  short nr_threads;
  char **targets;
} settings;

typedef struct target_info {
  int fd;
  char name[MAX_NAME_LEN];
  atomic_int dependencies;
  struct target_info *parent;
} target_info;

// --------------- Declaration of internal functions ------------------------ //

void *sum_target_size(void *arg);

void add_dir(target_info *info);

target_info *tinfo_create(target_info *parent, char *const restrict name);

void tinfo_destroy(target_info *info);

static settings *set_settings(const short argc, char *argv[]);

static void free_settings(settings *s);

static void cleanup_and_exit(settings *restrict opts, tpool_t *restrict pool,
                             const short exit_code);

// --------------- Definitions of internal functions ------------------------ //

#ifdef DEBUG
atomic_int max_name_len;
char *max_name;
#endif /* ifdef DEBUG */

tpool_t *pool;
atomic_int sum;

int main(int argc, char *argv[]) {
  bool exit_code = EXIT_SUCCESS;

  settings *opts = set_settings(argc, argv);
  pool = tpool_create(opts->nr_threads, sum_target_size);

#ifdef DEBUG
  atomic_init(&max_name_len, 0);
#endif /* ifdef DEBUG */

  for (short i = 0; opts->targets[i] != NULL; i++) {
    atomic_init(&sum, 0);

    target_info *info = tinfo_create(NULL, opts->targets[i]);
    tpool_add_work(pool, info);

    tpool_wait(pool);

    printf("%d\t%s\n", atomic_load(&sum), opts->targets[i]);
  }

  cleanup_and_exit(opts, pool, exit_code);
}

void *sum_target_size(void *arg) {
  target_info *info = (target_info *)arg;
  char *const filename = info->name;
  int parent_fd;
  if (!info->parent) {
    parent_fd = AT_FDCWD;
  } else {
    parent_fd = info->parent->fd;
  }

#ifdef DEBUG
  fprintf(stderr, "sum file: %s\n", filename);
  fprintf(stderr, "parent fd: %d\n", parent_fd);
#endif /* ifdef DEBUG */

  struct stat filestat;
  if (fstatat(parent_fd, filename, &filestat, AT_SYMLINK_NOFOLLOW)) {
#ifdef DEBUG
    fprintf(stderr, "\tmdu: stat '%s': ", filename);
    perror(NULL);
    return (void *)1;
#endif /* ifdef DEBUG */
    return NULL;
  }

  atomic_fetch_add(&sum, filestat.st_blocks);

  if (S_ISDIR(filestat.st_mode)) {
    info->fd = openat(parent_fd, filename, O_RDONLY | O_DIRECTORY);
    if (info->fd < 0) {
      perror("openat");
      return NULL;
    }

    add_dir(info);
  }

  tinfo_destroy(info);
  return NULL;
}

void add_dir(target_info *info) {
  int fd = info->fd;

  char buf[8192];
  short nread;
#ifdef DEBUG
  fprintf(stderr, "\tadd dir for: %s with fd: %d\n", info->name, info->fd);
#endif /* ifdef DEBUG */

  while ((nread = syscall(SYS_getdents64, fd, buf, sizeof(buf))) > 0) {
    for (register int bpos = 0; bpos < nread;) {
      struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
      bpos += d->d_reclen;

      if (d->d_type != DT_DIR) {
        struct stat filestat;
        fstatat(fd, d->d_name, &filestat, AT_SYMLINK_NOFOLLOW);
        atomic_fetch_add(&sum, filestat.st_blocks);
        continue; // dont add files to jobs
      }

      if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) {
        continue; // skip current and parent directory
      }

#ifdef DEBUG
      fprintf(stderr, "\tcreating '%s' with parent fd: %d\n", d->d_name, fd);
#endif /* ifdef DEBUG */
      target_info *new_info = tinfo_create(info, d->d_name);

      tpool_add_work(pool, new_info);
    }
  }

  if (info->parent) {
    tinfo_destroy(info->parent);
  }
  return;
}

target_info *tinfo_create(target_info *parent, char *const restrict name) {
  target_info *info = malloc(sizeof(target_info));
  info->parent = parent;
  info->fd = NO_PAR_FD;
  atomic_init(&info->dependencies, 1);

  strncpy(info->name, name, sizeof(info->name) - 1);
  if (parent) {
    atomic_fetch_add(&parent->dependencies, 1);
  }

#ifdef DEBUG
  if ((int)strlen(name) > atomic_load(&max_name_len)) {
    max_name_len = strlen(name);
    atomic_store(&max_name_len, strlen(name));
    max_name = strdup(name);
  }

#endif /* ifdef DEBUG */

  return info;
}

void tinfo_destroy(target_info *info) {

  if (atomic_fetch_sub(&info->dependencies, 1) == 1) {

#ifdef DEBUG
    fprintf(stderr, "destroing '%s' with fd: %d\n", info->name, info->fd);

#endif /* ifdef DEBUG */

    if (info->fd >= 0) {
      close(info->fd);
    }
    free(info);
  }
}

static settings *set_settings(short argc, char *argv[]) {
  settings *opts = malloc(sizeof(settings));

  opts->nr_threads = NR_DEFAULT_THREADS;

  // set flags
  short opt;
  while ((opt = getopt(argc, argv, "j:")) != -1) {
    if (opt == 'j') {
      opts->nr_threads = atoi(optarg);
    } else {
      free(opts);
      return NULL;
    }
  }

  // set targets
  const short len = argc - optind;
  if (len == 0) { // no targets given
    fprintf(stderr, "usage: %s [FILE]...\n", argv[0]);
    free(opts);
    return NULL;
  }

  opts->targets = calloc(len + 1, sizeof(char *));

  short k = 0;
  for (short i = optind; i < argc; i++) {
    opts->targets[k] = argv[i];

    k++;
  }
  opts->targets[len] = NULL; // make it null terminated to help with iteration

  return opts;
}

static void free_settings(settings *s) {
  if (!s) {
    return;
  }

  if (s->targets) {
    free(s->targets);
  }

  free(s);
}

static void cleanup_and_exit(settings *restrict s, tpool_t *restrict p,
                             const short exit_code) {
  free_settings(s);
  tpool_destroy(p);

#ifdef DEBUG
  printf("\n\n----- STATS -----\n");
  printf("Max name len: %d\n", atomic_load(&max_name_len));
  printf("Max name: %s\n", max_name);
#endif /* ifdef DEBUG */

  exit(exit_code);
}
