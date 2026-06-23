#include "patches.h"
#include "audio/heap.h"
#include "input.h"

extern NoteSampleState gZeroedSampleState;

static s32 AudioDiag_LogAllocCount = 0;
static f32 sAudioDiagAdsrDecayTable[0x100] = { 1.0f };

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

RECOMP_PATCH void AudioHeap_InitPool(AudioAllocPool* pool, void* addr, size_t size) {
    uintptr_t alignedAddr = ALIGN16((uintptr_t)addr);
    size_t alignmentLoss = (uintptr_t)addr & 0xF;

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
