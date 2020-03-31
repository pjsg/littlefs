
/* The follow defines control details of how the fuzzer can exercise the API. If you
 * undef any of these, then the fuzzer is less brutal. 
 */
#define HAVE_MULTIPLE_OPEN

#define _BSD_SOURCE

#include "lfs.h"
#include "bd/lfs_rambd.h"
#include "bd/lfs_testbd.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <setjmp.h>

static int run_fuzz_test(FILE *f, int maxfds, char *skipitems, powerfail_behavior_t);

#if 0
// test stuff
static void test_assert(const char *file, unsigned line,
        const char *s, uintmax_t v, uintmax_t e) {{
    if (v != e) {{
        fprintf(stderr, "\033[97m%s:%u: \033[91m"
                "assert failed with %jd, expected %jd\033[0m\n"
                "    %s\n\n", file, line, v, e, s);
        exit(-2);
    }}
}}

#define test_assert(v, e) \
        test_assert(__FILE__, __LINE__, #v " => " #e, v, e)
#endif

// implicit variable for asserts
uintmax_t test;

// utility functions for traversals
static int __attribute__((used)) test_count(void *p, lfs_block_t b) {{
    (void)b;
    unsigned *u = (unsigned*)p;
    *u += 1;
    return 0;
}}

// lfs declarations
lfs_t lfs;
lfs_testbd_t bd;
// other declarations for convenience
lfs_file_t file;
lfs_dir_t dir;

int debuglog;
int no_open_remove;

#if 0
#define LFS_READ_SIZE 8
#define LFS_BLOCK_SIZE 4096
#define LFS_BLOCK_COUNT 128
#define LFS_CACHE_SIZE 256
#endif

// test configuration options
#ifndef LFS_READ_SIZE
#define LFS_READ_SIZE 16
#endif

#ifndef LFS_PROG_SIZE
#define LFS_PROG_SIZE LFS_READ_SIZE
#endif

#ifndef LFS_BLOCK_SIZE
#define LFS_BLOCK_SIZE 512
#endif

#ifndef LFS_BLOCK_COUNT
#define LFS_BLOCK_COUNT 1024
#endif

#ifndef LFS_BLOCK_CYCLES
#define LFS_BLOCK_CYCLES 1024
#endif

#ifndef LFS_CACHE_SIZE
#define LFS_CACHE_SIZE (64 % LFS_PROG_SIZE == 0 ? 64 : LFS_PROG_SIZE)
#endif

#ifndef LFS_LOOKAHEAD_SIZE
#define LFS_LOOKAHEAD_SIZE 16
#endif




const struct lfs_config cfg = {
    .context = &bd,
    .read  = &lfs_testbd_read,
    .prog  = &lfs_testbd_prog,
    .erase = &lfs_testbd_erase,
    .sync  = &lfs_testbd_sync,

    .read_size      = LFS_READ_SIZE,
    .prog_size      = LFS_PROG_SIZE,
    .block_size     = LFS_BLOCK_SIZE,
    .block_count    = LFS_BLOCK_COUNT,
    .block_cycles   = LFS_BLOCK_CYCLES,
    .cache_size     = LFS_CACHE_SIZE,
    .lookahead_size = LFS_LOOKAHEAD_SIZE,
};

#define CHECK_ERR      if (err == LFS_ERR_CORRUPT) assert(err != LFS_ERR_CORRUPT)
#define CHECK_RC(call) { err = call; LOGOP(" -> %d\n", err); if (err == LFS_ERR_CORRUPT) assert(err != LFS_ERR_CORRUPT); }
#define MUST_WORK(call) { err = call; LOGOP(" -> %d\n", err); if (err < 0) { printf("**** " #call " must work and it failed\n"); abort(); }}
#define LOGOP if (check_duration() || debuglog) printf

static uint32_t last_read = 0;
static uint32_t last_prog = 0;
static uint32_t last_erase = 0;

int break_suffix = -1;
FILE *toml = NULL;

static void dump_disk_suffix(int suffix) {
  char fname[256];
  sprintf(fname, "/tmp/littlefs-disk-%d", suffix);

  FILE *f = fopen(suffix ? fname : "/tmp/littlefs-disk", "wb");

  uint8_t *rbuffer = malloc(cfg.block_size);

  for (uint32_t i = 0; i < cfg.block_count; i++) {
    lfs_testbd_read(&cfg, i, 0, rbuffer, cfg.block_size);
    fwrite(rbuffer, cfg.block_size, 1, f);
    last_read += cfg.block_size;
  }

  fclose(f);
  free(rbuffer);

  if (break_suffix == suffix) {
    __asm__("int $3");
  }
}

static void dump_disk(int info) {
  (void) info;
  dump_disk_suffix(0);
}

static void closetoml(int info) {
  (void) info;
  fprintf(toml, ";\n'''\n");
  fclose(toml);
}

struct timeval last;

static int check_duration(void) {
  struct timeval now;
  gettimeofday(&now, 0);

  int duration = (now.tv_sec - last.tv_sec) * 1000000 + now.tv_usec - last.tv_usec;
  last = now;

  if (bd.stats.read_count != last_read || bd.stats.prog_count != last_prog || bd.stats.erase_count != last_erase) {
    if (debuglog) {
      printf("{r%d,p%d,e%d}", bd.stats.read_count - last_read, bd.stats.prog_count - last_prog,
        bd.stats.erase_count - last_erase);
    }
    last_read = bd.stats.read_count;
    last_prog = bd.stats.prog_count;
    last_erase = bd.stats.erase_count;
  }

  if (duration > 1000) {
    if (debuglog) {
      printf("[^ %d us]", duration);
    }
  }

  return 0;
}

// entry point
int main(int argc, char**argv) {
  int opt;

  powerfail_behavior_t powerfail_behavior = 1;

  char skipitems[256];
  memset(skipitems, 0, sizeof(skipitems));
  char *emit_toml = NULL;

  while ((opt = getopt(argc, argv, "pRn:t:rs")) > 0) {
    switch (opt) {
      case 's':
        powerfail_behavior = 0;
        break;
      case 'r':
        powerfail_behavior = 1;
        break;
      case 'p':
        debuglog = 1;
        break;
      case 'R':
        no_open_remove = 1;
        break;
      case 'n':
        for (const char *skip = optarg; *skip; skip++) {
          skipitems[*skip & 255] = 2;
        }
        break;
      case 't':
        emit_toml = optarg;
        break;
    }
  }

  if (debuglog) {
    lfs_testbd_create(&cfg, "/tmp/littlefs-live-disk");
  } else {
    lfs_testbd_create(&cfg, NULL);
  }

  gettimeofday(&last, 0);

  int err;

  LOGOP("format/mount");
  lfs_format(&lfs, &cfg);
  MUST_WORK(lfs_mount(&lfs, &cfg));
#if 0

  // read current count
  uint32_t boot_count = 0;
  lfs_file_open(&lfs, &file, "boot_count", LFS_O_RDWR | LFS_O_CREAT);
  lfs_file_read(&lfs, &file, &boot_count, sizeof(boot_count));

  // update boot count
  boot_count += 1;
  lfs_file_rewind(&lfs, &file);
  lfs_file_write(&lfs, &file, &boot_count, sizeof(boot_count));

  // remember the storage is not updated until the file is closed successfully
  lfs_file_close(&lfs, &file);

  // release any resources we were using
  lfs_unmount(&lfs);

  // print the boot count
  printf("boot_count: %d\n", boot_count);
#endif
  if (debuglog) {
    signal(SIGABRT, dump_disk);
  }

  if (emit_toml) {
    toml = fopen(emit_toml, "w");
    if (!toml) {
      fprintf(stderr, "Failed to open %s for write\n", emit_toml);
      exit(1);
    }
    fprintf(toml, "[[case]]\ncode = '''\n");
    signal(SIGABRT, closetoml);
  }
  run_fuzz_test(stdin, 4, skipitems, powerfail_behavior);
  if (toml) {
    fprintf(toml, ";\n'''\n");
    fclose(toml);
  }
}

static jmp_buf hook_abort;

#define DUMP_CHANGED {  \
    if (debuglog && last_prog_erase != bd.stats.prog_count + bd.stats.erase_count) { \
      last_prog_erase = bd.stats.prog_count + bd.stats.erase_count; \
      printf("{d%d} ", command_count); \
      dump_disk_suffix(command_count); \
      command_count++; \
    } \
}

static int run_fuzz_test(FILE *f, int maxfds, char *skipitems, powerfail_behavior_t powerfail_behavior_init) {
  // There are a bunch of arbitrary constants in this test case. Changing them will
  // almost certainly change the effets of an input file. It *may* be worth
  // making some of these constants to come from the input file.
#if 0
  int setup = fgetc(f);

  int page_size = 128 << (setup & 3);
  setup >>= 2;
  int erase_size = 4096 << (setup & 3);
  setup >>= 2;
  int block_size = erase_size << (setup & 1);
  setup >>= 1;
  int blocks = 4 + (setup & 7);
  fs_reset_specific(0, 0, blocks * block_size, erase_size, block_size, page_size);
  int res;
  (FS)->fd_count = 4;
#endif

#define FS &lfs

#define DO(x) if (toml) { fprintf(toml, "%s\n", #x); } x;

  int c;

  DO(
  lfs_file_t fd[4];
  uint32_t i;
  )

  powerfail_behavior_t powerfail_behavior = powerfail_behavior_init;
  memset(fd, -1, sizeof(fd));
  int openindex[4];
  memset(openindex, -1, sizeof(openindex));

  char *filename[8];

  for (i = 0; i < 8; i++) {
    char buff[128];
    snprintf(buff, sizeof(buff), "%dfile%d.xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxasdasdasdadxxxxxxxxxxxxxxxxxxx", i, i);
    buff[9 + 2 * i] = 0;
    filename[i] = strdup(buff);
  }

  // The list of 8 modes that are chosen. SPIFFS_EXCL is not present -- it probably ought to be.
  int modes[8] = {LFS_O_RDONLY, LFS_O_RDWR, LFS_O_RDWR|LFS_O_TRUNC, LFS_O_RDWR|LFS_O_CREAT, LFS_O_RDWR|LFS_O_CREAT|LFS_O_TRUNC,
      LFS_O_WRONLY|LFS_O_CREAT|LFS_O_TRUNC, LFS_O_RDWR|LFS_O_CREAT|LFS_O_TRUNC, LFS_O_WRONLY};

  DO(
  char buff[2048];
  for (i = 0; i < sizeof(buff); i++) {
    buff[i] = i * 19;
  }
  jmp_buf powerfail;
  (void) powerfail;
  )

  int powerfail_index = 0;

#define EMIT(...) if (toml) fprintf(toml, __VA_ARGS__)

  if (toml) {
    if (0) {
    EMIT("struct lfs_config ncfg = cfg;\n");
    EMIT("#define cfg ncfg\n");

    EMIT("cfg.read_size      = %d;\n", LFS_READ_SIZE);
    EMIT("cfg.prog_size      = %d;\n", LFS_PROG_SIZE);
    EMIT("cfg.block_size     = %d;\n", LFS_BLOCK_SIZE);
    EMIT("cfg.block_count    = %d;\n", LFS_BLOCK_COUNT);
    EMIT("cfg.block_cycles   = %d;\n", LFS_BLOCK_CYCLES);
    EMIT("cfg.cache_size     = %d;\n", LFS_CACHE_SIZE);
    EMIT("cfg.lookahead_size = %d;\n", LFS_LOOKAHEAD_SIZE);
    }
    EMIT("lfs_format(&lfs, &cfg) => 0;\n");
    EMIT("lfs_mount(&lfs, &cfg) => 0;\n");

    EMIT("char rbuff[2048];\nrbuff[0] = 0; // Reference this buffer\n");
  }

  int command_count = 1;
  uint32_t last_prog_erase = bd.stats.prog_count + bd.stats.erase_count;
  int err;

  if (setjmp(hook_abort)) {
    EMIT(";\n// POWERFAIL ==========================\n");
    EMIT("powerfail%d:\n", powerfail_index++);
    LOGOP("powerfail\n");

    LOGOP("  mount");
    EMIT("lfs_mount(&lfs, &cfg) => 0;\n");
    MUST_WORK(lfs_mount(FS, &cfg));

    for (i = 0; i < 4; i++) {
      openindex[i] = -1;
    }
  }

  if (skipitems['b']) {
    skipitems['b']++;
  }
  if (skipitems['A']) {
    skipitems['A']++;
  }

  while ((c = fgetc(f)) >= 0) {
    int add;
    char rbuff[2048];
    if (c <= ' ') {
      continue;
    }

    int skip = skipitems[c & 255];

    if (skip) {
      while (skip-- > 1) {
        (void) fgetc(f);
      }
      continue;
    }

    int arg = fgetc(f);
    if (arg < 0) {
      break;
    }
    int fdn = ((arg >> 6) & 3) % maxfds;

    switch(c) {
    case 'O':
      if (openindex[fdn] >= 0) {
        LOGOP("  close(%d)", fdn);
        EMIT("lfs_file_close(&lfs, &fd[%d]) => 0;\n", fdn);
        CHECK_RC(lfs_file_close(FS, &fd[fdn]));
        DUMP_CHANGED;
        openindex[fdn] = -1;
      }
#ifndef HAVE_MULTIPLE_OPEN
      {
        int index = (arg >> 3) & 7;
        for (i = 0; i < sizeof(openindex) / sizeof(openindex[0]); i++) {
          if (openindex[i] == index) {
            break;
          }
        }
        if (i < sizeof(openindex) / sizeof(openindex[0])) {
          break;
        }
      }
#endif
      LOGOP("  open(%d, \"%s\", 0x%x)", fdn, filename[(arg>>3) & 7], modes[arg & 7]);
      //memset(&fd[fdn], 0, sizeof(fd[fdn]));
      EMIT("lfs_file_open(&lfs, &fd[%d], \"%s\", 0x%x) ", fdn, filename[(arg>>3) & 7], modes[arg & 7]);
      err = lfs_file_open(FS, &fd[fdn], filename[(arg>>3) & 7], modes[arg & 7]);
      EMIT("=> %d;\n", err);
      CHECK_ERR;
      if (err >= 0) {
        openindex[fdn] = (arg >> 3) & 7;
      }
      LOGOP(" -> %d\n", err);
      break;

    case 'A':
    {
      int amnt = (fgetc(f) + (arg << 8));
      lfs_testbd_setpowerfail(&cfg, amnt, powerfail_behavior, hook_abort);
      EMIT("if (setjmp(powerfail)) { goto powerfail%d; }\n", powerfail_index);
      EMIT("lfs_testbd_setpowerfail(&cfg, %d, %d, powerfail);\n", amnt, powerfail_behavior);

      if (powerfail_behavior) {
          LOGOP("  Setting prog abort after (roughly) %d bytes, using realistic random corruption model\n", amnt);
      } else {
          LOGOP("  Setting prog abort after (roughly) %d bytes\n", amnt >> 5);
      }
      break;
    }

    case 's':
    {
      int mul = 17 + 2 * (arg & 7);
      int addv = arg >> 3;
      for (i = 0; i < sizeof(buff); i++) {
        buff[i] = i * mul + addv;
      }
      EMIT("for (i = 0; i < sizeof(buff); i++) { buff[i] = i * %d + %d; }\n", mul, addv);
      LOGOP(" Change data seed to i*%d + %d\n", mul, addv);
      break;
    }

    case 'S':
      if (openindex[fdn] >= 0) {
        int offset = (14 << (arg & 7)) + arg;
        if (arg & 16) {
          offset = -offset;
        }
        int whence = (arg & 63) % 3;
        LOGOP("  seek(%d, %d, %d)", fdn, offset, whence);
        EMIT("lfs_file_seek(&lfs, &fd[%d], %d, %d) ", fdn, offset, whence);
        CHECK_RC(lfs_file_seek(FS, &fd[fdn], offset, whence));
        EMIT("=> %d;\n", err);
      }
      break;

    case 'R':
      if (openindex[fdn] >= 0) {
        LOGOP("  read(%d, , %d)", fdn, (15 << (arg & 7)) + (arg & 127));
        EMIT("lfs_file_read(&lfs, &fd[%d], rbuff, %d) ", fdn, (15 << (arg & 7)) + (arg & 127));
        err = lfs_file_read(FS, &fd[fdn], rbuff, (15 << (arg & 7)) + (arg & 127));
        EMIT("=> %d;\n", err);
        LOGOP(" -> %d\n", err);
        CHECK_ERR;
      }
      break;

    case 'W':
      if (openindex[fdn] >= 0) {
        LOGOP("  write(%d, , %d)", fdn, (15 << (arg & 7)) + (arg & 127));
        EMIT("lfs_file_write(&lfs, &fd[%d], buff, %d) => %d;\n", fdn, (15 << (arg & 7)) + (arg & 127), (15 << (arg & 7)) + (arg & 127));
        err = lfs_file_write(FS, &fd[fdn], buff, (15 << (arg & 7)) + (arg & 127));
        LOGOP(" -> %d\n", err);
        CHECK_ERR;
      }
      break;

    case 'C':
      if (openindex[fdn] >= 0) {
        LOGOP("  close(%d)", fdn);
        EMIT("lfs_file_close(&lfs, &fd[%d]) => 0;\n", fdn);
        CHECK_RC(lfs_file_close(FS, &fd[fdn]));
      }
      openindex[fdn] = -1;
      break;

    case 'b':
      add = fgetc(f);
      EMIT("for (i = 0; i < sizeof(buff); i++) { buff[i] = i * %d + %d; }\n", arg, add);
      for (i = 0; i < sizeof(buff); i++) {
        buff[i] = add + i * arg;
      }
      break;

    case 't':
      if (openindex[fdn] >= 0) {
        add = fgetc(f) * 17;
        LOGOP("  truncate(%d, %d)", fdn, add);
        EMIT("lfs_file_truncate(&lfs, &fd[%d], %d) => 0;\n", fdn, add);
        CHECK_RC(lfs_file_truncate(FS, &fd[fdn], add));
      }
      break;

    case 'f':
      if (openindex[fdn] >= 0) {
        LOGOP("  sync(%d)", fdn);
        EMIT("lfs_file_sync(&lfs, &fd[%d]) => 0;\n", fdn);
        CHECK_RC(lfs_file_sync(FS, &fd[fdn]));
      }
      break;

    case 'd':
    if (no_open_remove) {
      int findex = arg & 7;
      for (i = 0; i < sizeof(openindex) / sizeof(openindex[0]); i++) {
        if (openindex[i] == findex) {
          break;
        }
      }
      if (i < sizeof(openindex) / sizeof(openindex[0])) {
        break;
      }
    }
    LOGOP("  remove(\"%s\")", filename[arg & 7]);
    EMIT("lfs_remove(&lfs, \"%s\") ", filename[arg & 7]);
    err = lfs_remove(FS, filename[arg & 7]);
    EMIT("=> %d;\n", err);
    LOGOP(" -> %d\n", err);
    CHECK_ERR;
    break;

    case 'r':
    if (no_open_remove) {
      int findex = arg & 7;
      for (i = 0; i < sizeof(openindex) / sizeof(openindex[0]); i++) {
        if (openindex[i] == findex) {
          break;
        }
      }
      if (i < sizeof(openindex) / sizeof(openindex[0])) {
        break;
      }
    }
    LOGOP("  rename(\"%s\", \"%s\")", filename[arg & 7], filename[(arg >> 3) & 7]);
    EMIT("lfs_rename(&lfs, \"%s\", \"%s\") ", filename[arg & 7], filename[(arg >> 3) & 7]);
    err = lfs_rename(FS, filename[arg & 7], filename[(arg >> 3) & 7]);
    EMIT("=> %d;\n", err);
    LOGOP(" -> %d\n", err);
    CHECK_ERR;
    break;

    case 'U':
      for (i = 0; i < 4; i++) {
        openindex[i] = -1;
      }
      {
        if (arg & 0x01) {
          LOGOP("  unmount\n");
          EMIT("lfs_unmount(&lfs) => 0;\n");
          lfs_unmount(FS);
        }

        LOGOP("  mount");
        EMIT("lfs_mount(&lfs, &cfg) => 0;\n");
        MUST_WORK(lfs_mount(FS, &cfg));
      }
      break;

    case 'n':
      // Set powerfail behavior 
      if (powerfail_behavior) {
        powerfail_behavior = ((arg << 8) + fgetc(f)) | 0x10000;
      }
      break;

#if 0
    case 'c':
    {
      LOGOP("  check()");
      rc = SPIFFS_check(FS);
      LOGOP(" -> %d\n", rc);
      ungetc(arg, f);
      break;
    }
#endif

    default:
      ungetc(arg, f);
      continue;
    }

    DUMP_CHANGED;
  }

  for (i = 0; i < 4; i++) {
    if (openindex[i] >= 0) {
      LOGOP("  finally close(%d)", i);
      EMIT("lfs_file_close(&lfs, &fd[%d]) => 0;\n", i);
      CHECK_RC(lfs_file_close(FS, &fd[i]));
    }
  }

  return 0;
}
