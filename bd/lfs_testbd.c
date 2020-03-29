/*
 * Testing block device, wraps filebd and rambd while providing a bunch
 * of hooks for testing littlefs in various conditions.
 *
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "bd/lfs_testbd.h"

#include <stdlib.h>

static void handle_powerfail(lfs_testbd_t *bd) {
    if (bd->powerfail_after <= 0) {
      return;
    }
    bd->powerfail_after--;
    if (bd->powerfail_after == 0) {
      longjmp(bd->powerfail, 1);
    }
}

int lfs_testbd_createcfg(const struct lfs_config *cfg, const char *path,
        const struct lfs_testbd_config *bdcfg) {
    LFS_TRACE("lfs_testbd_createcfg(%p {.context=%p, "
                ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
                ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
                ".block_size=%"PRIu32", .block_count=%"PRIu32"}, "
                "\"%s\", "
                "%p {.erase_value=%"PRId32", .erase_cycles=%"PRIu32", "
                ".badblock_behavior=%"PRIu8", .power_cycles=%"PRIu32", "
                ".buffer=%p, .wear_buffer=%p})",
            (void*)cfg, cfg->context,
            (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
            (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
            cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count,
            path, (void*)bdcfg, bdcfg->erase_value, bdcfg->erase_cycles,
            bdcfg->badblock_behavior, bdcfg->power_cycles,
            bdcfg->buffer, bdcfg->wear_buffer);
    lfs_testbd_t *bd = cfg->context;
    bd->cfg = bdcfg;
    bd->powerfail_after = 0;

    // setup testing things
    bd->power_cycles = bd->cfg->power_cycles;

    if (bd->cfg->erase_cycles) {
        if (bd->cfg->wear_buffer) {
            bd->wear = bd->cfg->wear_buffer;
        } else {
            bd->wear = lfs_malloc(sizeof(lfs_testbd_wear_t) * cfg->block_count);
            if (!bd->wear) {
                LFS_TRACE("lfs_testbd_createcfg -> %d", LFS_ERR_NOMEM);
                return LFS_ERR_NOMEM;
            }
        }

        memset(bd->wear, 0, sizeof(lfs_testbd_wear_t) * cfg->block_count);
    }

    ((struct lfs_testbd_config *) bdcfg)->ram_cfg = *cfg;
    ((struct lfs_testbd_config *) bdcfg)->ram_cfg.prog_size = 1;

    // create underlying block device
    bd->u.ram.cfg = (struct lfs_rambd_config){
        .erase_value = bd->cfg->erase_value,
        .buffer = bd->cfg->buffer,
    };
    int err;
    if (path) {
      err = lfs_rambd_createcfg_mmap(&bd->cfg->ram_cfg, &bd->u.ram.cfg, path);
    } else {
      err = lfs_rambd_createcfg(&bd->cfg->ram_cfg, &bd->u.ram.cfg);
    }
    LFS_TRACE("lfs_testbd_createcfg -> %d", err);
    return err;
}

int lfs_testbd_create(const struct lfs_config *cfg, const char *path) {
    LFS_TRACE("lfs_testbd_create(%p {.context=%p, "
                ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
                ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
                ".block_size=%"PRIu32", .block_count=%"PRIu32"}, "
                "\"%s\")",
            (void*)cfg, cfg->context,
            (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
            (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
            cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count,
            path);
    static const struct lfs_testbd_config const_defaults = {.erase_value=-1};
    struct lfs_testbd_config *defaults = (struct lfs_testbd_config *) malloc(sizeof(*defaults));
    *defaults = const_defaults;
    int err = lfs_testbd_createcfg(cfg, path, defaults);
    LFS_TRACE("lfs_testbd_create -> %d", err);
    return err;
}

int lfs_testbd_destroy(const struct lfs_config *cfg) {
    LFS_TRACE("lfs_testbd_destroy(%p)", (void*)cfg);
    lfs_testbd_t *bd = cfg->context;
    if (bd->cfg->erase_cycles && !bd->cfg->wear_buffer) {
        lfs_free(bd->wear);
    }

    int err = lfs_rambd_destroy(&bd->cfg->ram_cfg);
    LFS_TRACE("lfs_testbd_destroy -> %d", err);
    return err;
}

/// Internal mapping to block devices ///
static int lfs_testbd_rawread(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size) {
    lfs_testbd_t *bd = cfg->context;
    handle_powerfail(bd);
    bd->stats.read_count += size;
    return lfs_rambd_read(&bd->cfg->ram_cfg, block, off, buffer, size);
}

static int lfs_testbd_rawprog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size) {
    lfs_testbd_t *bd = cfg->context;
    int do_powerfail = 0;
    int rc;

    handle_powerfail(bd);
    lfs_size_t nsize = size;
    if (bd->powerfail_after > 0 && (bd->powerfail_after >> 5) < (int) nsize) {
      nsize = bd->powerfail_after >> 5;
      do_powerfail = 1;
      printf("\nPowerfail during write of %d bytes at offset 0x%x in block %d. Wrote %d bytes.\n",
             size, off, block, nsize);
    }
    rc = lfs_rambd_prog(&bd->cfg->ram_cfg, block, off, buffer, nsize);
    if (rc) {
      return rc;
    }
    bd->powerfail_after -= nsize << 5;
    off += nsize;
    size -= nsize;
    bd->stats.prog_count += nsize;

    if (size && bd->powerfail_after) {
        // need to do a few bits in the last byte
        uint8_t last_byte = ((uint8_t *) buffer)[nsize] | (7 * bd->powerfail_after);
        bd->powerfail_after = 0;
        do_powerfail = 1;
        if (bd->partial_byte_writes) {
            printf("Byte at offset 0x%x should have been 0x%02x, but wrote 0x%02x instead.\n",
                   off, ((uint8_t *) buffer)[nsize], last_byte);
            rc = lfs_rambd_prog(&bd->cfg->ram_cfg, block, off, &last_byte, 1);
            if (rc) {
              return rc;
            }
        }
    }
    if (do_powerfail) {
        longjmp(bd->powerfail, 1);
    }

    return 0;
}

static int lfs_testbd_rawerase(const struct lfs_config *cfg,
        lfs_block_t block) {
    lfs_testbd_t *bd = cfg->context;
    handle_powerfail(bd);
    bd->stats.erase_count++;
    return lfs_rambd_erase(&bd->cfg->ram_cfg, block);
}

static int lfs_testbd_rawsync(const struct lfs_config *cfg) {
    lfs_testbd_t *bd = cfg->context;
    handle_powerfail(bd);
    return lfs_rambd_sync(&bd->cfg->ram_cfg);
}

/// block device API ///
int lfs_testbd_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size) {
    LFS_TRACE("lfs_testbd_read(%p, 0x%"PRIx32", %"PRIu32", %p, %"PRIu32")",
            (void*)cfg, block, off, buffer, size);
    lfs_testbd_t *bd = cfg->context;

    // check if read is valid
    LFS_ASSERT(off  % cfg->read_size == 0);
    LFS_ASSERT(size % cfg->read_size == 0);
    LFS_ASSERT(block < cfg->block_count);

    // block bad?
    if (bd->cfg->erase_cycles && bd->wear[block] >= bd->cfg->erase_cycles &&
            bd->cfg->badblock_behavior == LFS_TESTBD_BADBLOCK_READERROR) {
        LFS_TRACE("lfs_testbd_read -> %d", LFS_ERR_CORRUPT);
        return LFS_ERR_CORRUPT;
    }

    // read
    int err = lfs_testbd_rawread(cfg, block, off, buffer, size);
    LFS_TRACE("lfs_testbd_read -> %d", err);
    return err;
}

int lfs_testbd_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size) {
    LFS_TRACE("lfs_testbd_prog(%p, 0x%"PRIx32", %"PRIu32", %p, %"PRIu32")",
            (void*)cfg, block, off, buffer, size);
    lfs_testbd_t *bd = cfg->context;

    // check if write is valid
    LFS_ASSERT(off  % cfg->prog_size == 0);
    LFS_ASSERT(size % cfg->prog_size == 0);
    LFS_ASSERT(block < cfg->block_count);

    // block bad?
    if (bd->cfg->erase_cycles && bd->wear[block] >= bd->cfg->erase_cycles) {
        if (bd->cfg->badblock_behavior ==
                LFS_TESTBD_BADBLOCK_PROGERROR) {
            LFS_TRACE("lfs_testbd_prog -> %d", LFS_ERR_CORRUPT);
            return LFS_ERR_CORRUPT;
        } else if (bd->cfg->badblock_behavior ==
                LFS_TESTBD_BADBLOCK_PROGNOOP ||
                bd->cfg->badblock_behavior ==
                LFS_TESTBD_BADBLOCK_ERASENOOP) {
            LFS_TRACE("lfs_testbd_prog -> %d", 0);
            return 0;
        }
    }

    // prog
    int err = lfs_testbd_rawprog(cfg, block, off, buffer, size);
    if (err) {
        LFS_TRACE("lfs_testbd_prog -> %d", err);
        return err;
    }

    // lose power?
    if (bd->power_cycles > 0) {
        bd->power_cycles -= 1;
        if (bd->power_cycles == 0) {
            // sync to make sure we persist the last changes
            assert(lfs_testbd_rawsync(cfg) == 0);
            // simulate power loss
            exit(33);
        }
    }

    LFS_TRACE("lfs_testbd_prog -> %d", 0);
    return 0;
}

int lfs_testbd_erase(const struct lfs_config *cfg, lfs_block_t block) {
    LFS_TRACE("lfs_testbd_erase(%p, 0x%"PRIx32")", (void*)cfg, block);
    lfs_testbd_t *bd = cfg->context;

    // check if erase is valid
    LFS_ASSERT(block < cfg->block_count);

    // block bad?
    if (bd->cfg->erase_cycles) {
        if (bd->wear[block] >= bd->cfg->erase_cycles) {
            if (bd->cfg->badblock_behavior ==
                    LFS_TESTBD_BADBLOCK_ERASEERROR) {
                LFS_TRACE("lfs_testbd_erase -> %d", LFS_ERR_CORRUPT);
                return LFS_ERR_CORRUPT;
            } else if (bd->cfg->badblock_behavior ==
                    LFS_TESTBD_BADBLOCK_ERASENOOP) {
                LFS_TRACE("lfs_testbd_erase -> %d", 0);
                return 0;
            }
        } else {
            // mark wear
            bd->wear[block] += 1;
        }
    }

    // erase
    int err = lfs_testbd_rawerase(cfg, block);
    if (err) {
        LFS_TRACE("lfs_testbd_erase -> %d", err);
        return err;
    }

    // lose power?
    if (bd->power_cycles > 0) {
        bd->power_cycles -= 1;
        if (bd->power_cycles == 0) {
            // sync to make sure we persist the last changes
            assert(lfs_testbd_rawsync(cfg) == 0);
            // simulate power loss
            exit(33);
        }
    }

    LFS_TRACE("lfs_testbd_prog -> %d", 0);
    return 0;
}

int lfs_testbd_sync(const struct lfs_config *cfg) {
    LFS_TRACE("lfs_testbd_sync(%p)", (void*)cfg);
    int err = lfs_testbd_rawsync(cfg);
    LFS_TRACE("lfs_testbd_sync -> %d", err);
    return err;
}


/// simulated wear operations ///
lfs_testbd_swear_t lfs_testbd_getwear(const struct lfs_config *cfg,
        lfs_block_t block) {
    LFS_TRACE("lfs_testbd_getwear(%p, %"PRIu32")", (void*)cfg, block);
    lfs_testbd_t *bd = cfg->context;

    // check if block is valid
    LFS_ASSERT(bd->cfg->erase_cycles);
    LFS_ASSERT(block < cfg->block_count);

    LFS_TRACE("lfs_testbd_getwear -> %"PRIu32, bd->wear[block]);
    return bd->wear[block];
}

int lfs_testbd_setwear(const struct lfs_config *cfg,
        lfs_block_t block, lfs_testbd_wear_t wear) {
    LFS_TRACE("lfs_testbd_setwear(%p, %"PRIu32")", (void*)cfg, block);
    lfs_testbd_t *bd = cfg->context;

    // check if block is valid
    LFS_ASSERT(bd->cfg->erase_cycles);
    LFS_ASSERT(block < cfg->block_count);

    bd->wear[block] = wear;

    LFS_TRACE("lfs_testbd_setwear -> %d", 0);
    return 0;
}

void lfs_testbd_setpowerfail(const struct lfs_config *cfg, int powerfail_after, bool partial_byte_writes, jmp_buf powerfail) {
    LFS_TRACE("lfs_testbd_setpowerfail(%p, %"PRIu32")", (void*)cfg, powerfail_after);
    lfs_testbd_t *bd = cfg->context;

    bd->powerfail_after = powerfail_after;
    bd->partial_byte_writes = partial_byte_writes;
    memcpy(bd->powerfail, powerfail, sizeof(jmp_buf));
}
