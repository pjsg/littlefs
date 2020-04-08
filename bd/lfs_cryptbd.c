/*
 * Pass through bd which encrypts memory
 *
 */

#include "bd/lfs_cryptbd.h"
#include <errno.h>

int lfs_cryptbd_create(const struct lfs_config *cfg, const struct lfs_config *lower, const struct lfs_cryptbd_config *bdcfg) {
    struct lfs_cryptbd *bd = cfg->context;
    memcpy(bd->key, bdcfg->key, sizeof(bd->key));
    bd->lower_lfs_cfg = *lower;

    if (sizeof(bd->key) < cfg->prog_size) {
        return -EINVAL;
    }

    return 0;  
}

// Read a block
int lfs_cryptbd_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size) {
    struct lfs_cryptbd *bd = cfg->context;

    uint8_t dec_buffer[size];

    int err = bd->lower_lfs_cfg.read(&bd->lower_lfs_cfg, block, off, dec_buffer, size);

    if (err >= 0) {
        for (size_t i = 0; i < size; i++) {
            ((uint8_t *) buffer)[i] = dec_buffer[i] ^ bd->key[i % cfg->prog_size];
        }
    }

    return err;
}

// Program a block
//
// The block must have previously been erased.
int lfs_cryptbd_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size) {
    struct lfs_cryptbd *bd = cfg->context;

    uint8_t enc_buffer[size];

    for (size_t i = 0; i < size; i++) {
        enc_buffer[i] = ((uint8_t *) buffer)[i] ^ bd->key[i % cfg->prog_size];
    }

    return bd->lower_lfs_cfg.prog(&bd->lower_lfs_cfg, block, off, enc_buffer, size);
}

// Erase a block
//
// A block must be erased before being programmed. The
// state of an erased block is undefined.
int lfs_cryptbd_erase(const struct lfs_config *cfg, lfs_block_t block) {
    struct lfs_cryptbd *bd = cfg->context;

    return bd->lower_lfs_cfg.erase(&bd->lower_lfs_cfg, block);
}

// Sync the block device
int lfs_cryptbd_sync(const struct lfs_config *cfg) {
    struct lfs_cryptbd *bd = cfg->context;

    return bd->lower_lfs_cfg.sync(&bd->lower_lfs_cfg);
}


