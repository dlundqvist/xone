#! /usr/bin/env bash

set -eu

get_version() {
    echo $(dkms status | grep xone | head -n 1 | tr -s ',:/' ' ' | cut -d ' ' -f 2)
}

if [ "$(id -u)" -ne 0 ]; then
    echo 'This script must be run as root!' >&2
    exit 1
fi

modules=$(lsmod | grep '^xone_' | cut -d ' ' -f 1 | tr '\n' ' ')
if [ -n "$modules" ]; then
    echo "Unloading modules: $modules..."
    # shellcheck disable=SC2086
    modprobe -r -a $modules || true
fi

version=$(get_version)
while [[ -n $version ]]; do
    echo -e "Uninstalling xone $version...\n"
    dkms remove -m xone -v "$version" --all

    version=$(get_version)
done

rm -rf /usr/src/xone* || true
rm -rf /etc/modprobe.d/xone-blacklist.conf || true
rm -rf /var/lib/dkms/xone* || true
echo -e "All xone versions removed\n"

[[ ${1:-} == "--no-firmware" ]] && exit 0
rm -rf /lib/firmware/xow_dongle*
echo -e "All dongle firmwares removed\n"
