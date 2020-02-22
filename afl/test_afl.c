
/* The follow defines control details of how the fuzzer can exercise the API. If you
 * undef any of these, then the fuzzer is less brutal. FOr example, if you undef
 * HAVE_REMOVE_OPEN, then the fuzzer will not attempt to remove (or rename) an open file
 */
#define HAVE_REMOVE_OPEN
#define HAVE_MULTIPLE_OPEN

#include "lfs.h"
#include "bd/lfs_rambd.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <setjmp.h>

static int run_fuzz_test(FILE *f, int maxfds);

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
lfs_rambd_t bd;
// other declarations for convenience
lfs_file_t file;
lfs_dir_t dir;
struct lfs_info info;
uint8_t buffer[1024];
char path[1024];

int debuglog;

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


static int hook_lfs_rambd_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size);

static int hook_lfs_rambd_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size);

static int hook_lfs_rambd_erase(const struct lfs_config *cfg, lfs_block_t block);

static int hook_lfs_rambd_sync(const struct lfs_config *cfg);



const struct lfs_config cfg = {
    .context = &bd,
    .read  = &hook_lfs_rambd_read,
    .prog  = &hook_lfs_rambd_prog,
    .erase = &hook_lfs_rambd_erase,
    .sync  = &hook_lfs_rambd_sync,

    .read_size      = LFS_READ_SIZE,
    .prog_size      = LFS_PROG_SIZE,
    .block_size     = LFS_BLOCK_SIZE,
    .block_count    = LFS_BLOCK_COUNT,
    .block_cycles   = LFS_BLOCK_CYCLES,
    .cache_size     = LFS_CACHE_SIZE,
    .lookahead_size = LFS_LOOKAHEAD_SIZE,
};

#define CHECK_ERR      if (err == LFS_ERR_CORRUPT) abort()
#define CHECK_RC(call) { int err = call; LOGOP(" -> %d\n", err); if (err == LFS_ERR_CORRUPT) abort(); }
#define MUST_WORK(call) { int err = call; LOGOP(" -> %d\n", err); if (err < 0) { printf("**** " #call " must work and it failed\n"); abort(); }}
#define LOGOP if (check_duration() || debuglog) printf

static int last_read = 0;
static int last_prog = 0;
static int last_erase = 0;

int break_suffix = -1;

static void dump_disk_suffix(int suffix) {
  char fname[256];
  sprintf(fname, "/tmp/littlefs-disk-%d", suffix);

  FILE *f = fopen(suffix ? fname : "/tmp/littlefs-disk", "wb");

  uint8_t *buffer = malloc(cfg.block_size);

  for (int i = 0; i < cfg.block_count; i++) {
    lfs_rambd_read(&cfg, i, 0, buffer, cfg.block_size);
    fwrite(buffer, cfg.block_size, 1, f);
    last_read += cfg.block_size;
  }

  fclose(f);

  if (break_suffix == suffix) {
    __asm__("int $3");
  }
}

static void dump_disk(int info) {
  dump_disk_suffix(0);
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
    debuglog = argc > 1;
    if (debuglog) {
      lfs_rambd_create_mmap(&cfg, "/tmp/littlefs-live-disk");
    } else {
      lfs_rambd_create(&cfg);
    }

    gettimeofday(&last, 0);

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
    run_fuzz_test(stdin, 4);
}

static int hook_abort_after = -1;
static int hook_last_write_length = 0;
static jmp_buf hook_abort;

static int hook_lfs_rambd_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size) {
  if (hook_abort_after > 0) hook_abort_after--;

  if (hook_abort_after == 0) {
    LOGOP(" Failing read\n");
    longjmp(hook_abort, 1);
  }

  return lfs_rambd_read(cfg, block, off, buffer, size);
}

static int hook_lfs_rambd_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size){
  if (hook_abort_after > 0) hook_abort_after--;

  if (hook_abort_after == 0) {
    if (hook_last_write_length != 0) {
      void *nbuffer = malloc(size);

      lfs_size_t nsize = size;

      if (hook_last_write_length < 128) {
        if (nsize > hook_last_write_length) {
          nsize = hook_last_write_length;
        }
      } else {
        hook_last_write_length = 256 - hook_last_write_length;
        if (nsize > hook_last_write_length) {
          nsize -= hook_last_write_length;
        }
      }

      LOGOP("  Adjusting size %d -> %d before abort\n", size, nsize);
      memcpy(nbuffer, buffer, nsize);
      memset(nbuffer + nsize, 0xff, size - nsize);
      lfs_rambd_prog(cfg, block, off, nbuffer, size);
      free(nbuffer);
    }
    LOGOP(" Failing prog\n");
    longjmp(hook_abort, 1);
  }

  int rc = lfs_rambd_prog(cfg, block, off, buffer, size);
  if (rc < 0) {
    LOGOP("  prog operation was aborted\n");
    longjmp(hook_abort, 1);
  }
}

static int hook_lfs_rambd_erase(const struct lfs_config *cfg, lfs_block_t block) {
  if (hook_abort_after > 0) hook_abort_after--;

  if (hook_abort_after == 0) {
    LOGOP(" Failing prog\n");
    longjmp(hook_abort, 1);
  }

  return lfs_rambd_erase(cfg, block);
}

static int hook_lfs_rambd_sync(const struct lfs_config *cfg) {
  if (hook_abort_after > 0) hook_abort_after--;

  if (hook_abort_after == 0) {
    LOGOP(" Failing sync\n");
    longjmp(hook_abort, 1);
  }

  return lfs_rambd_sync(cfg);
}

static int run_fuzz_test(FILE *f, int maxfds) {
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

  int c;

  lfs_file_t fd[4];
  memset(fd, -1, sizeof(fd));
  int openindex[4];
  memset(openindex, -1, sizeof(openindex));
  char *filename[8];

  int i;

  for (i = 0; i < 8; i++) {
    char buff[128];
    snprintf(buff, sizeof(buff), "%dfile%d.xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxasdasdasdadxxxxxxxxxxxxxxxxxxx", i, i);
    buff[9 + 2 * i] = 0;
    filename[i] = strdup(buff);
  }

  // The list of 8 modes that are chosen. SPIFFS_EXCL is not present -- it probably ought to be.
  int modes[8] = {LFS_O_RDONLY, LFS_O_RDWR, LFS_O_RDWR|LFS_O_TRUNC, LFS_O_RDWR|LFS_O_CREAT, LFS_O_RDWR|LFS_O_CREAT|LFS_O_TRUNC,
      LFS_O_WRONLY|LFS_O_CREAT|LFS_O_TRUNC, LFS_O_RDWR|LFS_O_CREAT|LFS_O_TRUNC, LFS_O_WRONLY};

  char buff[2048];
  for (i = 0; i < sizeof(buff); i++) {
    buff[i] = i * 19;
  }

  if (setjmp(hook_abort)) {
    LOGOP("powerfail\n");
    hook_abort_after = -1;

    LOGOP("  mount");
    MUST_WORK(lfs_mount(FS, &cfg));

    for (i = 0; i < 4; i++) {
      openindex[i] = -1;
    }
  }

  int command_count = 0;

  while ((c = fgetc(f)) >= 0) {
    int add;
    char rbuff[2048];
    if (c <= ' ') {
      continue;
    }
    int arg = fgetc(f);
    if (arg < 0) {
      break;
    }
    command_count++;
    int fdn = ((arg >> 6) & 3) % maxfds;
    int rc;
    int err;
    switch(c) {
    case 'O':
      if (openindex[fdn] >= 0) {
        LOGOP("  close(%d)", fdn);
        CHECK_RC(lfs_file_close(FS, &fd[fdn]));
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
      err = lfs_file_open(FS, &fd[fdn], filename[(arg>>3) & 7], modes[arg & 7]);
      CHECK_ERR;
      if (err >= 0) {
        openindex[fdn] = (arg >> 3) & 7;
      }
      LOGOP(" -> %d\n", err);
      break;

    case 'p':
      hook_abort_after = arg + 1;
      LOGOP("  Setting powerfail in %d accesses\n", hook_abort_after);
      break;

    case 'P':
      hook_last_write_length = arg;
      break;

    case 'A':
    {
      int amnt = fgetc(f) + (arg << 8);
      lfs_rambd_prog_abort(&cfg, amnt);
      LOGOP("  Setting prog abort after %d bytes, bitor = %d\n", amnt >> 5, (7 * (amnt & 31)) & 255);
      break;
    }

    case 's':
    {
      int mul = 17 + 2 * (arg & 7);
      int add = arg >> 3;
      for (i = 0; i < sizeof(buff); i++) {
        buff[i] = i * mul + add;
      }
      LOGOP(" Change data seed to i*%d + %d\n", mul, add);
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
        CHECK_RC(lfs_file_seek(FS, &fd[fdn], offset, whence));
      }
      break;

    case 'R':
      if (openindex[fdn] >= 0) {
        LOGOP("  read(%d, , %d)", fdn, (15 << (arg & 7)) + (arg & 127));
        int err = lfs_file_read(FS, &fd[fdn], rbuff, (15 << (arg & 7)) + (arg & 127));
        LOGOP(" -> %d\n", err);
        CHECK_ERR;
      }
      break;

    case 'W':
      if (openindex[fdn] >= 0) {
        LOGOP("  write(%d, , %d)", fdn, (15 << (arg & 7)) + (arg & 127));
        int err = lfs_file_write(FS, &fd[fdn], buff, (15 << (arg & 7)) + (arg & 127));
        LOGOP(" -> %d\n", err);
        CHECK_ERR;
      }
      break;

    case 'C':
      if (openindex[fdn] >= 0) {
        LOGOP("  close(%d)", fdn);
        CHECK_RC(lfs_file_close(FS, &fd[fdn]));
      }
      openindex[fdn] = -1;
      break;

    case 'b':
      add = fgetc(f);
      for (i = 0; i < sizeof(buff); i++) {
        buff[i] = add + i * arg;
      }
      break;

    case 't':
      if (openindex[fdn] >= 0) {
        add = fgetc(f) * 17;
        LOGOP("  truncate(%d, %d)", fdn, add);
        CHECK_RC(lfs_file_truncate(FS, &fd[fdn], add));
      }
      break;

    case 'f':
      if (openindex[fdn] >= 0) {
        LOGOP("  sync(%d)", fdn);
        CHECK_RC(lfs_file_sync(FS, &fd[fdn]));
      }
      break;

    case 'd':
#ifndef HAVE_REMOVE_OPEN
    {
      int index = arg & 7;
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
    LOGOP("  remove(\"%s\")", filename[arg & 7]);
    err = lfs_remove(FS, filename[arg & 7]);
    LOGOP(" -> %d\n", err);
    CHECK_ERR;
    break;

    case 'r':
#ifndef HAVE_REMOVE_OPEN
    {
      int index = arg & 7;
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
    LOGOP("  rename(\"%s\", \"%s\")", filename[arg & 7], filename[(arg >> 3) & 7]);
    err = lfs_rename(FS, filename[arg & 7], filename[(arg >> 3) & 7]);
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
          lfs_unmount(FS);
        }

        LOGOP("  mount");
        MUST_WORK(lfs_mount(FS, &cfg));
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

    if (debuglog) {
      printf("{d%d} ", command_count);
      dump_disk_suffix(command_count);
    }
  }

  for (i = 0; i < 4; i++) {
    if (openindex[i] >= 0) {
      LOGOP("  finally close(%d)", i);
      CHECK_RC(lfs_file_close(FS, &fd[i]));
    }
  }

  return 0;
}
