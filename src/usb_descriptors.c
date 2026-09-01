#include "GenericTypeDefs.h"
#include "Compiler.h"
#include "usb_config.h"
#include "USB/usb.h"
#include "USB/usb_function_hid.h"

#if defined(__18CXX)
#pragma romdata
#endif

ROM USB_DEVICE_DESCRIPTOR device_dsc =
{
    0x12,
    USB_DESCRIPTOR_DEVICE,
    0x0200,
    0x00,
    0x00,
    0x00,
    USB_EP0_BUFF_SIZE,
    MY_VID,
    MY_PID,
    0x0001,
    0x01,
    0x02,
    0x00,
    0x01
};

/* One boot-protocol keyboard interface with an 8-byte IN and 1-byte OUT
 * interrupt endpoint. */
ROM BYTE configDescriptor1[] =
{
    0x09,
    USB_DESCRIPTOR_CONFIGURATION,
    DESC_CONFIG_WORD(0x0029),
    0x01,
    0x01,
    0x00,
    _DEFAULT,
    0x32,

    0x09,
    USB_DESCRIPTOR_INTERFACE,
    0x00,
    0x00,
    0x02,
    HID_INTF,
    BOOT_INTF_SUBCLASS,
    HID_PROTOCOL_KEYBOARD,
    0x00,

    0x09,
    DSC_HID,
    DESC_CONFIG_WORD(0x0111),
    0x00,
    HID_NUM_OF_DSC,
    DSC_RPT,
    DESC_CONFIG_WORD(HID_RPT01_SIZE),

    0x07,
    USB_DESCRIPTOR_ENDPOINT,
    HID_EP | _EP_IN,
    _INTERRUPT,
    DESC_CONFIG_WORD(HID_INT_IN_EP_SIZE),
    0x01,

    0x07,
    USB_DESCRIPTOR_ENDPOINT,
    HID_EP | _EP_OUT,
    _INTERRUPT,
    DESC_CONFIG_WORD(HID_INT_OUT_EP_SIZE),
    0x01
};

ROM HID_REPORT01 hid_rpt01 =
{
    {
        0x05, 0x01,
        0x09, 0x06,
        0xA1, 0x01,
        0x05, 0x07,
        0x19, 0xE0,
        0x29, 0xE7,
        0x15, 0x00,
        0x25, 0x01,
        0x75, 0x01,
        0x95, 0x08,
        0x81, 0x02,
        0x95, 0x01,
        0x75, 0x08,
        0x81, 0x03,
        0x95, 0x05,
        0x75, 0x01,
        0x05, 0x08,
        0x19, 0x01,
        0x29, 0x05,
        0x91, 0x02,
        0x95, 0x01,
        0x75, 0x03,
        0x91, 0x03,
        0x95, 0x06,
        0x75, 0x08,
        0x15, 0x00,
        0x25, 0x65,
        0x05, 0x07,
        0x19, 0x00,
        0x29, 0x65,
        0x81, 0x00,
        0xC0
    }
};

/* Required by the legacy HID function driver's generic declarations. */
ROM HID_REPORT02 hid_rpt02 = {{0}};
ROM HID_REPORT03 hid_rpt03 = {{0}};
ROM HID_REPORT04 hid_rpt04 = {{0}};

ROM struct { BYTE bLength; BYTE bDscType; WORD string[1]; } sd000 =
{
    sizeof(sd000), USB_DESCRIPTOR_STRING, {0x0409}
};

ROM struct { BYTE bLength; BYTE bDscType; WORD string[18]; } sd001 =
{
    sizeof(sd001), USB_DESCRIPTOR_STRING,
    {'K','h','r','o','n','o','s','3','1',' ','P','r','o','j','e','c','t','.'}
};

ROM struct { BYTE bLength; BYTE bDscType; WORD string[28]; } sd002 =
{
    sizeof(sd002), USB_DESCRIPTOR_STRING,
    {'A','D','0','0','0','2','0',' ','I','R',' ','P','a','s','s','w','o','r','d',' ','K','e','y','b','o','a','r','d'}
};

ROM BYTE *ROM USB_CD_Ptr[] =
{
    (ROM BYTE *ROM)&configDescriptor1
};

ROM BYTE *ROM USB_SD_Ptr[] =
{
    (ROM BYTE *ROM)&sd000,
    (ROM BYTE *ROM)&sd001,
    (ROM BYTE *ROM)&sd002
};
