#ifndef AD00020_HARDWARE_PROFILE_H
#define AD00020_HARDWARE_PROFILE_H

#define PROGRAMMABLE_WITH_USB_HID_BOOTLOADER
#define DEMO_BOARD AD00020_USB_IR_SCANNER
#define AD00020_USB_IR_SCANNER
#define CLOCK_FREQ 48000000

#define tris_self_power     TRISAbits.TRISA2
#define self_power          1
#define tris_usb_bus_sense  TRISAbits.TRISA1
#define USB_BUS_SENSE       1
#define INPUT_PIN           1
#define OUTPUT_PIN          0

#endif
