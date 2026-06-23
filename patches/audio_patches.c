#include "patches.h"
#include "audio/heap.h"
#include "audio/load.h"
#include "audio/soundfont.h"
#include "buffers.h"
#include "input.h"

extern NoteSampleState gZeroedSampleState;

#define AUDIO_DIAG_MAX_SAMPLE_DMAS 0x100
#define AUDIO_DIAG_MAX_FALLBACK_FONTS 0x40

static s32 AudioDiag_LogAllocCount = 0;
static f32 sAudioDiagAdsrDecayTable[0x100] = { 1.0f };
static SampleDma sAudioDiagSampleDmas[AUDIO_DIAG_MAX_SAMPLE_DMAS] = { { (u8*)1, 0, 0, 0, 0, 0, 0 } };
static s16 sAudioDiagAiBuffers[3][AIBUF_LEN];
static SoundFont sAudioDiagSoundFontList[AUDIO_DIAG_MAX_FALLBACK_FONTS];
static OSPiHandle sAudioDiagCartHandle;

static s32 AudioDiag_PtrInPool(void* ptr, size_t size, AudioAllocPool* pool) {
    uintptr_t start;
    uintptr_t end;
    uintptr_t value;

    if (ptr == NULL || pool->startAddr == NULL) {
        return false;
    }

    start = (uintptr_t)pool->startAddr;
    end = start + pool->size;
    value = (uintptr_t)ptr;

    return value >= start && value <= end && size <= (end - value);
}

static s32 AudioDiag_PtrInAudioPool(void* ptr, size_t size) {
    return AudioDiag_PtrInPool(ptr, size, &gAudioCtx.miscPool) ||
           AudioDiag_PtrInPool(ptr, size, &gAudioCtx.externalPool);
}

static s32 AudioDiag_PoolLooksValid(AudioAllocPool* pool) {
    uintptr_t start;
    uintptr_t cur;
    uintptr_t end;

    if (pool == NULL || pool->startAddr == NULL || pool->curAddr == NULL) {
        return false;
    }

    start = (uintptr_t)pool->startAddr;
    cur = (uintptr_t)pool->curAddr;
    end = start + pool->size;

    if (end < start || cur < start || cur > end) {
        return false;
    }

    return true;
}

void AudioHeap_InitReverb(s32 reverbIndex, ReverbSettings* settings, s32 isFirstInit);
void* AudioHeap_AllocZeroedAttemptExternal(AudioAllocPool* pool, size_t size);
void* AudioHeap_AllocDmaMemoryZeroed(AudioAllocPool* pool, size_t size);
void AudioHeap_LoadLowPassFilter(s16* filter, s32 cutoff);
void AudioHeap_DiscardSampleCacheEntry(SampleCacheEntry* entry);
void AudioHeap_DiscardSequence(s32 seqId);
void AudioHeap_DiscardSampleBank(s32 sampleBankId);
void AudioLoad_InitTable(AudioTable* table, uintptr_t romAddr, u16 unkMediumParam);
void AudioLoad_InitSoundFont(s32 fontId);

RECOMP_PATCH void AudioLoad_Init(void* heap, size_t heapSize) {
    s32 pad1[9];
    s32 numFonts;
    s32 pad2[2];
    u8* audioCtxPtr;
    void* addr;
    s32 i;
    s32 j;

    gAudioCustomUpdateFunction = NULL;
    gAudioCustomReverbFunction = NULL;
    gAudioCustomSynthFunction = NULL;

    for (i = 0; i < ARRAY_COUNT(gAudioCtx.customSeqFunctions); i++) {
        gAudioCtx.customSeqFunctions[i] = NULL;
    }

    gAudioCtx.resetTimer = 0;
    gAudioCtx.unk_29B8 = false;

    audioCtxPtr = (u8*)&gAudioCtx;
    for (j = sizeof(gAudioCtx); j > 0; j--) {
        *audioCtxPtr++ = 0;
    }

    switch (osTvType) {
        case OS_TV_PAL:
            gAudioCtx.unk_2960 = 20.03042f;
            gAudioCtx.refreshRate = 50;
            break;

        case OS_TV_MPAL:
            gAudioCtx.unk_2960 = 16.546f;
            gAudioCtx.refreshRate = 60;
            break;

        case OS_TV_NTSC:
        default:
            gAudioCtx.unk_2960 = 16.713f;
            gAudioCtx.refreshRate = 60;
    }

    AudioThread_InitMesgQueues();

    for (i = 0; i < ARRAY_COUNT(gAudioCtx.numSamplesPerFrame); i++) {
        gAudioCtx.numSamplesPerFrame[i] = 0xA0;
    }

    gAudioCtx.totalTaskCount = 0;
    gAudioCtx.rspTaskIndex = 0;
    gAudioCtx.curAiBufferIndex = 0;
    gAudioCtx.soundMode = SOUNDMODE_STEREO;
    gAudioCtx.curTask = NULL;
    gAudioCtx.rspTask[0].task.t.dataSize = 0;
    gAudioCtx.rspTask[1].task.t.dataSize = 0;

    osCreateMesgQueue(&gAudioCtx.syncDmaQueue, &gAudioCtx.syncDmaMesg, 1);
    osCreateMesgQueue(&gAudioCtx.curAudioFrameDmaQueue, gAudioCtx.currAudioFrameDmaMesgBuf,
                      ARRAY_COUNT(gAudioCtx.currAudioFrameDmaMesgBuf));
    osCreateMesgQueue(&gAudioCtx.externalLoadQueue, gAudioCtx.externalLoadMesgBuf,
                      ARRAY_COUNT(gAudioCtx.externalLoadMesgBuf));
    osCreateMesgQueue(&gAudioCtx.preloadSampleQueue, gAudioCtx.preloadSampleMesgBuf,
                      ARRAY_COUNT(gAudioCtx.preloadSampleMesgBuf));
    gAudioCtx.curAudioFrameDmaCount = 0;
    gAudioCtx.sampleDmaCount = 0;
    gAudioCtx.cartHandle = &sAudioDiagCartHandle;

    if (heap == NULL) {
        gAudioCtx.audioHeap = gAudioHeap;
        gAudioCtx.audioHeapSize = gAudioHeapInitSizes.heapSize;
    } else {
        void** hp = &heap;

        gAudioCtx.audioHeap = *hp;
        gAudioCtx.audioHeapSize = heapSize;
    }

    if (gAudioCtx.audioHeap == NULL || gAudioCtx.audioHeapSize == 0) {
        recomp_printf("[AudioDiag] AudioLoad_Init invalid heap=%p size=%d, using bundled heap\n",
                      gAudioCtx.audioHeap, gAudioCtx.audioHeapSize);
        gAudioCtx.audioHeap = gAudioHeap;
        gAudioCtx.audioHeapSize = gAudioHeapInitSizes.heapSize;
    }

    for (i = 0; i < ((s32)gAudioCtx.audioHeapSize / (s32)sizeof(u64)); i++) {
        ((u64*)gAudioCtx.audioHeap)[i] = 0;
    }

    AudioHeap_InitMainPool(gAudioHeapInitSizes.initPoolSize);

    for (i = 0; i < ARRAY_COUNT(gAudioCtx.aiBuffers); i++) {
        gAudioCtx.aiBuffers[i] = AudioHeap_AllocZeroed(&gAudioCtx.initPool, AIBUF_LEN * sizeof(s16));
        if (gAudioCtx.aiBuffers[i] == NULL) {
            recomp_printf("[AudioDiag] AudioLoad_Init using static AI buffer index=%d\n", i);
            bzero(sAudioDiagAiBuffers[i], sizeof(sAudioDiagAiBuffers[i]));
            gAudioCtx.aiBuffers[i] = sAudioDiagAiBuffers[i];
        }
    }

    gAudioCtx.sequenceTable = (AudioTable*)gSequenceTable;
    gAudioCtx.soundFontTable = (AudioTable*)gSoundFontTable;
    gAudioCtx.sampleBankTable = (AudioTable*)gSampleBankTable;
    gAudioCtx.sequenceFontTable = gSequenceFontTable;

    gAudioCtx.numSequences = gAudioCtx.sequenceTable->numEntries;

    gAudioCtx.specId = 0;
    gAudioCtx.resetStatus = 1;
    AudioHeap_ResetStep();

    AudioLoad_InitTable(gAudioCtx.sequenceTable, SEGMENT_ROM_START(Audioseq), 0);
    AudioLoad_InitTable(gAudioCtx.soundFontTable, SEGMENT_ROM_START(Audiobank), 0);
    AudioLoad_InitTable(gAudioCtx.sampleBankTable, SEGMENT_ROM_START(Audiotable), 0);

    numFonts = gAudioCtx.soundFontTable->numEntries;
    gAudioCtx.soundFontList = AudioHeap_Alloc(&gAudioCtx.initPool, numFonts * sizeof(SoundFont));
    if (gAudioCtx.soundFontList == NULL) {
        if (numFonts > AUDIO_DIAG_MAX_FALLBACK_FONTS) {
            recomp_printf("[AudioDiag] AudioLoad_Init too many fonts for fallback count=%d max=%d\n",
                          numFonts, AUDIO_DIAG_MAX_FALLBACK_FONTS);
            numFonts = AUDIO_DIAG_MAX_FALLBACK_FONTS;
        }
        bzero(sAudioDiagSoundFontList, sizeof(sAudioDiagSoundFontList));
        gAudioCtx.soundFontList = sAudioDiagSoundFontList;
        recomp_printf("[AudioDiag] AudioLoad_Init using static soundFontList count=%d\n", numFonts);
    }

    for (i = 0; i < numFonts; i++) {
        AudioLoad_InitSoundFont(i);
    }

    addr = AudioHeap_Alloc(&gAudioCtx.initPool, gAudioHeapInitSizes.permanentPoolSize);
    if (addr == NULL) {
        *((u32*)&gAudioHeapInitSizes.permanentPoolSize) = 0;
    }

    AudioHeap_InitPool(&gAudioCtx.permanentPool, addr, gAudioHeapInitSizes.permanentPoolSize);
    gAudioCtxInitalized = true;
    osSendMesg(gAudioCtx.taskStartQueueP, (void*)gAudioCtx.totalTaskCount, OS_MESG_NOBLOCK);
}

RECOMP_PATCH void AudioHeap_InitPool(AudioAllocPool* pool, void* addr, size_t size) {
    uintptr_t alignedAddr = ALIGN16((uintptr_t)addr);
    size_t alignmentLoss = (uintptr_t)addr & 0xF;

    if (addr == NULL || size == 0) {
        pool->curAddr = pool->startAddr = NULL;
        pool->size = 0;
        pool->count = 0;

        if (AudioDiag_LogAllocCount < 16) {
            recomp_printf("[AudioDiag] InitPool empty pool=%p addr=%p size=%d\n", pool, addr, size);
        }
        return;
    }

    pool->curAddr = pool->startAddr = (u8*)alignedAddr;
    pool->size = size > alignmentLoss ? size - alignmentLoss : 0;
    pool->count = 0;

    if (AudioDiag_LogAllocCount < 16) {
        recomp_printf("[AudioDiag] InitPool pool=%p addr=%p aligned=%p size=%d storedSize=%d\n",
                      pool, addr, pool->startAddr, size, pool->size);
    }
}

RECOMP_PATCH void* AudioHeap_Alloc(AudioAllocPool* pool, size_t size) {
    size_t alignedSize = ALIGN16(size);
    u8* curAddr;
    uintptr_t cur;
    uintptr_t end;

    if (!AudioDiag_PoolLooksValid(pool)) {
        recomp_printf("[AudioDiag] Alloc invalid pool=%p start=%p cur=%p size=%d count=%d request=%d\n",
                      pool, pool != NULL ? pool->startAddr : NULL, pool != NULL ? pool->curAddr : NULL,
                      pool != NULL ? pool->size : 0, pool != NULL ? pool->count : 0, size);
        return NULL;
    }

    curAddr = pool->curAddr;
    cur = (uintptr_t)pool->curAddr;
    end = (uintptr_t)pool->startAddr + pool->size;

    if (alignedSize > (end - cur)) {
        recomp_printf("[AudioDiag] Alloc failed pool=%p start=%p cur=%p end=%p size=%d aligned=%d count=%d\n",
                      pool, pool->startAddr, pool->curAddr, (void*)end, size, alignedSize, pool->count);
        return NULL;
    }

    pool->curAddr += alignedSize;
    pool->count++;

    if (AudioDiag_LogAllocCount < 16) {
        recomp_printf("[AudioDiag] Alloc ok pool=%p addr=%p newCur=%p size=%d aligned=%d count=%d\n",
                      pool, curAddr, pool->curAddr, size, alignedSize, pool->count);
        AudioDiag_LogAllocCount++;
    }

    return curAddr;
}
RECOMP_PATCH void* AudioHeap_AllocZeroed(AudioAllocPool* pool, size_t size) {
    u8* addr = AudioHeap_Alloc(pool, size);
    uintptr_t zeroStart;
    uintptr_t zeroEnd;

    if (addr == NULL) {
        return NULL;
    }

    if (!AudioDiag_PoolLooksValid(pool)) {
        recomp_printf("[AudioDiag] AllocZeroed invalid after alloc pool=%p addr=%p start=%p cur=%p size=%d count=%d request=%d\n",
                      pool, addr, pool != NULL ? pool->startAddr : NULL, pool != NULL ? pool->curAddr : NULL,
                      pool != NULL ? pool->size : 0, pool != NULL ? pool->count : 0, size);
        return NULL;
    }

    zeroStart = (uintptr_t)addr;
    zeroEnd = (uintptr_t)pool->curAddr;
    if (zeroEnd < zeroStart || !AudioDiag_PtrInPool(addr, zeroEnd - zeroStart, pool)) {
        recomp_printf("[AudioDiag] AllocZeroed invalid span pool=%p addr=%p cur=%p start=%p poolSize=%d request=%d\n",
                      pool, addr, pool->curAddr, pool->startAddr, pool->size, size);
        return NULL;
    }

    bzero(addr, zeroEnd - zeroStart);
    return addr;
}

RECOMP_PATCH void* AudioHeap_AllocPermanent(s32 tableType, s32 id, size_t size) {
    void* addr;
    s32 index = gAudioCtx.permanentPool.count;

    if (index < 0 || index >= ARRAY_COUNT(gAudioCtx.permanentEntries)) {
        recomp_printf("[AudioDiag] AllocPermanent full table=%d id=%d size=%d count=%d\n",
                      tableType, id, size, index);
        return NULL;
    }

    addr = AudioHeap_Alloc(&gAudioCtx.permanentPool, size);
    if (addr == NULL) {
        return NULL;
    }

    gAudioCtx.permanentEntries[index].addr = addr;
    gAudioCtx.permanentEntries[index].tableType = tableType;
    gAudioCtx.permanentEntries[index].id = id;
    gAudioCtx.permanentEntries[index].size = size;
    return addr;
}

RECOMP_PATCH SampleCacheEntry* AudioHeap_AllocTemporarySampleCacheEntry(size_t size) {
    s32 pad2[2];
    void* addr;
    s32 pad3[2];
    u8* allocAfter;
    u8* allocBefore;
    s32 pad1;
    s32 index;
    s32 i;
    SampleCacheEntry* entry;
    AudioPreloadReq* preload;
    AudioSampleCache* cache;
    u8* startAddr;
    u8* endAddr;

    cache = &gAudioCtx.temporarySampleCache;
    if (!AudioDiag_PoolLooksValid(&cache->pool)) {
        recomp_printf("[AudioDiag] TemporarySampleCache invalid pool start=%p cur=%p size=%d entries=%d request=%d\n",
                      cache->pool.startAddr, cache->pool.curAddr, cache->pool.size, cache->numEntries, size);
        return NULL;
    }

    allocBefore = cache->pool.curAddr;
    addr = AudioHeap_Alloc(&cache->pool, size);
    if (addr == NULL) {
        u8* oldAddr = cache->pool.curAddr;

        cache->pool.curAddr = cache->pool.startAddr;
        addr = AudioHeap_Alloc(&cache->pool, size);
        if (addr == NULL) {
            cache->pool.curAddr = oldAddr;
            return NULL;
        }
        allocBefore = cache->pool.startAddr;
    }

    allocAfter = cache->pool.curAddr;

    index = -1;
    for (i = 0; i < gAudioCtx.preloadSampleStackTop; i++) {
        preload = &gAudioCtx.preloadSampleStack[i];
        if (preload->isFree == false) {
            if (preload->ramAddr == NULL || preload->sample == NULL) {
                preload->isFree = true;
                continue;
            }

            startAddr = preload->ramAddr;
            endAddr = preload->ramAddr + preload->sample->size - 1;

            if ((endAddr < allocBefore) && (startAddr < allocBefore)) {
                continue;
            }
            if ((endAddr >= allocAfter) && (startAddr >= allocAfter)) {
                continue;
            }

            preload->isFree = true;
        }
    }

    for (i = 0; i < cache->numEntries; i++) {
        if (!cache->entries[i].inUse) {
            continue;
        }

        startAddr = cache->entries[i].allocatedAddr;
        if (startAddr == NULL) {
            cache->entries[i].inUse = false;
            if (index == -1) {
                index = i;
            }
            continue;
        }

        endAddr = startAddr + cache->entries[i].size - 1;

        if ((endAddr < allocBefore) && (startAddr < allocBefore)) {
            continue;
        }
        if ((endAddr >= allocAfter) && (startAddr >= allocAfter)) {
            continue;
        }

        AudioHeap_DiscardSampleCacheEntry(&cache->entries[i]);
        cache->entries[i].inUse = false;
        if (index == -1) {
            index = i;
        }
    }

    if (index == -1) {
        for (i = 0; i < cache->numEntries; i++) {
            if (!cache->entries[i].inUse) {
                break;
            }
        }

        index = i;
        if (index == cache->numEntries) {
            if (cache->numEntries == ARRAY_COUNT(cache->entries)) {
                return NULL;
            }
            cache->numEntries++;
        }
    }

    entry = &cache->entries[index];
    entry->inUse = true;
    entry->allocatedAddr = addr;
    entry->size = size;

    return entry;
}

RECOMP_PATCH SampleCacheEntry* AudioHeap_AllocPersistentSampleCacheEntry(size_t size) {
    AudioSampleCache* cache;
    SampleCacheEntry* entry;
    void* addr;

    cache = &gAudioCtx.persistentSampleCache;
    if (cache->numEntries == ARRAY_COUNT(cache->entries)) {
        recomp_printf("[AudioDiag] PersistentSampleCache full request=%d\n", size);
        return NULL;
    }

    if (!AudioDiag_PoolLooksValid(&cache->pool)) {
        recomp_printf("[AudioDiag] PersistentSampleCache invalid pool start=%p cur=%p size=%d entries=%d request=%d\n",
                      cache->pool.startAddr, cache->pool.curAddr, cache->pool.size, cache->numEntries, size);
        return NULL;
    }

    addr = AudioHeap_Alloc(&cache->pool, size);
    if (addr == NULL) {
        return NULL;
    }

    entry = &cache->entries[cache->numEntries];
    entry->inUse = true;
    entry->allocatedAddr = addr;
    entry->size = size;
    cache->numEntries++;

    return entry;
}

static f32 AudioDiag_CalculateAdsrDecay(f32 scaleInv) {
    return 256.0f * gAudioCtx.audioBufferParameters.updatesPerFrameInvScaled / scaleInv;
}

RECOMP_PATCH void AudioHeap_InitAdsrDecayTable(void) {
    f32* table = gAudioCtx.adsrDecayTable;
    s32 useStaticTable = recomp_android_should_use_sync_boot_dma();
    s32 i;

    if (!AudioDiag_PtrInPool(table, sizeof(sAudioDiagAdsrDecayTable), &gAudioCtx.miscPool)) {
        useStaticTable = true;
    }

    if (useStaticTable) {
        recomp_printf("[AudioDiag] Using static ADSR table old=%p static=%p miscStart=%p miscCur=%p miscSize=%d syncBootDma=%d\n",
                      table, sAudioDiagAdsrDecayTable, gAudioCtx.miscPool.startAddr, gAudioCtx.miscPool.curAddr,
                      gAudioCtx.miscPool.size, recomp_android_should_use_sync_boot_dma());
        table = sAudioDiagAdsrDecayTable;
        gAudioCtx.adsrDecayTable = table;
    }

    table[255] = AudioDiag_CalculateAdsrDecay(0.25f);
    table[254] = AudioDiag_CalculateAdsrDecay(0.33f);
    table[253] = AudioDiag_CalculateAdsrDecay(0.5f);
    table[252] = AudioDiag_CalculateAdsrDecay(0.66f);
    table[251] = AudioDiag_CalculateAdsrDecay(0.75f);

    for (i = 128; i < 251; i++) {
        table[i] = AudioDiag_CalculateAdsrDecay(251 - i);
    }

    for (i = 16; i < 128; i++) {
        table[i] = AudioDiag_CalculateAdsrDecay(4 * (143 - i));
    }

    for (i = 1; i < 16; i++) {
        table[i] = AudioDiag_CalculateAdsrDecay(60 * (23 - i));
    }

    table[0] = 0.0f;
}

RECOMP_PATCH void AudioPlayback_InitNoteFreeList(void) {
    Note* note;
    s32 i;

    recomp_printf("[AudioDiag] InitNoteFreeList begin notes=%p numNotes=%d miscStart=%p miscCur=%p miscSize=%d\n",
                  gAudioCtx.notes, gAudioCtx.numNotes, gAudioCtx.miscPool.startAddr, gAudioCtx.miscPool.curAddr,
                  gAudioCtx.miscPool.size);

    AudioPlayback_InitNoteLists(&gAudioCtx.noteFreeLists);

    if (!AudioDiag_PtrInPool(gAudioCtx.notes, gAudioCtx.numNotes * sizeof(Note), &gAudioCtx.miscPool)) {
        recomp_printf("[AudioDiag] InitNoteFreeList invalid notes allocation notes=%p bytes=%d miscStart=%p miscSize=%d\n",
                      gAudioCtx.notes, gAudioCtx.numNotes * sizeof(Note), gAudioCtx.miscPool.startAddr,
                      gAudioCtx.miscPool.size);
        return;
    }

    for (i = 0; i < gAudioCtx.numNotes; i++) {
        note = &gAudioCtx.notes[i];

        if (i < 4 || i == (gAudioCtx.numNotes - 1)) {
            recomp_printf("[AudioDiag] InitNoteFreeList note=%d ptr=%p item=%p listPrev=%p listNext=%p\n", i, note,
                          &note->listItem, gAudioCtx.noteFreeLists.disabled.prev,
                          gAudioCtx.noteFreeLists.disabled.next);
        }

        note->listItem.u.value = note;
        note->listItem.prev = NULL;
        AudioScript_AudioListPushBack(&gAudioCtx.noteFreeLists.disabled, &note->listItem);
    }

    recomp_printf("[AudioDiag] InitNoteFreeList end count=%d prev=%p next=%p\n",
                  gAudioCtx.noteFreeLists.disabled.u.count, gAudioCtx.noteFreeLists.disabled.prev,
                  gAudioCtx.noteFreeLists.disabled.next);
}

RECOMP_PATCH void AudioPlayback_NoteInitAll(void) {
    Note* note;
    s32 i;

    recomp_printf("[AudioDiag] NoteInitAll begin notes=%p numNotes=%d miscStart=%p miscCur=%p miscSize=%d miscCount=%d\n",
                  gAudioCtx.notes, gAudioCtx.numNotes, gAudioCtx.miscPool.startAddr, gAudioCtx.miscPool.curAddr,
                  gAudioCtx.miscPool.size, gAudioCtx.miscPool.count);

    if (!AudioDiag_PtrInPool(gAudioCtx.notes, gAudioCtx.numNotes * sizeof(Note), &gAudioCtx.miscPool)) {
        recomp_printf("[AudioDiag] NoteInitAll invalid notes allocation notes=%p bytes=%d miscStart=%p miscSize=%d\n",
                      gAudioCtx.notes, gAudioCtx.numNotes * sizeof(Note), gAudioCtx.miscPool.startAddr,
                      gAudioCtx.miscPool.size);
        return;
    }

    for (i = 0; i < gAudioCtx.numNotes; i++) {
        note = &gAudioCtx.notes[i];

        if (i < 4 || i == (gAudioCtx.numNotes - 1)) {
            recomp_printf("[AudioDiag] NoteInitAll note=%d ptr=%p\n", i, note);
        }

        note->sampleState = gZeroedSampleState;
        note->playbackState.priority = 0;
        note->playbackState.status = PLAYBACK_STATUS_0;
        note->playbackState.parentLayer = NO_LAYER;
        note->playbackState.wantedParentLayer = NO_LAYER;
        note->playbackState.prevParentLayer = NO_LAYER;
        note->playbackState.waveId = 0;
        note->playbackState.attributes.velocity = 0.0f;
        note->playbackState.adsrVolScaleUnused = 0;
        note->playbackState.adsr.action.asByte = 0;
        note->playbackState.vibratoState.active = false;
        note->playbackState.portamento.cur = 0;
        note->playbackState.portamento.speed = 0;
        note->playbackState.stereoHeadsetEffects = false;
        note->playbackState.startSamplePos = 0;
        note->synthesisState.synthesisBuffers =
            AudioHeap_AllocDmaMemory(&gAudioCtx.miscPool, sizeof(NoteSynthesisBuffers));
        note->playbackState.attributes.filterBuf = AudioHeap_AllocDmaMemory(&gAudioCtx.miscPool, FILTER_SIZE);

        if (note->synthesisState.synthesisBuffers == NULL || note->playbackState.attributes.filterBuf == NULL) {
            recomp_printf("[AudioDiag] NoteInitAll alloc failed note=%d synth=%p filter=%p miscCur=%p miscSize=%d miscCount=%d\n",
                          i, note->synthesisState.synthesisBuffers, note->playbackState.attributes.filterBuf,
                          gAudioCtx.miscPool.curAddr, gAudioCtx.miscPool.size, gAudioCtx.miscPool.count);
            return;
        }
    }

    recomp_printf("[AudioDiag] NoteInitAll end miscCur=%p miscCount=%d\n", gAudioCtx.miscPool.curAddr,
                  gAudioCtx.miscPool.count);
}

static void AudioDiag_DisableReverb(SynthesisReverb* reverb, s32 reverbIndex, const char* reason) {
    recomp_printf("[AudioDiag] Disable reverb index=%d reason=%s delay=%d left=%p right=%p miscStart=%p miscCur=%p miscSize=%d externalStart=%p externalCur=%p externalSize=%d\n",
                  reverbIndex, reason, reverb != NULL ? reverb->delayNumSamples : 0,
                  reverb != NULL ? reverb->leftReverbBuf : NULL,
                  reverb != NULL ? reverb->rightReverbBuf : NULL,
                  gAudioCtx.miscPool.startAddr, gAudioCtx.miscPool.curAddr, gAudioCtx.miscPool.size,
                  gAudioCtx.externalPool.startAddr, gAudioCtx.externalPool.curAddr, gAudioCtx.externalPool.size);

    if (reverb == NULL) {
        return;
    }

    reverb->useReverb = 0;
    reverb->leftReverbBuf = NULL;
    reverb->rightReverbBuf = NULL;
    reverb->sample.sampleAddr = NULL;
    reverb->filterLeft = NULL;
    reverb->filterRight = NULL;
}

RECOMP_PATCH void AudioHeap_SetReverbData(s32 reverbIndex, u32 dataType, s32 data, s32 isFirstInit) {
    s32 delayNumSamples;
    SynthesisReverb* reverb;
    size_t filterStateSize;

    if (reverbIndex < 0 || reverbIndex >= ARRAY_COUNT(gAudioCtx.synthesisReverbs)) {
        recomp_printf("[AudioDiag] SetReverbData invalid index=%d type=%d data=%d first=%d\n",
                      reverbIndex, dataType, data, isFirstInit);
        return;
    }

    reverb = &gAudioCtx.synthesisReverbs[reverbIndex];

    switch (dataType) {
        case REVERB_DATA_TYPE_SETTINGS:
            AudioHeap_InitReverb(reverbIndex, (ReverbSettings*)data, false);
            break;

        case REVERB_DATA_TYPE_DELAY:
            if (reverb->downsampleRate == 0) {
                recomp_printf("[AudioDiag] Reverb delay had zero downsample index=%d data=%d\n", reverbIndex, data);
                reverb->downsampleRate = 1;
            }

            if (data < 4) {
                data = 4;
            }

            delayNumSamples = data * 64;
            if (delayNumSamples < (16 * SAMPLES_PER_FRAME)) {
                delayNumSamples = 16 * SAMPLES_PER_FRAME;
            }

            delayNumSamples /= reverb->downsampleRate;

            if (!isFirstInit) {
                if (reverb->delayNumSamplesAfterDownsampling < (data / reverb->downsampleRate)) {
                    break;
                }
                if ((reverb->nextReverbBufPos >= delayNumSamples) || (reverb->delayNumSamplesUnk >= delayNumSamples)) {
                    reverb->nextReverbBufPos = 0;
                    reverb->delayNumSamplesUnk = 0;
                }
            }

            reverb->delayNumSamples = delayNumSamples;

            if ((reverb->downsampleRate != 1) || reverb->resampleEffectOn) {
                reverb->downsamplePitch = 0x8000 / reverb->downsampleRate;
                if (reverb->leftLoadResampleBuf == NULL) {
                    reverb->leftLoadResampleBuf = AudioHeap_AllocZeroed(&gAudioCtx.miscPool, sizeof(RESAMPLE_STATE));
                    reverb->rightLoadResampleBuf = AudioHeap_AllocZeroed(&gAudioCtx.miscPool, sizeof(RESAMPLE_STATE));
                    reverb->leftSaveResampleBuf = AudioHeap_AllocZeroed(&gAudioCtx.miscPool, sizeof(RESAMPLE_STATE));
                    reverb->rightSaveResampleBuf = AudioHeap_AllocZeroed(&gAudioCtx.miscPool, sizeof(RESAMPLE_STATE));
                    if (!AudioDiag_PtrInPool(reverb->leftLoadResampleBuf, sizeof(RESAMPLE_STATE), &gAudioCtx.miscPool) ||
                        !AudioDiag_PtrInPool(reverb->rightLoadResampleBuf, sizeof(RESAMPLE_STATE), &gAudioCtx.miscPool) ||
                        !AudioDiag_PtrInPool(reverb->leftSaveResampleBuf, sizeof(RESAMPLE_STATE), &gAudioCtx.miscPool) ||
                        !AudioDiag_PtrInPool(reverb->rightSaveResampleBuf, sizeof(RESAMPLE_STATE), &gAudioCtx.miscPool)) {
                        recomp_printf("[AudioDiag] Reverb resample alloc invalid index=%d leftLoad=%p rightLoad=%p leftSave=%p rightSave=%p miscStart=%p miscCur=%p miscSize=%d\n",
                                      reverbIndex, reverb->leftLoadResampleBuf, reverb->rightLoadResampleBuf,
                                      reverb->leftSaveResampleBuf, reverb->rightSaveResampleBuf,
                                      gAudioCtx.miscPool.startAddr, gAudioCtx.miscPool.curAddr, gAudioCtx.miscPool.size);
                        reverb->leftLoadResampleBuf = NULL;
                        reverb->rightLoadResampleBuf = NULL;
                        reverb->leftSaveResampleBuf = NULL;
                        reverb->rightSaveResampleBuf = NULL;
                        reverb->downsampleRate = 1;
                    }
                }
            }
            break;

        case REVERB_DATA_TYPE_DECAY:
            reverb->decayRatio = data;
            break;

        case REVERB_DATA_TYPE_SUB_VOLUME:
            reverb->subVolume = data;
            break;

        case REVERB_DATA_TYPE_VOLUME:
            reverb->volume = data;
            break;

        case REVERB_DATA_TYPE_LEAK_RIGHT:
            reverb->leakRtl = data;
            break;

        case REVERB_DATA_TYPE_LEAK_LEFT:
            reverb->leakLtr = data;
            break;

        case REVERB_DATA_TYPE_FILTER_LEFT:
            if (data != 0) {
                if (isFirstInit || (reverb->filterLeftInit == NULL)) {
                    filterStateSize = 2 * (FILTER_BUF_PART1 + FILTER_BUF_PART2);
                    reverb->filterLeftState = AudioHeap_AllocDmaMemoryZeroed(&gAudioCtx.miscPool, filterStateSize);
                    reverb->filterLeftInit = AudioHeap_AllocDmaMemory(&gAudioCtx.miscPool, FILTER_SIZE);
                    if (!AudioDiag_PtrInPool(reverb->filterLeftState, filterStateSize, &gAudioCtx.miscPool) ||
                        !AudioDiag_PtrInPool(reverb->filterLeftInit, FILTER_SIZE, &gAudioCtx.miscPool)) {
                        recomp_printf("[AudioDiag] Reverb left filter alloc invalid index=%d state=%p init=%p miscStart=%p miscCur=%p miscSize=%d\n",
                                      reverbIndex, reverb->filterLeftState, reverb->filterLeftInit,
                                      gAudioCtx.miscPool.startAddr, gAudioCtx.miscPool.curAddr, gAudioCtx.miscPool.size);
                        reverb->filterLeftState = NULL;
                        reverb->filterLeftInit = NULL;
                    }
                }

                reverb->filterLeft = reverb->filterLeftInit;
                if (reverb->filterLeft != NULL) {
                    AudioHeap_LoadLowPassFilter(reverb->filterLeft, data);
                }
            } else {
                reverb->filterLeft = NULL;

                if (isFirstInit) {
                    reverb->filterLeftInit = NULL;
                }
            }
            break;

        case REVERB_DATA_TYPE_FILTER_RIGHT:
            if (data != 0) {
                if (isFirstInit || (reverb->filterRightInit == NULL)) {
                    filterStateSize = 2 * (FILTER_BUF_PART1 + FILTER_BUF_PART2);
                    reverb->filterRightState = AudioHeap_AllocDmaMemoryZeroed(&gAudioCtx.miscPool, filterStateSize);
                    reverb->filterRightInit = AudioHeap_AllocDmaMemory(&gAudioCtx.miscPool, FILTER_SIZE);
                    if (!AudioDiag_PtrInPool(reverb->filterRightState, filterStateSize, &gAudioCtx.miscPool) ||
                        !AudioDiag_PtrInPool(reverb->filterRightInit, FILTER_SIZE, &gAudioCtx.miscPool)) {
                        recomp_printf("[AudioDiag] Reverb right filter alloc invalid index=%d state=%p init=%p miscStart=%p miscCur=%p miscSize=%d\n",
                                      reverbIndex, reverb->filterRightState, reverb->filterRightInit,
                                      gAudioCtx.miscPool.startAddr, gAudioCtx.miscPool.curAddr, gAudioCtx.miscPool.size);
                        reverb->filterRightState = NULL;
                        reverb->filterRightInit = NULL;
                    }
                }

                reverb->filterRight = reverb->filterRightInit;
                if (reverb->filterRight != NULL) {
                    AudioHeap_LoadLowPassFilter(reverb->filterRight, data);
                }
            } else {
                reverb->filterRight = NULL;
                if (isFirstInit) {
                    reverb->filterRightInit = NULL;
                }
            }
            break;

        case REVERB_DATA_TYPE_9:
            reverb->resampleEffectExtraSamples = data;
            reverb->resampleEffectOn = data != 0;
            break;

        default:
            break;
    }
}

RECOMP_PATCH void AudioHeap_InitReverb(s32 reverbIndex, ReverbSettings* settings, s32 isFirstInit) {
    SynthesisReverb* reverb;
    size_t reverbBufferSize;

    if (reverbIndex < 0 || reverbIndex >= ARRAY_COUNT(gAudioCtx.synthesisReverbs)) {
        recomp_printf("[AudioDiag] InitReverb invalid index=%d settings=%p first=%d\n",
                      reverbIndex, settings, isFirstInit);
        return;
    }

    reverb = &gAudioCtx.synthesisReverbs[reverbIndex];

    if (settings == NULL || settings->downsampleRate == 0) {
        AudioDiag_DisableReverb(reverb, reverbIndex, "invalid settings");
        return;
    }

    recomp_printf("[AudioDiag] InitReverb begin index=%d first=%d settings=%p delaySetting=%d downsample=%d numReverbs=%d miscStart=%p miscCur=%p miscSize=%d\n",
                  reverbIndex, isFirstInit, settings, settings->delayNumSamples, settings->downsampleRate,
                  gAudioCtx.numSynthesisReverbs, gAudioCtx.miscPool.startAddr, gAudioCtx.miscPool.curAddr,
                  gAudioCtx.miscPool.size);

    if (isFirstInit) {
        reverb->delayNumSamplesAfterDownsampling = settings->delayNumSamples / settings->downsampleRate;
        reverb->leftLoadResampleBuf = NULL;
    } else if (reverb->delayNumSamplesAfterDownsampling < (settings->delayNumSamples / settings->downsampleRate)) {
        return;
    }

    reverb->downsampleRate = settings->downsampleRate;
    reverb->resampleEffectOn = false;
    reverb->resampleEffectExtraSamples = 0;
    reverb->resampleEffectLoadUnk = 0;
    reverb->resampleEffectSaveUnk = 0;
    AudioHeap_SetReverbData(reverbIndex, REVERB_DATA_TYPE_DELAY, settings->delayNumSamples, isFirstInit);

    if (reverb->delayNumSamples <= 0) {
        AudioDiag_DisableReverb(reverb, reverbIndex, "invalid delay");
        return;
    }

    reverb->decayRatio = settings->decayRatio;
    reverb->volume = settings->volume;
    reverb->subDelay = settings->subDelay * 64;
    reverb->subVolume = settings->subVolume;
    reverb->leakRtl = settings->leakRtl;
    reverb->leakLtr = settings->leakLtr;
    reverb->mixReverbIndex = settings->mixReverbIndex;
    reverb->mixReverbStrength = settings->mixReverbStrength;
    reverb->useReverb = 8;

    if (isFirstInit) {
        reverbBufferSize = reverb->delayNumSamples * 2;
        reverb->leftReverbBuf = AudioHeap_AllocZeroedAttemptExternal(&gAudioCtx.miscPool, reverbBufferSize);
        reverb->rightReverbBuf = AudioHeap_AllocZeroedAttemptExternal(&gAudioCtx.miscPool, reverbBufferSize);

        if (!AudioDiag_PtrInAudioPool(reverb->leftReverbBuf, reverbBufferSize) ||
            !AudioDiag_PtrInAudioPool(reverb->rightReverbBuf, reverbBufferSize)) {
            recomp_printf("[AudioDiag] Reverb buffer alloc invalid index=%d bytes=%d left=%p right=%p miscStart=%p miscCur=%p miscSize=%d externalStart=%p externalCur=%p externalSize=%d\n",
                          reverbIndex, reverbBufferSize, reverb->leftReverbBuf, reverb->rightReverbBuf,
                          gAudioCtx.miscPool.startAddr, gAudioCtx.miscPool.curAddr, gAudioCtx.miscPool.size,
                          gAudioCtx.externalPool.startAddr, gAudioCtx.externalPool.curAddr, gAudioCtx.externalPool.size);
            AudioDiag_DisableReverb(reverb, reverbIndex, "buffer allocation");
            return;
        }

        reverb->resampleFlags = 1;
        reverb->nextReverbBufPos = 0;
        reverb->delayNumSamplesUnk = 0;
        reverb->curFrame = 0;
        reverb->framesToIgnore = 2;
    }

    reverb->tunedSample.sample = &reverb->sample;
    reverb->sample.loop = &reverb->loop;
    reverb->tunedSample.tuning = 1.0f;
    reverb->sample.codec = CODEC_REVERB;
    reverb->sample.medium = MEDIUM_RAM;
    reverb->sample.size = reverb->delayNumSamples * SAMPLE_SIZE;
    reverb->sample.sampleAddr = (u8*)reverb->leftReverbBuf;
    reverb->loop.start = 0;
    reverb->loop.count = 1;
    reverb->loop.loopEnd = reverb->delayNumSamples;

    AudioHeap_SetReverbData(reverbIndex, REVERB_DATA_TYPE_FILTER_LEFT, settings->lowPassFilterCutoffLeft, isFirstInit);
    AudioHeap_SetReverbData(reverbIndex, REVERB_DATA_TYPE_FILTER_RIGHT, settings->lowPassFilterCutoffRight,
                            isFirstInit);

    recomp_printf("[AudioDiag] InitReverb end index=%d use=%d delay=%d left=%p right=%p filterL=%p filterR=%p miscCur=%p\n",
                  reverbIndex, reverb->useReverb, reverb->delayNumSamples, reverb->leftReverbBuf,
                  reverb->rightReverbBuf, reverb->filterLeft, reverb->filterRight, gAudioCtx.miscPool.curAddr);
}

RECOMP_PATCH void AudioLoad_InitSampleDmaBuffers(s32 numNotes) {
    SampleDma* dma;
    size_t sampleDmasBytes;
    s32 totalSampleDmas;
    s32 i;
    s32 t2;
    s32 j;

    gAudioCtx.sampleDmaBufSize = gAudioCtx.sampleDmaBufSize1;
    totalSampleDmas = 4 * gAudioCtx.numNotes * gAudioCtx.audioBufferParameters.specUnk4;
    sampleDmasBytes = totalSampleDmas * sizeof(SampleDma);
    gAudioCtx.sampleDmas = AudioHeap_Alloc(&gAudioCtx.miscPool, sampleDmasBytes);

    if (!AudioDiag_PtrInPool(gAudioCtx.sampleDmas, sampleDmasBytes, &gAudioCtx.miscPool)) {
        if (recomp_android_should_use_sync_boot_dma() && totalSampleDmas <= AUDIO_DIAG_MAX_SAMPLE_DMAS) {
            recomp_printf("[AudioDiag] Using static sample DMA table old=%p static=%p count=%d bytes=%d miscStart=%p miscCur=%p miscSize=%d\n",
                          gAudioCtx.sampleDmas, sAudioDiagSampleDmas, totalSampleDmas, sampleDmasBytes,
                          gAudioCtx.miscPool.startAddr, gAudioCtx.miscPool.curAddr, gAudioCtx.miscPool.size);
            bzero(sAudioDiagSampleDmas, sizeof(sAudioDiagSampleDmas));
            gAudioCtx.sampleDmas = sAudioDiagSampleDmas;
        } else {
            recomp_printf("[AudioDiag] Sample DMA table allocation invalid ptr=%p count=%d bytes=%d miscStart=%p miscCur=%p miscSize=%d\n",
                          gAudioCtx.sampleDmas, totalSampleDmas, sampleDmasBytes, gAudioCtx.miscPool.startAddr,
                          gAudioCtx.miscPool.curAddr, gAudioCtx.miscPool.size);
            gAudioCtx.sampleDmaCount = 0;
            gAudioCtx.sampleDmaListSize1 = 0;
            gAudioCtx.sampleDmaReuseQueue1RdPos = 0;
            gAudioCtx.sampleDmaReuseQueue1WrPos = 0;
            gAudioCtx.sampleDmaReuseQueue2RdPos = 0;
            gAudioCtx.sampleDmaReuseQueue2WrPos = 0;
            return;
        }
    }

    t2 = 3 * gAudioCtx.numNotes * gAudioCtx.audioBufferParameters.specUnk4;

    for (i = 0; i < t2 && gAudioCtx.sampleDmaCount < AUDIO_DIAG_MAX_SAMPLE_DMAS; i++) {
        dma = &gAudioCtx.sampleDmas[gAudioCtx.sampleDmaCount];
        dma->ramAddr = AudioHeap_AllocAttemptExternal(&gAudioCtx.miscPool, gAudioCtx.sampleDmaBufSize);
        if (dma->ramAddr == NULL) {
            break;
        }

        AudioHeap_WritebackDCache(dma->ramAddr, gAudioCtx.sampleDmaBufSize);
        dma->size = gAudioCtx.sampleDmaBufSize;
        dma->devAddr = 0;
        dma->sizeUnused = 0;
        dma->unused = 0;
        dma->ttl = 0;
        gAudioCtx.sampleDmaCount++;
    }

    for (i = 0; (u32)i < gAudioCtx.sampleDmaCount; i++) {
        gAudioCtx.sampleDmaReuseQueue1[i] = i;
        gAudioCtx.sampleDmas[i].reuseIndex = i;
    }

    for (i = gAudioCtx.sampleDmaCount; i < AUDIO_DIAG_MAX_SAMPLE_DMAS; i++) {
        gAudioCtx.sampleDmaReuseQueue1[i] = 0;
    }

    gAudioCtx.sampleDmaReuseQueue1RdPos = 0;
    gAudioCtx.sampleDmaReuseQueue1WrPos = gAudioCtx.sampleDmaCount;
    gAudioCtx.sampleDmaListSize1 = gAudioCtx.sampleDmaCount;
    gAudioCtx.sampleDmaBufSize = gAudioCtx.sampleDmaBufSize2;

    for (j = 0; j < gAudioCtx.numNotes && gAudioCtx.sampleDmaCount < AUDIO_DIAG_MAX_SAMPLE_DMAS; j++) {
        dma = &gAudioCtx.sampleDmas[gAudioCtx.sampleDmaCount];
        dma->ramAddr = AudioHeap_AllocAttemptExternal(&gAudioCtx.miscPool, gAudioCtx.sampleDmaBufSize);
        if (dma->ramAddr == NULL) {
            break;
        }

        AudioHeap_WritebackDCache(dma->ramAddr, gAudioCtx.sampleDmaBufSize);
        dma->size = gAudioCtx.sampleDmaBufSize;
        dma->devAddr = 0;
        dma->sizeUnused = 0;
        dma->unused = 0;
        dma->ttl = 0;
        gAudioCtx.sampleDmaCount++;
    }

    for (i = gAudioCtx.sampleDmaListSize1; (u32)i < gAudioCtx.sampleDmaCount; i++) {
        gAudioCtx.sampleDmaReuseQueue2[i - gAudioCtx.sampleDmaListSize1] = i;
        gAudioCtx.sampleDmas[i].reuseIndex = i - gAudioCtx.sampleDmaListSize1;
    }

    for (i = gAudioCtx.sampleDmaCount; i < AUDIO_DIAG_MAX_SAMPLE_DMAS; i++) {
        gAudioCtx.sampleDmaReuseQueue2[i] = gAudioCtx.sampleDmaListSize1;
    }

    gAudioCtx.sampleDmaReuseQueue2RdPos = 0;
    gAudioCtx.sampleDmaReuseQueue2WrPos = gAudioCtx.sampleDmaCount - gAudioCtx.sampleDmaListSize1;
    recomp_printf("[AudioDiag] Sample DMA init count=%d list1=%d buf1=%d buf2=%d table=%p\n",
                  gAudioCtx.sampleDmaCount, gAudioCtx.sampleDmaListSize1, gAudioCtx.sampleDmaBufSize1,
                  gAudioCtx.sampleDmaBufSize2, gAudioCtx.sampleDmas);
}
