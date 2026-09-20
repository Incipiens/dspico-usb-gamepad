#include <nds/ndstypes.h>
#include <nds.h>
#include <stdio.h>
#include <libtwl/gfx/gfxStatus.h>
#include <libtwl/mem/memExtern.h>
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/rtos/rtosThread.h>
#include <libtwl/rtos/rtosEvent.h>
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/ipc/ipcFifoSystem.h>

// keys + touch state from arm7, must match arm7
#define IPC_CHANNEL_INPUT 21

static rtos_event_t sVblankEvent;
static volatile u32 sInputState;
static u32 sPrevState = 0xFFFFFFFF;

static void vblankIrq(u32 irqMask)
{
    rtos_signalEvent(&sVblankEvent);
}

static void inputHandler(u32 channel, u32 data, void* arg)
{
    sInputState = data;
}

static const struct
{
    const char* name;
    u32 mask;
} BUTTONS[] =
{
    { "A", 1 << 0 },
    { "B", 1 << 1 },
    { "Select", 1 << 2 },
    { "Start", 1 << 3 },
    { "Right", 1 << 4 },
    { "Left", 1 << 5 },
    { "Up", 1 << 6 },
    { "Down", 1 << 7 },
    { "R", 1 << 8 },
    { "L", 1 << 9 },
    { "X", 1 << 10 },
    { "Y", 1 << 11 },
};

int main(int argc, char* argv[])
{
    *(vu32*)0x04000000 = 0x10000;
    *(vu16*)0x05000000 = 31 << 5;
    *(vu16*)0x0400006C = 0;

    mem_setDsCartridgeCpu(EXMEMCNT_SLOT1_CPU_ARM7);

    rtos_initIrq();
    rtos_startMainThread();
    ipc_initFifoSystem();

    rtos_createEvent(&sVblankEvent);

    while (ipc_getArm7SyncBits() != 7);

    ipc_setArm9SyncBits(6);

    ipc_setChannelHandler(IPC_CHANNEL_INPUT, inputHandler, nullptr);

    rtos_setIrqFunc(RTOS_IRQ_VBLANK, vblankIrq);
    rtos_enableIrqMask(RTOS_IRQ_VBLANK);
    gfx_setVBlankIrqEnabled(true);

    consoleDemoInit();
    printf("\x1b[0;0HDSpico USB Gamepad");
    printf("\x1b[1;0HButtons + touch (right stick) on your PC:");

    while (true)
    {
        rtos_waitEvent(&sVblankEvent, true, true);

        u32 state = sInputState;
        if (state != sPrevState)
        {
            sPrevState = state;
            u16 keys = state & 0x0FFF;
            bool penDown = state & (1 << 12);
            u16 px = (state >> 16) & 0xFF;
            u16 py = (state >> 24) & 0xFF;

            for (int i = 0; i < 12; i++)
            {
                int col = i < 6 ? 2 : 18;
                int row = 3 + (i % 6);
                printf("\x1b[%d;%dH%-6s [%s]", row, col, BUTTONS[i].name,
                    (keys & BUTTONS[i].mask) ? "X" : " ");
            }

            printf("\x1b[10;2HTouch:     [%s] %3d,%3d ", penDown ? "X" : " ",
                penDown ? px : 0, penDown ? py : 0);
        }
    }

    return 0;
}
