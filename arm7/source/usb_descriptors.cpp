#include "common.h"
#include "UsbStringDescriptor.h"
#include "tusb.h"
#include "usb_descriptors.h"

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]     AUDIO | MIDI | HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n)  ( (CFG_TUD_##itf) << (n) )
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
    _PID_MAP(MIDI, 3) | _PID_MAP(AUDIO, 4) | _PID_MAP(VENDOR, 5) )

const u8* tud_descriptor_device_cb(void)
{
    static const tusb_desc_device_t deviceDescriptor =
    {
        .bLength            = sizeof(tusb_desc_device_t),
        .bDescriptorType    = TUSB_DESC_DEVICE,
        .bcdUSB             = 0x0110,

        // Single HID interface, the device class lives in the interface descriptor.
        .bDeviceClass       = 0x00,
        .bDeviceSubClass    = 0x00,
        .bDeviceProtocol    = 0x00,
        .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

        .idVendor           = 0xCafe,
        .idProduct          = USB_PID,
        .bcdDevice          = 0x0100,

        .iManufacturer      = STRID_MANUFACTURER,
        .iProduct           = STRID_PRODUCT,
        .iSerialNumber      = STRID_SERIAL,

        .bNumConfigurations = 0x01
    };

    return (const u8*)&deviceDescriptor;
}

const u8* tud_descriptor_configuration_cb(u8 index)
{
    (void)index;

    static const u8 reportDescriptor[] = { TUD_HID_REPORT_DESC_GAMEPAD() };
    static const u8 configurationDescriptor[] =
    {
        // Config number, interface count, string index, total length, attribute, power in mA
        TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

        // Interface number, string index, protocol, report descriptor len, EP In address, EP size, polling interval
        TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(reportDescriptor), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, HID_INTERVAL)
    };

    return configurationDescriptor;
}

const u8* tud_hid_descriptor_report_cb(u8 instance)
{
    (void)instance;

    static const u8 reportDescriptor[] = { TUD_HID_REPORT_DESC_GAMEPAD() };
    return reportDescriptor;
}

const u16* tud_descriptor_string_cb(u8 index, u16 langid)
{
    switch (index)
    {
        case STRID_LANGID:
        {
            static const UsbStringDescriptor<2> descriptor(0x0409);
            return (const u16*)&descriptor;
        }
        case STRID_MANUFACTURER:
        {
            return USB_STRING_DESCRIPTOR(u"LNH");
        }
        case STRID_PRODUCT:
        {
            return USB_STRING_DESCRIPTOR(u"DSpico Gamepad Example");
        }
        case STRID_SERIAL:
        {
            return USB_STRING_DESCRIPTOR(u"123456789");
        }
        default:
        {
            return nullptr;
        }
    }
}
