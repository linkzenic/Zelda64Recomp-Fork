#include "patches.h"
#include "input.h"
#include "misc_funcs.h"

// @recomp Leave the entire KSEG0 range unmodified when translating to a virtual address. This will allow
// using the entirety of the extended RAM address space for custom assets. 
RECOMP_PATCH void* Lib_SegmentedToVirtual(void* ptr) {
    if (IS_KSEG0(ptr)) {
        return ptr;
    }
    else {
        return SEGMENTED_TO_K0(ptr);
    }
}

AnimationEntry* AnimationContext_AddEntry(AnimationContext* animationCtx, AnimationType type);
#define LINK_ANIMETION_OFFSET(addr, offset) \
    (SEGMENT_ROM_START(link_animetion) + ((uintptr_t)addr & 0xFFFFFF) + ((u32)offset))

// @recomp Skip the DMA if the animation is already in RAM. Allows mods to play custom animations. 
RECOMP_PATCH void AnimationContext_SetLoadFrame(PlayState* play, PlayerAnimationHeader* animation, s32 frame, s32 limbCount,
                                      Vec3s* frameTable) {
    AnimationEntry* task = AnimationContext_AddEntry(&play->animationCtx, ANIMATION_LINKANIMETION);

    if (task != NULL) {
        PlayerAnimationHeader* playerAnimHeader = Lib_SegmentedToVirtual(animation);
        size_t frameSize = sizeof(Vec3s) * limbCount + sizeof(s16);
        uintptr_t rom = LINK_ANIMETION_OFFSET(playerAnimHeader->linkAnimSegment, frameSize * frame);
        s32 pad;

        osCreateMesgQueue(&task->data.load.msgQueue, task->data.load.msg,
                          ARRAY_COUNT(task->data.load.msg));
        
        if (IS_KSEG0(playerAnimHeader->linkAnimSegment)) {
            osSendMesg(&task->data.load.msgQueue, NULL, OS_MESG_NOBLOCK);
            Lib_MemCpy(frameTable, ((u8*)playerAnimHeader->segmentVoid) + (sizeof(Vec3s) * limbCount + sizeof(s16)) * frame,
                sizeof(Vec3s) * limbCount + sizeof(s16));
        }
        else if (recomp_android_should_use_sync_boot_dma()) {
            recomp_measure_latency(97, 0x40, (u32)rom, (u32)(uintptr_t)frameTable, (u32)frameSize);
            recomp_load_overlays(rom, frameTable, frameSize);
            osSendMesg(&task->data.load.msgQueue, NULL, OS_MESG_NOBLOCK);
        }
        else {
            DmaMgr_SendRequestImpl(
                &task->data.load.req, frameTable, rom, frameSize, 0, &task->data.load.msgQueue, NULL);
        }
    }
}
