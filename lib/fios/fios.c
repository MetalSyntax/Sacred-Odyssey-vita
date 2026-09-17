/*
 * fios.c -- use FIOS2 for optimized I/O
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <malloc.h>

#include "fios.h"

#define MAX_PATH_LENGTH 256
#define RAMCACHEBLOCKSIZE (128 * 1024)
#define RAMCACHEBLOCKNUM 512
#define RAMCACHEBLOCKNUM_FALLBACK 256

static int64_t g_OpStorage[SCE_FIOS_OP_STORAGE_SIZE(128, MAX_PATH_LENGTH) / sizeof(int64_t) + 1];
static int64_t g_ChunkStorage[SCE_FIOS_CHUNK_STORAGE_SIZE(2048) / sizeof(int64_t) + 1];
static int64_t g_FHStorage[SCE_FIOS_FH_STORAGE_SIZE(2048, MAX_PATH_LENGTH) / sizeof(int64_t) + 1];
static int64_t g_DHStorage[SCE_FIOS_DH_STORAGE_SIZE(64, MAX_PATH_LENGTH) / sizeof(int64_t) + 1];

static SceFiosRamCacheContext g_RamCacheContext = SCE_FIOS_RAM_CACHE_CONTEXT_INITIALIZER;
static char *g_RamCacheWorkBuffer;

int fios_init(const char * path) {
    int res;

    SceFiosParams params = SCE_FIOS_PARAMS_INITIALIZER;
    params.opStorage.pPtr = g_OpStorage;
    params.opStorage.length = sizeof(g_OpStorage);
    params.chunkStorage.pPtr = g_ChunkStorage;
    params.chunkStorage.length = sizeof(g_ChunkStorage);
    params.fhStorage.pPtr = g_FHStorage;
    params.fhStorage.length = sizeof(g_FHStorage);
    params.dhStorage.pPtr = g_DHStorage;
    params.dhStorage.length = sizeof(g_DHStorage);
    params.pathMax = MAX_PATH_LENGTH;
    params.maxChunk = 2048;

    params.threadAffinity[SCE_FIOS_IO_THREAD] = 0x20000;
    params.threadAffinity[SCE_FIOS_CALLBACK_THREAD] = 0;
    params.threadAffinity[SCE_FIOS_DECOMPRESSOR_THREAD] = 0;

    params.threadPriority[SCE_FIOS_IO_THREAD] = 64;
    params.threadPriority[SCE_FIOS_CALLBACK_THREAD] = 191;
    params.threadPriority[SCE_FIOS_DECOMPRESSOR_THREAD] = 191;

    res = sceFiosInitialize(&params);
    if (res < 0)
        return res;

    size_t block_num = RAMCACHEBLOCKNUM;
    g_RamCacheWorkBuffer = memalign(8, block_num * RAMCACHEBLOCKSIZE);
    if (!g_RamCacheWorkBuffer) {
        block_num = RAMCACHEBLOCKNUM_FALLBACK;
        g_RamCacheWorkBuffer = memalign(8, block_num * RAMCACHEBLOCKSIZE);
    }
    if (!g_RamCacheWorkBuffer)
        return -1;

    g_RamCacheContext.pPath = path;
    g_RamCacheContext.pWorkBuffer = g_RamCacheWorkBuffer;
    g_RamCacheContext.workBufferSize = block_num * RAMCACHEBLOCKSIZE;
    g_RamCacheContext.blockSize = RAMCACHEBLOCKSIZE;
    res = sceFiosIOFilterAdd(0, sceFiosIOFilterCache, &g_RamCacheContext);
    if (res < 0)
        return res;

    return 0;
}

void fios_terminate(void) {
    sceFiosTerminate();
    free(g_RamCacheWorkBuffer);
}
