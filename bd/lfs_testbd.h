/*
 * Testing block device, wraps mmapbd while providing a bunch
 * of hooks for testing littlefs in various conditions.
 *
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef LFS_TESTBD_H
#define LFS_TESTBD_H

#include "lfs.h"
#include "lfs_util.h"
#include "bd/lfs_mmapbd.h"
#include "bd/lfs_filebd.h"
#include <setjmp.h>

#ifdef __cplusplus
extern "C"
{
#endif


// Block device specific tracing
#ifdef LFS_TESTBD_YES_TRACE
#define LFS_TESTBD_TRACE(...) LFS_TRACE(__VA_ARGS__)
#else
#define LFS_TESTBD_TRACE(...)
#endif

// Mode determining how "bad blocks" behave during testing. This simulates
// some real-world circumstances such as progs not sticking (prog-noop),
// a readonly disk (erase-noop), and ECC failures (read-error).
//
// Not that read-noop is not allowed. Read _must_ return a consistent (but
// may be arbitrary) value on every read.
enum lfs_testbd_badblock_behavior {
    LFS_TESTBD_BADBLOCK_PROGERROR,
    LFS_TESTBD_BADBLOCK_ERASEERROR,
    LFS_TESTBD_BADBLOCK_READERROR,
    LFS_TESTBD_BADBLOCK_PROGNOOP,
    LFS_TESTBD_BADBLOCK_ERASENOOP,
};

// 0 is write whole bytes. Anything else is used as a seed for randomly corrupting the whole area.
typedef uint32_t powerfail_behavior_t;

// Type for measuring wear
typedef uint32_t lfs_testbd_wear_t;
typedef int32_t  lfs_testbd_swear_t;

// testbd config, this is required for testing
struct lfs_testbd_config {
    // 8-bit erase value to use for simulating erases. -1 does not simulate
    // erases, which can speed up testing by avoiding all the extra block-device
    // operations to store the erase value.
    int32_t erase_value;

    // If non-zero, then we emulate an encrypted drive -- all this means is that
    // erased space is junk.
    uint32_t key;

    // Number of erase cycles before a block becomes "bad". The exact behavior
    // of bad blocks is controlled by the badblock_mode.
    uint32_t erase_cycles;

    // The mode determining how bad blocks fail
    uint8_t badblock_behavior;

    // Number of write operations (erase/prog) before forcefully killing
    // the program with exit. Simulates power-loss. 0 disables.
    uint32_t power_cycles;

    // Optional buffer for RAM block device.
    void *buffer;

    // Optional buffer for wear
    void *wear_buffer;

};

typedef struct {
      unsigned int read_count;
      unsigned int read_byte_count;
      unsigned int prog_count;
      unsigned int prog_byte_count;
      unsigned int erase_count;
} lfs_testbd_stats_t;

// testbd state
typedef struct lfs_testbd {
    union {
        struct {
            lfs_mmapbd_t bd;
            struct lfs_mmapbd_config cfg;
        } mmap;
    } u;

    uint8_t destroy_mmapbd;
    uint32_t power_cycles;
    lfs_testbd_wear_t *wear;
    //
    // powerfail after operation count
    int powerfail_after;

    // Do we write the last byte in a powerfail completely or only partially?
    powerfail_behavior_t powerfail_behavior;

    // Where to go on powerfail
    jmp_buf powerfail;

    lfs_testbd_stats_t  stats;

    struct lfs_config lower_lfs_cfg;

    const struct lfs_testbd_config *cfg;
} lfs_testbd_t;


/// Block device API ///

// Create a test block device using the geometry in lfs_config
//
// Note that the path is optional.
int lfs_testbd_create(const struct lfs_config *cfg, const char *path);
int lfs_testbd_createcfg(const struct lfs_config *cfg, const char *path,
        const struct lfs_testbd_config *bdcfg);
int lfs_testbd_create_lower(const struct lfs_config *cfg, const struct lfs_config *underlying_cfg, const struct lfs_testbd_config *bdcfg);

// Clean up memory associated with block device
int lfs_testbd_destroy(const struct lfs_config *cfg);

// Read a block
int lfs_testbd_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size);

// Program a block
//
// The block must have previously been erased.
int lfs_testbd_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size);

// Erase a block
//
// A block must be erased before being programmed. The
// state of an erased block is undefined.
int lfs_testbd_erase(const struct lfs_config *cfg, lfs_block_t block);

// Sync the block device
int lfs_testbd_sync(const struct lfs_config *cfg);


/// Additional extended API for driving test features ///

// Get simulated wear on a given block
lfs_testbd_swear_t lfs_testbd_getwear(const struct lfs_config *cfg,
        lfs_block_t block);

// Manually set simulated wear on a given block
int lfs_testbd_setwear(const struct lfs_config *cfg,
        lfs_block_t block, lfs_testbd_wear_t wear);

void lfs_testbd_setpowerfail(const struct lfs_config *cfg, int powerfail_after, powerfail_behavior_t powerfail_behavior, jmp_buf powerfail);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
