/*
 * Pass through bd which encrypts memory
 *
 */
#ifndef LFSCRYPTBD_H
#define LFSCRYPTBD_H

#include "lfs.h"
#include "lfs_util.h"

#ifdef __cplusplus
extern "C"
{
#endif


// Block device specific tracing
#ifdef LFS_CRYPTBD_YES_TRACE
#define LFS_CRYPTBD_TRACE(...) LFS_TRACE(__VA_ARGS__)
#else
#define LFS_CRYPTBD_TRACE(...)
#endif

// rambd config (optional)
struct lfs_cryptbd_config {
    uint8_t key[16];
};

// rambd state
typedef struct lfs_cryptbd {
    const struct lfs_cryptbd_config *cfg;
    uint8_t key[16];
    struct lfs_config lower_lfs_cfg;
} lfs_cryptbd_t;

int lfs_cryptbd_create(const struct lfs_config *cfg, const struct lfs_config *lower, const struct lfs_cryptbd_config *bdcfg);

// Read a block
int lfs_cryptbd_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size);

// Program a block
//
// The block must have previously been erased.
int lfs_cryptbd_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size);

// Erase a block
//
// A block must be erased before being programmed. The
// state of an erased block is undefined.
int lfs_cryptbd_erase(const struct lfs_config *cfg, lfs_block_t block);

// Sync the block device
int lfs_cryptbd_sync(const struct lfs_config *cfg);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
