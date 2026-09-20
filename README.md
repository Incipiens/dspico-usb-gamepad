# DSpico USB Gamepad: use your DS as a PC controller
A DS homebrew app that allows the device to enumerate over the DSpico's USB port as a standard HID gamepad. No driver is required on the PC side.

Button states are sent every VBlank to the host as a HID gamepad report through tinyusb, and button states are read via the ARM7. The D-pad becomes a hat switch, with face and shoulder buttons mapped by physical position instead of their label. X and Y aren't wired to `KEYINPUT`, so they're read separately from the ARM7-only `EXTKEYIN` register. 

The touchscreen simulates the right stick input, with pen position being scaled to a deflection from the centre of the screen. The stick re-centres when the user is no longer touching the display.

Written for [an XDA article](https://www.xda-developers.com/put-raspberry-pi-in-nintendo-3ds-pc-gamepad-webcam-microphone/).

## Building
Building requires a pre-calico devkitpro/libnds environment, plus the two submodules under `libs/`:

```sh
git clone --recursive https://github.com/Incipiens/dspico-usb-gamepad.git
cd dspico-usb-gamepad
make
```

If you already cloned without `--recursive`, run `git submodule update --init --recursive` first.

The easiest way to get a toolchain is the `devkitpro/devkitarm:20241104` Docker image:

```sh
docker run --rm -v "$PWD":/src -w /src devkitpro/devkitarm:20241104 make
```

`make` builds libtwl first before the ARM7 and ARM9 binaries. After building, `usb-gamepad.nds` will be compiled in the repo root.

## License
The example and the DSpico platform code in `platform/` are licensed under the Zlib license. For details, see `LICENSE.txt`.

## Credits
- [tinyusb](https://github.com/hathach/tinyusb) for the USB library. This example is also largely based on the examples that come with tinyusb.
- [libtwl](https://github.com/Gericom/libtwl) by [Gericom](https://github.com/Gericom) for the DS hardware and RTOS layer.
- The DSpico platform code and this example come from [dspico-usb-examples](https://github.com/LNH-team/dspico-usb-examples).
- Banner icon by [nitehack](https://www.github.com/nitehack)
