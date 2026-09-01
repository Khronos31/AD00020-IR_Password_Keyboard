#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$PROJECT_DIR/build"
XC8=${XC8:-/config/.tools/xc8-v4.00/bin/xc8-cc}
DFP=${DFP:-/config/.tools/packs/PIC18F-K_DFP/1.16.308/xc8}
DB_HEADER=${DB_HEADER:-$PROJECT_DIR/include/generated_database.h}

if [ ! -f "$DB_HEADER" ]; then
  echo "missing encrypted database: $DB_HEADER" >&2
  echo "run tools/provision_db.py first" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$PROJECT_DIR/src"

"$XC8" \
  -mcpu=18F14K50 \
  -mdfp="$DFP" \
  -D__18CXX \
  -mcodeoffset=0x1000 \
  -Os \
  -I. -I"$PROJECT_DIR/include" -IMicrochip/Include \
  -Wl,-Map="$BUILD_DIR/AD00020-IR-Password-Keyboard.map" \
  -o "$BUILD_DIR/AD00020-IR-Password-Keyboard.hex" \
  main.c usb_descriptors.c \
  crypto/aes.c \
  Microchip/USB/usb_device.c \
  "Microchip/USB/HID Device Driver/usb_function_hid.c"

echo "HEX: $BUILD_DIR/AD00020-IR-Password-Keyboard.hex"
