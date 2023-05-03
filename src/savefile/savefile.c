
#include "savefile.h"
#include "util/memory.h"
#include "controls/controller.h"

#include "../controls/controller_actions.h"

struct SaveData gSaveData;
int gCurrentTestSubject = -1;

#ifdef DEBUG
#define UNLOCK_ALL  1
#else
#define UNLOCK_ALL  1
#endif

OSPiHandle gSramHandle;
OSMesgQueue     timerQueue;
OSMesg     timerQueueBuf;

extern OSMesgQueue dmaMessageQ;

void savefileNew() {
    zeroMemory(&gSaveData, sizeof(gSaveData));
    gSaveData.header.header = SAVEFILE_HEADER;

    gSaveData.header.nextTestSubject = 0;
    gSaveData.header.flags = 0;

    for (int i = 0; i < MAX_SAVE_SLOTS; ++i) {
        gSaveData.saveSlotMetadata[i].testChamber = NO_TEST_CHAMBER;
        gSaveData.saveSlotMetadata[i].testSubjectNumber = 0xFF;
        gSaveData.saveSlotMetadata[i].saveSlotOrder = 0xFF;
    }

    controllerSetDefaultSource();

    gSaveData.audio.soundVolume = 0xFF;
    gSaveData.audio.musicVolume = 0xFF;
}

#define SRAM_latency     0x5 
#define SRAM_pulse       0x0c 
#define SRAM_pageSize    0xd 
#define SRAM_relDuration 0x2

#define SRAM_CHUNK_DELAY        OS_USEC_TO_CYCLES(10 * 1000)

#define SRAM_ADDR   0x08000000

void savefileLoad() {
    /* Fill basic information */

    gSramHandle.type = 3;
    gSramHandle.baseAddress = PHYS_TO_K1(SRAM_START_ADDR);

    /* Get Domain parameters */

    gSramHandle.latency = (u8)SRAM_latency;
    gSramHandle.pulse = (u8)SRAM_pulse;
    gSramHandle.pageSize = (u8)SRAM_pageSize;
    gSramHandle.relDuration = (u8)SRAM_relDuration;
    gSramHandle.domain = PI_DOMAIN2;
    gSramHandle.speed = 0;

    osCreateMesgQueue(&timerQueue, &timerQueueBuf, 1);

    /* TODO gSramHandle.speed = */

    zeroMemory(&(gSramHandle.transferInfo), sizeof(gSramHandle.transferInfo));

    /*
    * Put the gSramHandle onto PiTable
    */

    OSIntMask saveMask = osGetIntMask();
    osSetIntMask(OS_IM_NONE);
    gSramHandle.next = __osPiTable;
    __osPiTable = &gSramHandle;
    osSetIntMask(saveMask);


    OSTimer timer;

    OSIoMesg dmaIoMesgBuf;

    dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_HIGH;
    dmaIoMesgBuf.hdr.retQueue = &dmaMessageQ;
    dmaIoMesgBuf.dramAddr = &gSaveData;
    dmaIoMesgBuf.devAddr = SRAM_ADDR;
    dmaIoMesgBuf.size = sizeof(gSaveData);

    osInvalDCache(&gSaveData, sizeof(gSaveData));
    if (osEPiStartDma(&gSramHandle, &dmaIoMesgBuf, OS_READ) == -1)
    {
        savefileNew();
        return;
    }
    (void) osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);

    osSetTimer(&timer, SRAM_CHUNK_DELAY, 0, &timerQueue, 0);
    (void) osRecvMesg(&timerQueue, NULL, OS_MESG_BLOCK);

    if (gSaveData.header.header != SAVEFILE_HEADER) {
        savefileNew();
    }
}

void savefileSave() {
    OSTimer timer;

    OSIoMesg dmaIoMesgBuf;

    dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_HIGH;
    dmaIoMesgBuf.hdr.retQueue = &dmaMessageQ;
    dmaIoMesgBuf.dramAddr = &gSaveData;
    dmaIoMesgBuf.devAddr = SRAM_ADDR;
    dmaIoMesgBuf.size = sizeof(gSaveData);

    osWritebackDCache(&gSaveData, sizeof(gSaveData));
    if (osEPiStartDma(&gSramHandle, &dmaIoMesgBuf, OS_WRITE) == -1)
    {
        return;
    }
    (void) osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);

    osSetTimer(&timer, SRAM_CHUNK_DELAY, 0, &timerQueue, 0);
    (void) osRecvMesg(&timerQueue, NULL, OS_MESG_BLOCK);
}

void savefileSetFlags(enum SavefileFlags flags) {
    gSaveData.header.flags |= flags;
}

void savefileUnsetFlags(enum SavefileFlags flags) {
    gSaveData.header.flags &= ~flags;
}

int savefileReadFlags(enum SavefileFlags flags) {
    return gSaveData.header.flags & flags;
}

#define SAVE_SLOT_SRAM_ADDRESS(index) (SRAM_ADDR + (1 + (index)) * SAVE_SLOT_SIZE)

void savefileSaveGame(Checkpoint checkpoint, int testChamberIndex, int subjectNumber, int slotIndex) {
    OSTimer timer;

    OSIoMesg dmaIoMesgBuf;

    dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_HIGH;
    dmaIoMesgBuf.hdr.retQueue = &dmaMessageQ;
    dmaIoMesgBuf.dramAddr = checkpoint;
    dmaIoMesgBuf.devAddr = SAVE_SLOT_SRAM_ADDRESS(slotIndex);
    dmaIoMesgBuf.size = MAX_CHECKPOINT_SIZE;

    osWritebackDCache(&gSaveData, MAX_CHECKPOINT_SIZE);
    if (osEPiStartDma(&gSramHandle, &dmaIoMesgBuf, OS_WRITE) == -1)
    {
        return;
    }
    (void) osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);

    osSetTimer(&timer, SRAM_CHUNK_DELAY, 0, &timerQueue, 0);
    (void) osRecvMesg(&timerQueue, NULL, OS_MESG_BLOCK);

    unsigned char prevSortOrder = gSaveData.saveSlotMetadata[slotIndex].saveSlotOrder;

    // shift existing slot sort orders
    for (int i = 0; i < MAX_SAVE_SLOTS; ++i) {
        if (gSaveData.saveSlotMetadata[i].saveSlotOrder < prevSortOrder) {
            ++gSaveData.saveSlotMetadata[i].saveSlotOrder;
        }
    }

    gSaveData.saveSlotMetadata[slotIndex].testChamber = testChamberIndex;
    gSaveData.saveSlotMetadata[slotIndex].testSubjectNumber = subjectNumber;
    gSaveData.saveSlotMetadata[slotIndex].saveSlotOrder = 0;

    savefileSave();
}

struct SlotAndOrder {
    unsigned char saveSlot;
    unsigned char sortOrder;
};

void savefileMetadataSort(struct SlotAndOrder* result, struct SlotAndOrder* tmp, int start, int end) {
    if (start + 1 >= end) {
        return;
    }

    int mid = (start + end) >> 1;

    savefileMetadataSort(result, tmp, start, mid);
    savefileMetadataSort(result, tmp, mid, end);

    int currentOut = start;
    int aRead = start;
    int bRead = mid;

    while (aRead < mid || bRead < end) {
        if (bRead == end || (aRead < mid && result[aRead].sortOrder < result[bRead].sortOrder)) {
            tmp[currentOut] = result[aRead];
            ++currentOut;
            ++aRead;
        } else {
            tmp[currentOut] = result[bRead];
            ++currentOut;
            ++bRead;
        }
    }

    for (int i = start; i < end; ++i) {
        result[i] = tmp[i];
    }
}

int savefileListSaves(struct SaveSlotInfo* slots, int includeAuto) {
    int result = 0;

    struct SlotAndOrder unsortedResult[MAX_SAVE_SLOTS];

    for (int i = 0; i < MAX_SAVE_SLOTS; ++i) {
        if (gSaveData.saveSlotMetadata[i].testChamber == NO_TEST_CHAMBER) {
            continue;
        }

        if (gSaveData.saveSlotMetadata[i].testSubjectNumber == TEST_SUBJECT_AUTOSAVE && !includeAuto) {
            continue;
        }

        unsortedResult[result].sortOrder = gSaveData.saveSlotMetadata[i].saveSlotOrder;
        unsortedResult[result].saveSlot = i;
        ++result;
    }

    struct SlotAndOrder tmp[MAX_SAVE_SLOTS];

    savefileMetadataSort(unsortedResult, tmp, 0, result);

    for (int i = 0; i < result; ++i) {
        slots[i].saveSlot = unsortedResult[i].saveSlot;
        slots[i].testChamber = gSaveData.saveSlotMetadata[unsortedResult[i].saveSlot].testChamber;
    }

    return result;
}

int savefileNextTestSubject() {
    int needsToCheck = 1;

    while (needsToCheck) {
        needsToCheck = 0;

        for (int i = 0; i < MAX_SAVE_SLOTS; ++i) {
            if (gSaveData.saveSlotMetadata[i].testSubjectNumber == gSaveData.header.nextTestSubject) {
                needsToCheck = 1;
                ++gSaveData.header.nextTestSubject;

                if (gSaveData.header.nextTestSubject > TEST_SUBJECT_MAX) {
                    gSaveData.header.nextTestSubject = 0;
                }

                break;
            }
        }
    }

    return gSaveData.header.nextTestSubject;
}

int savefileSuggestedSlot(int testSubject) {
    int result = 0;

    // 0 indicates a new save
    for (int i = 1; i < MAX_SAVE_SLOTS; ++i) {
        if (gSaveData.saveSlotMetadata[i].testSubjectNumber == testSubject && 
            (result == 0 || gSaveData.saveSlotMetadata[i].saveSlotOrder < gSaveData.saveSlotMetadata[result].saveSlotOrder)) {
            result = i;
        }
    }

    return result;
}

int savefileOldestSlot() {
    int result = 1;

    // 0 indicates a new save
    for (int i = 1; i < MAX_SAVE_SLOTS; ++i) {
        if (gSaveData.saveSlotMetadata[i].saveSlotOrder > gSaveData.saveSlotMetadata[result].saveSlotOrder) {
            result = i;
        }
    }

    return result;
}

int savefileFirstFreeSlot() {
    for (int i = 1; i < MAX_SAVE_SLOTS; ++i) {
        if (gSaveData.saveSlotMetadata[i].testChamber == NO_TEST_CHAMBER) {
            return i;
        }
    }

    return SAVEFILE_NO_SLOT;
}

void savefileLoadGame(int slot, Checkpoint checkpoint) {
    OSTimer timer;

    OSIoMesg dmaIoMesgBuf;

    dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_HIGH;
    dmaIoMesgBuf.hdr.retQueue = &dmaMessageQ;
    dmaIoMesgBuf.dramAddr = checkpoint;
    dmaIoMesgBuf.devAddr = SAVE_SLOT_SRAM_ADDRESS(slot);
    dmaIoMesgBuf.size = MAX_CHECKPOINT_SIZE;

    osInvalDCache(checkpoint, MAX_CHECKPOINT_SIZE);
    if (osEPiStartDma(&gSramHandle, &dmaIoMesgBuf, OS_READ) == -1)
    {
        savefileNew();
        return;
    }
    (void) osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);

    osSetTimer(&timer, SRAM_CHUNK_DELAY, 0, &timerQueue, 0);
    (void) osRecvMesg(&timerQueue, NULL, OS_MESG_BLOCK);
}

void savefileLoadScreenshot(u16* target, u16* location) {
    if ((int)location >= SRAM_START_ADDR && (int)location <= (SRAM_START_ADDR + SRAM_SIZE)) {
        OSTimer timer;

        OSIoMesg dmaIoMesgBuf;

        dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_HIGH;
        dmaIoMesgBuf.hdr.retQueue = &dmaMessageQ;
        dmaIoMesgBuf.dramAddr = target;
        dmaIoMesgBuf.devAddr = (u32)location;
        dmaIoMesgBuf.size = THUMBANIL_IMAGE_SIZE;

        osInvalDCache(target, THUMBANIL_IMAGE_SIZE);
        if (osEPiStartDma(&gSramHandle, &dmaIoMesgBuf, OS_READ) == -1)
        {
            savefileNew();
            return;
        }
        (void) osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);

        osSetTimer(&timer, SRAM_CHUNK_DELAY, 0, &timerQueue, 0);
        (void) osRecvMesg(&timerQueue, NULL, OS_MESG_BLOCK);
    } else {
        memCopy(target, location, THUMBANIL_IMAGE_SIZE);
    }
}