#! /usr/bin/env bash

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
dest_file="/lib/firmware/xone_dongle_02fe.bin"

if [[ ! -f $dest_file ]]; then
    curl -L -o driver.cab "$driver_url"
    cabextract -F FW_ACC_00U.bin driver.cab
    echo "$firmware_hash" FW_ACC_00U.bin | sha256sum -c
    mv FW_ACC_00U.bin $dest_file
    rm driver.cab
else
    echo -e "xone_dongle_02fe.bin found. Skipping download\n"
fi

driver_url='https://catalog.s.download.windowsupdate.com/d/msdownload/update/driver/drvs/2017/03/2ea9591b-f751-442c-80ce-8f4692cdc67b_6b555a3a288153cf04aec6e03cba360afe2fce34.cab'
firmware_hash='080ce4091e53a4ef3e5fe29939f51fd91f46d6a88be6d67eb6e99a5723b3a223'
dest_file="/lib/firmware/xone_dongle_02e6.bin"

if [[ ! -f $dest_file ]]; then
    curl -L -o driver.cab "$driver_url"
    cabextract -F FW_ACC_00U.bin driver.cab
    echo "$firmware_hash" FW_ACC_00U.bin | sha256sum -c
    mv FW_ACC_00U.bin $dest_file
    rm driver.cab
else
    echo -e "xone_dongle_02e6.bin found. Skipping download\n"
fi

driver_url='https://catalog.s.download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/06/1dbd7cb4-53bc-4857-a5b0-5955c8acaf71_9081931e7d664429a93ffda0db41b7545b7ac257.cab'
firmware_hash='0023a7bae02974834500c665a281e25b1ba52c9226c84989f9084fa5ce591d9b'
dest_file="/lib/firmware/xone_dongle_02f9.bin"

if [[ ! -f $dest_file ]]; then
    curl -L -o driver.cab "$driver_url"
    cabextract -F FW_ACC_CL.bin driver.cab
    echo "$firmware_hash" FW_ACC_CL.bin | sha256sum -c
    mv FW_ACC_CL.bin $dest_file
    rm driver.cab
else
    echo -e "xone_dongle_02f9.bin found. Skipping download\n"
fi

driver_url='https://catalog.s.download.windowsupdate.com/c/msdownload/update/driver/drvs/2018/09/e5339a2a-0cbf-4100-ae09-81dab77d8ab2_56ceef39e5f673aa1ea7dd3c17e71b5bd2add2f7.cab'
firmware_hash='cef0b2a1a94c5a6407f1d198354d712eddc86dbc61a87223f4e4c53fe10ac92e'
dest_file="/lib/firmware/xone_dongle_091e.bin"

if [[ ! -f $dest_file ]]; then
    curl -L -o driver.cab "$driver_url"
    cabextract -F FW_ACC_BR.bin driver.cab
    echo "$firmware_hash" FW_ACC_BR.bin | sha256sum -c
    mv FW_ACC_BR.bin $dest_file
    rm driver.cab
else
    echo -e "xone_dongle_091e.bin found. Skipping download\n"
fi

echo -e "\n\nDongle firmwares installed\n"

