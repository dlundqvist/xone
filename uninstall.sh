#!/usr/bin/env sh

set -eu

get_version() {
    echo $(dkms status xone | head -n 1 | tr -s ',:/' ' ' | cut -d ' ' -f 2)
}

if [ "$(id -u)" -ne 0 ]; then
    echo 'This script must be run as root!' >&2
    exit 1
fi

modules=$(lsmod | grep '^xone_' | cut -d ' ' -f 1 | tr '\n' ' ')
if [ -n "$modules" ]; then
    echo "Unloading modules: $modules..."
    # shellcheck disable=SC2086
    modprobe -r -a $modules
fi

version=$(get_version)
while [[ -n $version ]]; do
    echo -e "Uninstalling xone $version...\n"

    dkms remove -m xone -v "$version" --all
    rm -r "/usr/src/xone-$version"
    rm -f /etc/modprobe.d/xone-blacklist.conf

    version=$(get_version)
done

echo -e "All xone versions removed\n"
