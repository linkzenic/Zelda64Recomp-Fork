#include "patches.h"
#include "audio/heap.h"

extern NoteSampleState gZeroedSampleState;

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
