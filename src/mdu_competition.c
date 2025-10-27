/**
 * This program is a "du"-like program which utilzes threads. It is
 * optimized for larger directories on a linux system. It was implemeted for the
 * mdu competition in the course C Programming and Unix (5DV088).
 *
 * @file mdu_competition.c
 * @author Elias Svensson (c24esn@cs.umu.se)
 * @date 2025-10-20
 */

// --------------- Preprocessor directives ---------------------------------- //

// #define DEBUG

// --------------- Headers -------------------------------------------------- //

#include "thread_pool_competition.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

// --------------- Constants ------------------------------------------------ //

#define NR_DEFAULT_THREADS 1
#define MAX_NAME_LEN 350
#define DIR_BUF_SIZE 1024

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
  short nr_threads; /* Amount of threads to use */
  char **targets;   /* A list of files to count blocksize of */
} settings;

// --------------- Declaration of internal functions ------------------------ //

void *count_dir(void *arg);

/**
 * @brief Appends two filenames into the aboslute path for f2. The memory
 * allocated needs to be freed by the caller
 *
 * @param f1      The base name of the file
 * @param f2      The file to be appended
 *
 * @return        A pointer to the full name
 */
static inline char *append_filename(const char *restrict f1,
                                    const char *restrict f2);

/**
 * @brief Parses the cmd line args and sores them in a struct. If targets were
 * given the memory allocated for settings.targets needs to be freed by
 * calling free_settings()
 *
 * @param argc    nr of args
 * @param argv    an array of args as strings
 *
 * @return        a structre of type settings containg all options. Null if
 * there was an issue
 */
static settings *set_settings(const short argc, char *argv[]);

/**
 * @brief Frees all memory allocted by set_settings
 *
 * @param s     the settings to be freed
 */
static void free_settings(settings *s);

/**
 * @brief Free the given structures and exit with EXIT_CODE
 *
 * @param opts          a pointer to a struct of settings
 * @param pool          a pointer to a struct of tpool_t
 * @param exit_code     the exit code to use
 */
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
  settings *opts = set_settings(argc, argv);
  pool = tpool_create(opts->nr_threads, count_dir);

#ifdef DEBUG
  atomic_init(&max_name_len, 0);
#endif /* ifdef DEBUG */

  for (short i = 0; opts->targets[i] != NULL; i++) {
    atomic_init(&sum, 0);

    struct stat filestat;
    lstat(opts->targets[i], &filestat);
    atomic_fetch_add(&sum, filestat.st_blocks);

    tpool_add_work(pool, strdup(opts->targets[i]));

    tpool_wait(pool);

    printf("%d\t%s\n", atomic_load(&sum), opts->targets[i]);
  }

#ifdef DEBUG
  printf("\n\n----- STATS -----\n");
  printf("Max name len: %d\n", atomic_load(&max_name_len));
  printf("Max name: %s\n", max_name);
#endif /* ifdef DEBUG */

  // TODO: remove cleanup? use -
  // _exit(EXIT_SUCCESS);
  cleanup_and_exit(opts, pool, EXIT_SUCCESS);
}

void *count_dir(void *arg) {
  char *filename = (char *)arg;
  const short fd = open(filename, O_RDONLY | O_DIRECTORY | O_NONBLOCK);
  if (fd < 0) {
    free(filename);
    return NULL;
  }

  char buf[DIR_BUF_SIZE];
  short nread;

  while ((nread = syscall(SYS_getdents64, fd, buf, sizeof(buf))) > 0) {
    for (register short bpos = 0; bpos < nread;) {
      struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
      bpos += d->d_reclen;

      if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) {
        continue; // skip current and parent directory
      }

      struct stat filestat;
      fstatat(fd, d->d_name, &filestat, AT_SYMLINK_NOFOLLOW);
      atomic_fetch_add(&sum, filestat.st_blocks);

#ifdef DEBUG
      fprintf(stderr, "sum file: %s\n", d->d_name);
#endif /* ifdef DEBUG */

      if (d->d_type != DT_DIR) {
        continue; // dont add files to jobs
      }

      tpool_add_work(pool, append_filename(filename, d->d_name));
    }
  }

  free(filename);
  close(fd);
  return NULL;
}

static inline char *append_filename(const char *restrict f1,
                                    const char *restrict f2) {
  short base_len = strlen(f1);
  short name_len = strlen(f2);
  short tot_len = name_len + base_len + 2;
  char *new_file = malloc(tot_len * sizeof(char));

  memcpy(new_file, f1, base_len);

  if (f1[base_len - 1] != '/')
    new_file[base_len++] = '/';

  memcpy(new_file + base_len, f2, name_len);
  new_file[base_len + name_len] = '\0';

#ifdef DEBUG
  if (name_len > atomic_load(&max_name_len)) {
    atomic_store(&max_name_len, name_len);
    max_name = strdup(f2);
  }
#endif /* ifdef DEBUG */

  return new_file;
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

  exit(exit_code);
}
