#include "common.h"
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/rtos/rtosThread.h>
#include <libtwl/rtos/rtosEvent.h>
#include <libtwl/sio/sio.h>
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include <libtwl/gfx/gfxStatus.h>
#include <libtwl/mem/memSwap.h>
#include <libtwl/i2c/i2cMcu.h>
#include <libtwl/spi/spiPmic.h>
#include <nds/arm7/touch.h>
#include "ExitMode.h"
#include "Arm7State.h"
#include "tusb.h"
#include "usb_descriptors.h"

rtos_mutex_t gCardMutex;

// X/Y not connected to KEYINPUT; NDS7-only EXTKEYIN register. RCNT must be in GPIO mode
#define REG_EXTKEYIN (*(vu16*)0x04000136)
#define EXTKEYIN_KEY_X (1 << 0)
#define EXTKEYIN_KEY_Y (1 << 1)

// key bit positions in 12-bit layout shared with the arm9 display
#define KEYBIT_X (1 << 10)
#define KEYBIT_Y (1 << 11)

// keys + touch state for arm9 display. Must match arm9
#define IPC_CHANNEL_INPUT 21

static rtos_event_t sVBlankEvent;
static ExitMode sExitMode;
static Arm7State sState;
static volatile u8 sMcuIrqFlag = false;

static rtos_thread_t sUsbThread;
static u32 sUsbThreadStack[512];

// Initialized to invalid value so first report always sent
static u32 sLastState = 0xFFFFFFFF;

static void vblankIrq(u32 irqMask)
{
    rtos_signalEvent(&sVBlankEvent);
}

static void mcuIrq(u32 irq2Mask)
{
    sMcuIrqFlag = true;
}

static void checkMcuIrq(void)
{
    // mcu only exists in DSi mode
    if (isDSiMode())
    {
        // check and ack flag atomically
        if (mem_swapByte(false, &sMcuIrqFlag))
        {
            // check irq mask
            u32 irqMask = mcu_getIrqMask();
            if (irqMask & MCU_IRQ_RESET)
            {
                // power button was released
                sExitMode = ExitMode::Reset;
                sState = Arm7State::ExitRequested;
            }
            else if (irqMask & MCU_IRQ_POWER_OFF)
            {
                // power button was held long to trigger a power off
                sExitMode = ExitMode::PowerOff;
                sState = Arm7State::ExitRequested;
            }
        }
    }
}

static void initializeVBlankIrq()
{
    rtos_createEvent(&sVBlankEvent);
    rtos_setIrqFunc(RTOS_IRQ_VBLANK, vblankIrq);
    rtos_enableIrqMask(RTOS_IRQ_VBLANK);
    gfx_setVBlankIrqEnabled(true);
}

static void usbThreadMain(void* arg)
{
    while (true)
    {
        tud_task();
    }
}

static uint8_t keysToHat(u16 keys)
{
    uint8_t hat = GAMEPAD_HAT_CENTERED;
    if (keys & KEYINPUT_KEY_DPAD_UP)
    {
        hat = keys & KEYINPUT_KEY_DPAD_RIGHT ? GAMEPAD_HAT_UP_RIGHT
            : keys & KEYINPUT_KEY_DPAD_LEFT ? GAMEPAD_HAT_UP_LEFT
            : GAMEPAD_HAT_UP;
    }
    else if (keys & KEYINPUT_KEY_DPAD_DOWN)
    {
        hat = keys & KEYINPUT_KEY_DPAD_RIGHT ? GAMEPAD_HAT_DOWN_RIGHT
            : keys & KEYINPUT_KEY_DPAD_LEFT ? GAMEPAD_HAT_DOWN_LEFT
            : GAMEPAD_HAT_DOWN;
    }
    else if (keys & KEYINPUT_KEY_DPAD_RIGHT)
    {
        hat = GAMEPAD_HAT_RIGHT;
    }
    else if (keys & KEYINPUT_KEY_DPAD_LEFT)
    {
        hat = GAMEPAD_HAT_LEFT;
    }
    return hat;
}

// face buttons map by physical position
static uint32_t keysToButtons(u16 keys)
{
    uint32_t buttons = 0;
    if (keys & KEYINPUT_KEY_B) buttons |= GAMEPAD_BUTTON_A;
    if (keys & KEYINPUT_KEY_A) buttons |= GAMEPAD_BUTTON_B;
    if (keys & KEYBIT_Y) buttons |= GAMEPAD_BUTTON_X;
    if (keys & KEYBIT_X) buttons |= GAMEPAD_BUTTON_Y;
    if (keys & KEYINPUT_KEY_L) buttons |= GAMEPAD_BUTTON_TL;
    if (keys & KEYINPUT_KEY_R) buttons |= GAMEPAD_BUTTON_TR;
    if (keys & KEYINPUT_KEY_SELECT) buttons |= GAMEPAD_BUTTON_SELECT;
    if (keys & KEYINPUT_KEY_START) buttons |= GAMEPAD_BUTTON_START;
    return buttons;
}

// touch maps to right stick: screen position = deflection from center
static int8_t axisFromRaw(int raw, int range)
{
    int scaled = raw * 255 / range - 128;
    if (scaled < -127)
    {
        scaled = -127;
    }
    if (scaled > 127)
    {
        scaled = 127;
    }
    return (int8_t)scaled;
}

static void updateGamepad()
{
    u16 keys = ~REG_KEYINPUT & 0x03FF;
    if (!(REG_EXTKEYIN & EXTKEYIN_KEY_X))
    {
        keys |= KEYBIT_X;
    }
    if (!(REG_EXTKEYIN & EXTKEYIN_KEY_Y))
    {
        keys |= KEYBIT_Y;
    }

    bool penDown = touchPenDown();
    touchPosition touch = {};
    if (penDown)
    {
        touchReadXY(&touch);
        // pen can lift between the pen-IRQ check and the TSC read.
        if (touch.px == 0 && touch.py == 0)
        {
            penDown = false;
        }
    }

    u32 state = keys | (penDown << 12) | (touch.px << 16) | (touch.py << 24);
    if (state == sLastState || !tud_hid_ready())
    {
        return;
    }

    int8_t z = 0;
    int8_t rz = 0;
    if (penDown)
    {
        z = axisFromRaw(touch.px, 255);
        rz = axisFromRaw(touch.py, 191);
    }

    tud_hid_gamepad_report(0, 0, 0, z, rz, 0, 0, keysToHat(keys), keysToButtons(keys));

    sLastState = state;
    ipc_trySendFifoMessage(IPC_CHANNEL_INPUT, state);
}

static void initializeArm7()
{
    rtos_initIrq();
    rtos_startMainThread();
    ipc_initFifoSystem();

    // loads touch calibration data used by touchReadXY
    readUserSettings();
    touchInit();

    sio_setGpioSiIrq(false);
    sio_setGpioMode(RCNT0_L_MODE_GPIO);

    initializeVBlankIrq();

    if (isDSiMode())
    {
        rtos_setIrq2Func(RTOS_IRQ2_MCU, mcuIrq);
        rtos_enableIrq2Mask(RTOS_IRQ2_MCU);
    }

    tusb_rhport_init_t dev_init =
    {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(0, &dev_init);

    rtos_createThread(&sUsbThread, 3, usbThreadMain, NULL, sUsbThreadStack, sizeof(sUsbThreadStack));
    rtos_wakeupThread(&sUsbThread);

    ipc_setArm7SyncBits(7);
}

static bool performExit(ExitMode exitMode)
{
    switch (exitMode)
    {
        case ExitMode::Reset:
        {
            mcu_setWarmBootFlag(true);
            mcu_hardReset();
            break;
        }
        case ExitMode::PowerOff:
        {
            pmic_shutdown();
            break;
        }
    }

    while (true); // wait infinitely for exit
}

static void updateArm7()
{
    switch (sState)
    {
        case Arm7State::Idle:
        {
            checkMcuIrq();
            updateGamepad();
            break;
        }
        case Arm7State::ExitRequested:
        {
            performExit(sExitMode);
            break;
        }
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
    uint8_t* buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
    uint8_t const* buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

int main()
{
    sState = Arm7State::Idle;
    initializeArm7();

    while (true)
    {
        rtos_waitEvent(&sVBlankEvent, true, true);
        updateArm7();
    }

    return 0;
}
