/*
 * Block device emulated in RAM
 *
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef LFSMMAPBD_H
#define LFSMMAPBD_H

#include "lfs.h"
#include "lfs_util.h"

#ifdef __cplusplus
extern "C"
{
#endif


// Block device specific tracing
#ifdef LFS_MMAPBD_YES_TRACE
#define LFS_MMAPBD_TRACE(...) LFS_TRACE(__VA_ARGS__)
#else
#define LFS_MMAPBD_TRACE(...)
#endif

// rambd config (optional)
struct lfs_mmapbd_config {
    // 8-bit erase value to simulate erasing with. -1 indicates no erase
    // occurs, which is still a valid block device
    int32_t erase_value;

    // Optional statically allocated buffer for the block device.
    void *buffer;
};

// rambd state
typedef struct lfs_mmapbd {
    uint8_t *buffer;
    const struct lfs_mmapbd_config *cfg;
} lfs_mmapbd_t;


// Create a RAM block device using the geometry in lfs_config
int lfs_mmapbd_create(const struct lfs_config *cfg);
int lfs_mmapbd_createcfg_mmap(const struct lfs_config *cfg, const struct lfs_mmapbd_config *bdcfg, const char *filename);
int lfs_mmapbd_createcfg(const struct lfs_config *cfg,
        const struct lfs_mmapbd_config *bdcfg);

// Clean up memory associated with block device
int lfs_mmapbd_destroy(const struct lfs_config *cfg);

// Read a block
int lfs_mmapbd_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size);

// Program a block
//
// The block must have previously been erased.
int lfs_mmapbd_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size);

// Erase a block
//
// A block must be erased before being programmed. The
// state of an erased block is undefined.
int lfs_mmapbd_erase(const struct lfs_config *cfg, lfs_block_t block);

// Sync the block device
int lfs_mmapbd_sync(const struct lfs_config *cfg);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
