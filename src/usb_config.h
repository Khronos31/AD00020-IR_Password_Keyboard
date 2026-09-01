#ifndef AD00020_USB_CONFIG_H
#define AD00020_USB_CONFIG_H

#define USB_EP0_BUFF_SIZE       8
#define USB_MAX_NUM_INT         1
#define USB_MAX_EP_NUMBER       1

#define USB_USER_DEVICE_DESCRIPTOR             &device_dsc
#define USB_USER_DEVICE_DESCRIPTOR_INCLUDE     extern ROM USB_DEVICE_DESCRIPTOR device_dsc
#define USB_USER_CONFIG_DESCRIPTOR             USB_CD_Ptr
#define USB_USER_CONFIG_DESCRIPTOR_INCLUDE     extern ROM BYTE *ROM USB_CD_Ptr[]

#define USB_PING_PONG_MODE       USB_PING_PONG__FULL_PING_PONG
#define USB_FULL_PING_PONG
#define USB_EP0_OUT_ONLY
#define USB_POLLING

#define USB_PULLUP_OPTION        USB_PULLUP_ENABLE
#define USB_TRANSCEIVER_OPTION   USB_INTERNAL_TRANSCEIVER
#define USB_SPEED_OPTION         USB_FULL_SPEED

/* Keep the factory identity so the existing board is treated as the same
 * generic HID device by hosts.  This firmware changes the product string. */
#define MY_VID                   0x22EA
#define MY_PID                   0x001E

#define USB_SUPPORT_DEVICE
#define USB_NUM_STRING_DESCRIPTORS 3
#define USB_ENABLE_ALL_HANDLERS
#define USB_USE_HID

#define HID_INTF_ID              0x00
#define HID_EP                   1
#define HID_INT_OUT_EP_SIZE      1
#define HID_INT_IN_EP_SIZE       8
#define HID_NUM_OF_DSC           1

/* The old HID stack declares four report symbols.  Only report 01 is used;
 * the remaining three are one-byte dummies defined by usb_descriptors.c. */
#define HID_RPT01_SIZE           63
#define HID_RPT02_SIZE           1
#define HID_RPT03_SIZE           1
#define HID_RPT04_SIZE           1

#endif
