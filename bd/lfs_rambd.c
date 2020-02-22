/*
 * Block device emulated in RAM
 *
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "bd/lfs_rambd.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

int lfs_rambd_createcfg(const struct lfs_config *cfg,
        const struct lfs_rambd_config *bdcfg) {
    LFS_TRACE("lfs_rambd_createcfg(%p {.context=%p, "
                ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
                ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
                ".block_size=%"PRIu32", .block_count=%"PRIu32"}, "
                "%p {.erase_value=%"PRId32", .buffer=%p})",
            (void*)cfg, cfg->context,
            (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
            (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
            cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count,
            (void*)bdcfg, bdcfg->erase_value, bdcfg->buffer);
    lfs_rambd_t *bd = cfg->context;
    bd->cfg = bdcfg;

    bd->prog_abort_bits = 0;

    // allocate buffer?
    if (bd->cfg->buffer) {
        bd->buffer = bd->cfg->buffer;
    } else {
        bd->buffer = lfs_malloc(cfg->block_size * cfg->block_count);
        if (!bd->buffer) {
            LFS_TRACE("lfs_rambd_createcfg -> %d", LFS_ERR_NOMEM);
            return LFS_ERR_NOMEM;
        }
    }

    // set to something that isn't the erase value.
    memset(bd->buffer, ~bd->cfg->erase_value,
            cfg->block_size * cfg->block_count);

    LFS_TRACE("lfs_rambd_createcfg -> %d", 0);
    return 0;
}

int lfs_rambd_create_mmap(const struct lfs_config *cfg, const char *filename) {
    LFS_TRACE("lfs_rambd_create(%p {.context=%p, "
                ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
                ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
                ".block_size=%"PRIu32", .block_count=%"PRIu32"})",
            (void*)cfg, cfg->context,
            (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
            (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
            cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count);
    static struct lfs_rambd_config defaults = {.erase_value=-1};
    int fd = open(filename, O_CREAT|O_RDWR, 0640);
    char *buffer = malloc(cfg->block_size * cfg->block_count);
    memset(buffer, 0, cfg->block_size * cfg->block_count);
    write(fd, buffer, cfg->block_size * cfg->block_count);
    free(buffer);
    defaults.buffer = mmap(0, cfg->block_size * cfg->block_count, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    int err = lfs_rambd_createcfg(cfg, &defaults);
    LFS_TRACE("lfs_rambd_create -> %d", err);
    return err;
}

int lfs_rambd_create(const struct lfs_config *cfg) {
    LFS_TRACE("lfs_rambd_create(%p {.context=%p, "
                ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
                ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
                ".block_size=%"PRIu32", .block_count=%"PRIu32"})",
            (void*)cfg, cfg->context,
            (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
            (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
            cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count);
    static const struct lfs_rambd_config defaults = {.erase_value=-1};
    int err = lfs_rambd_createcfg(cfg, &defaults);
    LFS_TRACE("lfs_rambd_create -> %d", err);
    return err;
}

int lfs_rambd_destroy(const struct lfs_config *cfg) {
    LFS_TRACE("lfs_rambd_destroy(%p)", (void*)cfg);
    // clean up memory
    lfs_rambd_t *bd = cfg->context;
    if (!bd->cfg->buffer) {
        lfs_free(bd->buffer);
    }
    LFS_TRACE("lfs_rambd_destroy -> %d", 0);
    return 0;
}

int lfs_rambd_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size) {
    LFS_TRACE("lfs_rambd_read(%p, 0x%"PRIx32", %"PRIu32", %p, %"PRIu32")",
            (void*)cfg, block, off, buffer, size);
    lfs_rambd_t *bd = cfg->context;

    // check if read is valid
    LFS_ASSERT(off  % cfg->read_size == 0);
    LFS_ASSERT(size % cfg->read_size == 0);
    LFS_ASSERT(block < cfg->block_count);

    // read data
    memcpy(buffer, &bd->buffer[block*cfg->block_size + off], size);

    bd->stats.read_count += size;

    LFS_TRACE("lfs_rambd_read -> %d", 0);
    return 0;
}

int lfs_rambd_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size) {
    LFS_TRACE("lfs_rambd_prog(%p, 0x%"PRIx32", %"PRIu32", %p, %"PRIu32")",
            (void*)cfg, block, off, buffer, size);
    lfs_rambd_t *bd = cfg->context;

    // check if write is valid
    LFS_ASSERT(off  % cfg->prog_size == 0);
    LFS_ASSERT(size % cfg->prog_size == 0);
    LFS_ASSERT(block < cfg->block_count);

    // check that data was erased? only needed for testing
    for (lfs_off_t i = 0; i < size; i++) {
        int8_t current = bd->buffer[block*cfg->block_size + off + i];
        uint8_t new_value = ((uint8_t *)buffer)[i];
        if (current != bd->cfg->erase_value) {
          printf("\nTrying to program 0x%02x into location with value 0x%02x [at offset 0x%x (in block %d) in a length of %d]\n",
              new_value, current, i + off, block, size);
          LFS_ASSERT(current == bd->cfg->erase_value);
        }
    }

    int rc = 0;

    // program data
    lfs_size_t nsize = size;
    if (bd->prog_abort_bits > 0 && (bd->prog_abort_bits >> 5) < nsize) {
      nsize = bd->prog_abort_bits >> 5;
      rc = -1;
      printf("\nPowerfail during write of %d bytes at offset 0x%x in block %d. Wrote %d bytes.\n",
         size, off, block, nsize);
    }
    memcpy(&bd->buffer[block*cfg->block_size + off], buffer, nsize);
    bd->prog_abort_bits -= nsize << 5;
    off += nsize;
    size -= nsize;
    bd->stats.prog_count += nsize;

    if (size && bd->prog_abort_bits) {
      // need to do a few bits in the last byte
      bd->buffer[block * cfg->block_size + off] = ((uint8_t *) buffer)[nsize] | (7 * bd->prog_abort_bits);
      bd->prog_abort_bits = 0;
      rc = -1;
      printf("Byte at offset 0x%x should have been 0x%02x, but wrote 0x%02x instead.\n",
         off, ((uint8_t *) buffer)[nsize], bd->buffer[block * cfg->block_size + off]);
    }

    LFS_TRACE("lfs_rambd_prog -> %d", rc);
    return rc;
}

void lfs_rambd_prog_abort(const struct lfs_config *cfg, unsigned int abort_bits) {
    lfs_rambd_t *bd = cfg->context;
    bd->prog_abort_bits = abort_bits;
}

int lfs_rambd_erase(const struct lfs_config *cfg, lfs_block_t block) {
    LFS_TRACE("lfs_rambd_erase(%p, 0x%"PRIx32")", (void*)cfg, block);
    lfs_rambd_t *bd = cfg->context;

    // check if erase is valid
    LFS_ASSERT(block < cfg->block_count);

    // erase, only needed for testing
    memset(&bd->buffer[block*cfg->block_size],
            bd->cfg->erase_value, cfg->block_size);

    bd->stats.erase_count++;

    LFS_TRACE("lfs_rambd_erase -> %d", 0);
    return 0;
}

int lfs_rambd_sync(const struct lfs_config *cfg) {
    LFS_TRACE("lfs_rambd_sync(%p)", (void*)cfg);
    // sync does nothing because we aren't backed by anything real
    (void)cfg;
    LFS_TRACE("lfs_rambd_sync -> %d", 0);
    return 0;
}
