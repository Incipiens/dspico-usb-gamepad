#pragma once

enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL
};

enum
{
    ITF_NUM_HID = 0,
    ITF_NUM_TOTAL
};

#define EPNUM_HID       0x81
#define HID_INTERVAL    8

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
