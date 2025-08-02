#!/bin/sh

set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo 'This script must be run as root!' >&2
    exit 1
fi

if ! [ -x "$(command -v curl)" ]; then
    echo 'This script requires curl!' >&2
    exit 1
fi

if ! [ -x "$(command -v cabextract)" ]; then
    echo 'This script requires cabextract!' >&2
    exit 1
fi

if [ "${1:-}" != --skip-disclaimer ]; then
    echo "The firmware for the wireless dongle is subject to Microsoft's Terms of Use:"
    echo 'https://www.microsoft.com/en-us/legal/terms-of-use'
    echo
    echo 'Press enter to continue!'
    read -r _
fi

echo -e "dongle firmware installation...\n"

driver_url='https://catalog.s.download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab'
firmware_hash='48084d9fa53b9bb04358f3bb127b7495dc8f7bb0b3ca1437bd24ef2b6eabdf66'
dest_file="/lib/firmware/xow_dongle.bin"

if [[ ! -f $dest_file ]]; then
    curl -L -o driver.cab "$driver_url"
    cabextract -F FW_ACC_00U.bin driver.cab
    echo "$firmware_hash" FW_ACC_00U.bin | sha256sum -c
    mv FW_ACC_00U.bin $dest_file
    rm driver.cab
else
    echo -e "xow_dongle.bin found. Skipping download\n"
fi

driver_url='https://catalog.s.download.windowsupdate.com/d/msdownload/update/driver/drvs/2015/12/20810869_8ce2975a7fbaa06bcfb0d8762a6275a1cf7c1dd3.cab'
firmware_hash='080ce4091e53a4ef3e5fe29939f51fd91f46d6a88be6d67eb6e99a5723b3a223'
dest_file="/lib/firmware/xow_dongle_045e_02e6.bin"

if [[ ! -f $dest_file ]]; then
    curl -L -o driver.cab "$driver_url"
    cabextract -F FW_ACC_00U.bin driver.cab
    echo "$firmware_hash" FW_ACC_00U.bin | sha256sum -c
    mv FW_ACC_00U.bin $dest_file
    rm driver.cab
else
    echo -e "xow_dongle_045e_02e6.bin found. Skipping download\n"
fi

echo -e "dongle firmware installed\n"
