#! /usr/bin/env bash
# Switches an installed xone from Microsoft's dongle firmware to mt76x2u and
# xone_mtnet, or back with --remove. Requires iw. Replug the dongle afterwards.
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo 'This script must be run as root!' >&2
    exit 1
fi

cd "$(dirname "$(readlink -f "$0")")"

if [ "${1:-}" == --remove ]; then
    rm -f /etc/udev/rules.d/70-xone-mtnet.rules /usr/libexec/xone-mtnet-setup
    install -D -m 644 ../modprobe.conf /etc/modprobe.d/xone-blacklist.conf
    modprobe -r xone_mtnet mt76x2u || true
else
    if ! [ -x "$(command -v iw)" ]; then
        echo 'This script requires iw!' >&2
        exit 1
    fi
    install -D -m 755 xone-mtnet-setup /usr/libexec/xone-mtnet-setup
    install -D -m 644 70-xone-mtnet.rules /etc/udev/rules.d/70-xone-mtnet.rules
    install -D -m 644 modprobe.conf /etc/modprobe.d/xone-blacklist.conf
    modprobe -r xone_dongle || true
fi

udevadm control --reload
echo 'Done. Replug the dongle.'
