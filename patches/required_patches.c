#include "patches.h"
#include "misc_funcs.h"
#include "transform_ids.h"
#include "loadfragment.h"
#include "libc/math.h"
#include "input.h"

void Main_ClearMemory(void* begin, void* end);
void Main_InitMemory(void);
void Main_InitScreen(void);
void Fault_AddHungupAndCrash(const char* file, s32 line);

extern u16 sNumDmaEntries;


RECOMP_DECLARE_EVENT(recomp_on_init());

// @recomp Patched to load the code segment in the recomp runtime.
RECOMP_PATCH void Main_Init(void) {
    DmaRequest dmaReq;
    OSMesgQueue mq;
    OSMesg msg[1];
    size_t prevSize;
    s32 syncBootDma;

    // @recomp Register base actor extensions.
    register_base_actor_extensions();

    // @recomp_event recomp_on_init(): Allow mods to initialize themselves once.
    recomp_on_init();

    syncBootDma = recomp_android_should_use_sync_boot_dma();

    if (!syncBootDma) {
        osCreateMesgQueue(&mq, msg, ARRAY_COUNT(msg));
    }

    prevSize = gDmaMgrDmaBuffSize;
    gDmaMgrDmaBuffSize = 0;

    // @recomp Load the code segment in the recomp runtime.
    recomp_load_overlays(SEGMENT_ROM_START(code), SEGMENT_START(code), SEGMENT_ROM_END(code) - SEGMENT_ROM_START(code));

    if (syncBootDma) {
        // @recomp Samsung devices have shown problems with this boot-time DMA
        // request going through the DMA manager queue. Keep that workaround
        // scoped to Samsung so normal Android devices retain the original path.
        DmaMgr_DmaRomToRam(SEGMENT_ROM_START(code), SEGMENT_START(code),
                           SEGMENT_ROM_END(code) - SEGMENT_ROM_START(code));
    } else {
        DmaMgr_SendRequestImpl(&dmaReq, SEGMENT_START(code), SEGMENT_ROM_START(code),
                               SEGMENT_ROM_END(code) - SEGMENT_ROM_START(code), 0, &mq, NULL);
    }
    Main_InitScreen();
    Main_InitMemory();
    if (!syncBootDma) {
        osRecvMesg(&mq, NULL, OS_MESG_BLOCK);
    }

    gDmaMgrDmaBuffSize = prevSize;

    Main_ClearMemory(SEGMENT_BSS_START(code), SEGMENT_BSS_END(code));

    // @recomp Patch a float that's used to render the clock into the correct value.
    // This is done this way instead of patching the function to avoid conflicts with mods that need to patch the function.
    // The original code is `Matrix_RotateZF(-(timeInSeconds * 0.0175f) / 10.0f, MTXMODE_APPLY);`, where 0.0175f is being used
    // to convert degrees to radians. However, the correct value is PI/180 which is approximately 0.0174533f, and the difference is enough
    // to cause the clock to overshoot when reaching an hour mark.
    *(f32*)0x801DDBBC = ((f32)M_PI) / 180.0f;
}

void Overlay_Relocate(void* allocatedRamAddr, OverlayRelocationSection* ovlRelocs, uintptr_t vramStart);

static void AndroidDiag_LoadOverlayToRam(uintptr_t vromStart, void* dst, size_t size) {
    DmaEntry* entry = DmaMgr_FindDmaEntry(vromStart);
    s32 yaz0Status;

    recomp_measure_latency(97, 0x70, (u32)vromStart, (u32)(uintptr_t)dst, (u32)size);

    if (entry == NULL) {
        recomp_measure_latency(97, 0x71, (u32)vromStart, (u32)(uintptr_t)dst, (u32)size);
        recomp_load_overlays(vromStart, dst, size);
        return;
    }

    recomp_measure_latency(97, 0x72, (u32)entry->vromStart, (u32)entry->vromEnd, (u32)entry->romStart);
    recomp_measure_latency(97, 0x73, (u32)entry->romStart, (u32)entry->romEnd, (u32)size);

    if (entry->romEnd == 0) {
        recomp_load_overlays((entry->romStart + vromStart) - entry->vromStart, dst, size);
        recomp_measure_latency(97, 0x74, (u32)((entry->romStart + vromStart) - entry->vromStart),
                               (u32)(uintptr_t)dst, (u32)size);
        return;
    }

    if ((vromStart != entry->vromStart) || (size != (entry->vromEnd - entry->vromStart))) {
        recomp_measure_latency(97, 0x75, (u32)vromStart, (u32)entry->vromStart, (u32)size);
        recomp_load_overlays(vromStart, dst, size);
        return;
    }

    yaz0Status = recomp_android_load_yaz0(entry->romStart, entry->romEnd - entry->romStart, dst, size);
    recomp_measure_latency(97, 0x76, (u32)yaz0Status, (u32)entry->romStart, (u32)(uintptr_t)dst);

    if (yaz0Status != 0) {
        recomp_load_overlays(vromStart, dst, size);
        recomp_measure_latency(97, 0x77, (u32)yaz0Status, (u32)vromStart, (u32)size);
    }
}

// Samsung devices can reach the game entrypoint now, but still fault inside the
// vanilla DMAMGR thread during boot asset loads. Keep the normal path everywhere
// else, and route Samsung DMAs through the host-side Android loaders used by the
// other Samsung-safe asset paths.
RECOMP_PATCH void DmaMgr_ProcessMsg(DmaRequest* req) {
    uintptr_t vrom;
    void* ram;
    size_t size;
    uintptr_t romStart;
    size_t romSize;
    DmaEntry* dmaEntry;
    s32 index;
    s32 yaz0Status;
    s32 syncBootDma = recomp_android_should_use_sync_boot_dma();

    vrom = req->vromAddr;
    ram = req->dramAddr;
    size = req->size;

    index = DmaMgr_FindDmaIndex(vrom);

    if ((index >= 0) && (index < sNumDmaEntries)) {
        dmaEntry = &dmadata[index];
        if (dmaEntry->romEnd == 0) {
            if (dmaEntry->vromEnd < (vrom + size)) {
                Fault_AddHungupAndCrash("../z_std_dma.c", 499);
            }
            if (syncBootDma) {
                recomp_load_overlays((dmaEntry->romStart + vrom) - dmaEntry->vromStart, ram, size);
            } else {
                DmaMgr_DmaRomToRam((dmaEntry->romStart + vrom) - dmaEntry->vromStart, (u8*)ram, size);
            }
            return;
        }

        romSize = dmaEntry->romEnd - dmaEntry->romStart;
        romStart = dmaEntry->romStart;

        if (vrom != dmaEntry->vromStart) {
            Fault_AddHungupAndCrash("../z_std_dma.c", 518);
        }

        if (size != (dmaEntry->vromEnd - dmaEntry->vromStart)) {
            Fault_AddHungupAndCrash("../z_std_dma.c", 525);
        }

        if (syncBootDma) {
            yaz0Status = recomp_android_load_yaz0(romStart, romSize, ram, size);
            if (yaz0Status != 0) {
                recomp_printf("[AndroidLoadDiag] DmaMgr_ProcessMsg host Yaz0 failed status=%d rom=%08llX "
                              "compressed=%08X expected=%08X\n",
                              yaz0Status, (u64)romStart, (u32)romSize, (u32)size);
                Fault_AddHungupAndCrash("../z_std_dma.c", 545);
            }
        } else {
            Yaz0_Decompress(romStart, ram, romSize);
        }
    } else {
        Fault_AddHungupAndCrash("../z_std_dma.c", 558);
    }
}

// @recomp Patched to load the overlay in the recomp runtime.
RECOMP_PATCH size_t Overlay_Load(uintptr_t vromStart, uintptr_t vromEnd, void* ramStart, void* ramEnd, void* allocatedRamAddr) {
    uintptr_t vramStart = (uintptr_t)ramStart;
    uintptr_t vramEnd = (uintptr_t)ramEnd;
    s32 size = vromEnd - vromStart;
    uintptr_t end;
    OverlayRelocationSection* ovlRelocs;
    s32 syncBootDma = recomp_android_should_use_sync_boot_dma();
    
    // @recomp Load the overlay in the recomp runtime.
    recomp_load_overlays(vromStart, allocatedRamAddr, vromEnd - vromStart);

    end = (uintptr_t)allocatedRamAddr + size;
    if (syncBootDma) {
        // Samsung devices can fault when actor overlays go through the raw
        // DMA path. Load directly from the ROM mapping instead, preserving the
        // same VROM-to-ROM and Yaz0 handling used by the safer Android loaders.
        AndroidDiag_LoadOverlayToRam(vromStart, allocatedRamAddr, size);
    } else {
        DmaMgr_SendRequest0(allocatedRamAddr, vromStart, size);
    }

    ovlRelocs = (OverlayRelocationSection*)(end - ((s32*)end)[-1]);

    Overlay_Relocate(allocatedRamAddr, ovlRelocs, vramStart);

    if (ovlRelocs->bssSize != 0) {
        bzero((void*)end, ovlRelocs->bssSize);
    }

    size = vramEnd - vramStart;

    osWritebackDCache(allocatedRamAddr, size);
    osInvalICache(allocatedRamAddr, size);

    return size;
}
