#! /usr/bin/env bash
# Sets up the new wireless dongle (045e:02fe) to run on mt76x2u and xone_mtnet
# with MediaTek's firmware, including wake on wireless, or removes that setup
# with --remove. xone_dongle no longer claims 045e:02fe, so after --remove that
# dongle works only as a plain mt76x2u Wi-Fi adapter; the old and built-in
# dongles stay on xone_dongle either way. Requires iw. Replug the dongle afterwards.
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo 'This script must be run as root!' >&2
    exit 1
fi

cd "$(dirname "$(readlink -f "$0")")"

if [ "${1:-}" == --remove ]; then
    rm -f /etc/udev/rules.d/70-xone-mtnet.rules /usr/libexec/xone-mtnet-setup \
          /etc/systemd/sleep.conf.d/60-xone-mtnet.conf
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
    install -D -m 644 sleep.conf /etc/systemd/sleep.conf.d/60-xone-mtnet.conf
fi

udevadm control --reload
echo 'Done. Replug the dongle.'
