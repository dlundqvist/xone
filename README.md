<p align="center">
    <img src="logo.svg" alt="Logo" width="200">
</p>

<p align="center">
    <a href="https://github.com/dlundqvist/xone/releases/latest"><img src="https://img.shields.io/github/v/release/dlundqvist/xone?logo=github" alt="Release Badge"></a>
    <a href="https://discord.gg/T3dSC3ReuS"><img src="https://img.shields.io/discord/733964971842732042?label=discord&logo=discord" alt="Discord Badge"></a>
</p>

`xone` is a Linux kernel driver for Xbox One and Xbox Series X|S accessories. It serves as a modern replacement for
`xpad`, aiming to be compatible with Microsoft's *Game Input Protocol* (GIP).

**NOTE**: This is a fork, please support the upstream project.

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
- [x] Power management (suspend/resume and remote/wireless wakeup)

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

## Releases

[![Packaging status](https://repology.org/badge/vertical-allrepos/xone.svg)](https://repology.org/project/xone/versions)

Feel free to package `xone` for any Linux distribution or hardware you like.
Any issues regarding the packaging should be reported to the respective maintainers.

## Building and testing

### Prerequisites

- Linux 5.13+
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
- curl (for firmware download)
- cabextract (for firmware extraction)
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

4. Download the firmware for the wireless dongle (optional, makefile automatically installs firmware):

```
sudo install/firmware.sh
```

**NOTE:** The `--skip-disclaimer` flag might be useful for scripting purposes.

5. Plug in your Xbox devices.

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

To fix that, there is a fork of `xpad` driver, called [`xpad-noone`](https://github.com/medusalix/xpad-noone) that
has disabled support for Xbox One controllers, so it can coexist with `xone` driver. If you're using Xbox 360
controllers, it is recommended to use it to replace the standard `xpad` driver.

### Installation on Steam Deck

An installation script for the Steam Deck is available
[here](https://gist.github.com/SavageCore/263a3413532bc181c9bb215c8fe6c30d). It handles all the prerequisites and other
quirks, along with installing `xone-noone`.

You can run it by executing the following command: `wget -O /tmp/bootstrap.sh
https://gist.githubusercontent.com/SavageCore/263a3413532bc181c9bb215c8fe6c30d/raw/8cfbc292c4b55612a2ebea3227911a3c3a6ae214/bootstrap.sh &&
sh /tmp/bootstrap.sh`

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
cat /sys/bus/usb/drivers/xone-dongle/*/pairing
```

You can enable (`1`) or disable (`0`) the pairing using the following command:

```
echo 1 | sudo tee /sys/bus/usb/drivers/xone-dongle/*/pairing
```

## Troubleshooting

Uninstall the release version and install a debug build of `xone` (see installation guide).
Run `sudo dmesg` to gather logs and check for any error messages related to `xone`.
If `xone` is not being loaded automatically you might have to reboot your system.

### Error messages

- `Direct firmware load for xow_dongle.bin failed with error -2`
    - Download the firmware for the wireless dongle (see installation guide).

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
