# Third-party notices

The MIT license in `LICENSE` applies only to files authored for this project.
It does not relicense the third-party files listed below.

## Microchip MCHPFSUSB stack

The directory `src/Microchip/` contains the Microchip USB device/HID stack
used by the firmware. The files retain their original Microchip copyright and
software-license notices. Those notices govern use, modification, copying,
and distribution of those files; they are not replaced by the project MIT
license. The notices limit the intended use to Microchip PIC microcontroller
products.

## tiny-AES-C

`src/crypto/aes.c` and `src/crypto/aes.h` are distributed under the Unlicense.
The applicable license text is included in `src/crypto/unlicense.txt`.

## Bit Trade One / AD00020

This repository is an unofficial firmware project targeting the Bit Trade One
AD00020 hardware. It does not include Bit Trade One schematics, artwork, or
other Assembly Desk design-material files. The AD00020 and Bit Trade One names
remain the property of their respective owners.

If Bit Trade One design materials are added in the future, their applicable
Assembly Desk License notice must be retained separately. See the official
license page: https://bit-trade-one.co.jp/adl/

## Rust configuration tool

The Rust configuration tool under `config-tool/` obtains its dependencies
through Cargo; dependencies are not vendored in this repository. `config-tool/Cargo.lock`
pins the resolved dependency versions. Binary distribution is blocked until
all direct and transitive dependency licenses and required notices have been
inventoried and packaged with the release.
