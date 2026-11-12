<p align="center">
    <img src="logo.svg" alt="Logo" width="200">
</p>

<p align="center">
    <a href="https://github.com/dlundqvist/xone/releases/latest"><img src="https://img.shields.io/github/v/release/dlundqvist/xone?logo=github" alt="Release Badge"></a>
    <a href="https://discord.gg/T3dSC3ReuS"><img src="https://img.shields.io/discord/733964971842732042?label=discord&logo=discord" alt="Discord Badge"></a>
</p>

`xone` is a Linux kernel driver for Xbox One and Xbox Series X|S accessories. It serves as a modern replacement for
`xpad`, aiming to be compatible with Microsoft's *Game Input Protocol* (GIP).

> [!NOTE]
> The original project is in maintance mode, please refer to this one for updates and issues.
> 
> Huge thanks to medusalix for all the work and creating this driver!

## Compatibility

- [x] Wired devices (via USB)
- [x] Wireless devices (with Xbox Wireless Dongle)
- [ ] Bluetooth devices (check out [`xpadneo`](https://github.com/atar-axis/xpadneo))

Installing `xone` will disable the `xpad` kernel driver. If you are still using Xbox or Xbox 360 peripherals,
you will have to install [`xpad-noone`](https://github.com/forkymcforkface/xpad-noone) as a replacement for `xpad`.

## Important notes

This driver is still in active development. Use at your own risk!
If you are running `xow` upgrading to `xone` is *highly recommended*!
Always update your Xbox devices to the latest firmware version!
**Any feedback including bug reports, suggestions or ideas is [*greatly appreciated*](https://discord.gg/J7kgN5Wm).**

## Features

- [x] Input and force feedback (rumble)
- [x] Battery reporting (`UPower` integration)
- [x] LED control (using `/sys/class/leds`)
- [x] Audio capture/playback (through `ALSA`)
- [x] Power management (suspend/resume, and remote wakeup over USB)

## Supported devices

- [x] Gamepads
    - [x] Xbox One Controllers
    - [x] Xbox Series X|S Controllers
    - [x] Xbox Adaptive Controller
    - [x] Third party controllers (PowerA, PDP, etc.)
- [x] Headsets
    - [x] Xbox One Chat Headset
    - [x] Xbox One Stereo Headset (adapter or jack)
    - [x] Xbox Wireless Headset
    - [x] Third party wired and wireless headsets (SteelSeries, Razer, etc.)
- [x] Guitars & Drums
    - [x] Mad Catz Rock Band 4 Wireless Fender Stratocaster
    - [x] Mad Catz Rock Band 4 Wireless Drum Kit
    - [x] PDP Rock Band 4 Wireless Fender Jaguar
- [x] Xbox One Chatpad
- [ ] Third party racing wheels (Thrustmaster, Logitech, etc.)

## Wireless dongle

The new Xbox Wireless Dongle (`045e:02fe`) runs on the kernel's `mt76x2u` driver and Mediatek's firmware from
`linux-firmware`, through the `xone_mtnet` module. No firmware from Microsoft is needed. `make install` sets this up,
and the dongle is ready once it's replugged.

The old dongle (`045e:02e6`) and the built-in dongles (`045e:02f9` and `045e:091e`) don't run on `mt76x2u`, so they
use the `xone_dongle` module and Microsoft's firmware (see *Dongles that need Microsoft's firmware*).

The dongle's button and LED work as usual: press the button to start pairing, and the LED blinks while pairing, stays
on while a controller is connected, and is off otherwise.

Connected controllers are turned off when the computer goes to sleep, and after 15 minutes without input (moving a
stick or trigger, pressing a button, or headset audio). Change the idle time with the `idle_timeout` module parameter,
in minutes, or set it to `0` to keep controllers on:

```
# for the running module
echo 30 | sudo tee /sys/module/xone_mtnet/parameters/idle_timeout

# from the next boot
echo 'options xone_mtnet idle_timeout=30' | sudo tee /etc/modprobe.d/xone-mtnet-idle.conf
```

### Wake on wireless

With `xone_mtnet`, turning on a paired controller wakes the computer from suspend (the `wake` module parameter, on by
default). It works with whichever sleep mode the system uses, s2idle or S3 (deep), and has been tested with both. For
S3 the motherboard has to keep the USB port powered while asleep and allow USB devices to wake it; on some boards that
is a firmware setup option. After waking, the dongle is re-probed and the controller reconnects within a few seconds.

Known limits:

- The channel is fixed, and channel 153 is the default. Set a different one with the `channel` module parameter. Unlike
  Microsoft's firmware, the module doesn't look for a quiet channel. Only channel 153 has been tested since the timing
  fixes, and channels 36 and 40 dropped controllers before them.
- More than one dongle isn't supported.
- Waking takes up to about 10 seconds longer than the controller's normal connection time, because the dongle checks
  for a controller about every 10 seconds while the computer sleeps.
- Wake on wireless relies on a small patch to Mediatek's firmware in the dongle's memory that is specific to the
  firmware build in `linux-firmware`.
- Only the Xbox One S (1708) and Elite Series 1 controllers have been tested. Wireless headset audio is untested.

### Dongles that need Microsoft's firmware

The `xone_dongle` module drives the old and built-in dongles with Microsoft's firmware. The firmware can't be
redistributed, so it's downloaded from Microsoft, under Microsoft's Terms of Use, and needs `curl` and `bsdtar`:

```shell
sudo ./install/firmware.sh
```

The new dongle (`045e:02fe`) always uses `xone_mtnet`; `xone_dongle` no longer claims it.

> [!TIP]
> The `xone-dongle.fw_override=0x0000` module paramter can be used to load a different firmware file than the one selected automatically by the driver. The value is the USB PID contained in the fw file eg. `xone_dongle_02e6.bin`

## Releases

[![Packaging status](https://repology.org/badge/vertical-allrepos/xone.svg)](https://repology.org/project/xone/versions)

Feel free to package `xone` for any Linux distribution or hardware you like.
Any issues regarding the packaging should be reported to the respective maintainers.

## Building and testing

### Prerequisites

- Linux 6.5+
- Linux headers

### Automagically
Build the driver with debug flags, load modules, cleanup working directory
```shell
sudo make test
```

### Manually
Build the driver
```shell
make
# with debug
make debug
```

Load modules from the build directory
```shell
sudo make load
```

Unload all xone modules
```shell
# called automatically during load as well
sudo make unload
```

Clean all build files
```shell
make clean
```

## Installation

### Prerequisites

- Linux (kernel 5.13+ and headers)
- DKMS
- iw (to set up the wireless dongle)
- For SecureBoot-enabled systems see [SecureBoot dkms guide](https://github.com/dell/dkms#secure-boot)

### Guide

1. Unplug your Xbox devices.

2. Clone the repository:

```
git clone https://github.com/dlundqvist/xone
```

3. Install `xone`:

```
cd xone
sudo make install
```

**NOTE:** You can use the `install-debug` target instead to enable debug logging.

4. Plug in your Xbox devices.

### Updating

Just run the install script again after pulling the newset changes from the repository.

```
git pull
sudo make install
```

Reboot is highly suggested

### Steam Deck/SteamOS
#### Automatic install
First, let's set a password
```bash
# (optional, skip if you've already done this in the past)
passwd deck
```
Run installation script
```bash
sudo sh -c "$(curl -fsSL https://raw.githubusercontent.com/dlundqvist/xone/master/install/steam-deck-install.sh)"
```
#### Uninstall:
```bash
sudo pacman -Rcns xone-dkms
```
Optionally, lock your deck and  remove password
```bash
steamos-readonly enable
# enter current one and leave the new password blank
passwd deck
```

### Using Xbox 360 controllers with xone

`xone` doesn't support Xbox 360 controllers at all. On top of that, `xone` needs to disable `xpad` driver to work
properly, which would normally support Xbox 360 controllers. This is due to `xpad` also trying to handle Xbox One
controllers, which `xone` aims to support.

To fix that, there is a fork of [xpad](https://github.com/paroj/xpad) driver, called [xpad-noone](https://github.com/forkymcforkface/xpad-noone) that
has disabled support for Xbox One controllers, so it can coexist with `xone` driver. If you're using Xbox 360
controllers, it is recommended to use it to replace the standard `xpad` driver.

## Wireless pairing

Xbox devices have to be paired to the wireless dongle. They will not automatically connect to the dongle if they have
been previously plugged into a USB port or used via Bluetooth.

Instructions for pairing your devices can be found
[here](https://support.xbox.com/en-US/help/hardware-network/controller/connect-xbox-wireless-controller-to-pc)
(see the section on *Xbox Wireless*).

## Kernel interface

### LED control

The guide button LED can be controlled via `sysfs`:

```
echo 2 | sudo tee /sys/class/leds/gip*/mode
echo 5 | sudo tee /sys/class/leds/gip*/brightness
```

Changing the LED in the above way is temporary, it will only last until the device disconnects. To apply these settings
automatically when a device connects, you can create a new `udev` rule in `/etc/udev/rules.d/50-xone.rules` with
the following content:

```
ACTION=="add", SUBSYSTEM=="leds", KERNEL=="gip*", ATTR{mode}="2", ATTR{brightness}="5"
```

Replace the wildcard (`gip*`) if you want to control the LED of a specific device.
The modes and the maximum brightness can vary from device to device.

### Pairing mode

The pairing mode of the dongle can be queried via `sysfs`:

```
cat /sys/module/xone_mtnet/parameters/pairing
```

You can enable (`1`) or disable (`0`) the pairing using the following command. Pairing mode ends by itself after 60
seconds or when a controller pairs.

```
echo 1 | sudo tee /sys/module/xone_mtnet/parameters/pairing
```

With Microsoft's firmware, use `/sys/bus/usb/drivers/xone-dongle/*/pairing` instead.

## Number of active clients and forcing poweroff

These are only available with Microsoft's firmware. With `xone_mtnet`, controllers are powered off automatically when
idle and when the computer sleeps (see *Wireless dongle*).

```
# show number of connected controllers
cat /sys/bus/usb/drivers/xone-dongle/*/active_clients

# power off selected client (possible values from 0 to 15)
sudo tee /sys/bus/usb/drivers/xone-dongle/*/poweroff <<< 1

# power off all connected clients
sudo tee /sys/bus/usb/drivers/xone-dongle/*/poweroff <<< -1
```

## Troubleshooting

Uninstall the release version and install a debug build of `xone` (see installation guide).
Run `sudo dmesg` to gather logs and check for any error messages related to `xone`.
If `xone` is not being loaded automatically you might have to reboot your system.

### Error messages

- `Direct firmware load for xone_dongle_02fe.bin failed with error -2`
    - An old or built-in dongle is in use but Microsoft's firmware isn't installed. Download it (see *Dongles that need
      Microsoft's firmware*).

### Input issues

You can use `evtest` and `fftest` to check the input and force feedback functionality of your devices.

### Other problems

Please join the [Discord server](https://discord.gg/T3dSC3ReuS) in case of any other problems.

## License

`xone` is released under the [GNU General Public License, Version 2](LICENSE).

```
Copyright (C) 2021 Severin von Wnuck-Lipinski

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
```
