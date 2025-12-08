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

if ! [ -x "$(command -v bsdtar)" ]; then
    echo 'This script requires bsdtar!' >&2
    exit 1
fi

if [ "${1:-}" != --skip-disclaimer ]; then
    echo "The firmware for the wireless dongle is subject to Microsoft's Terms of Use:"
    echo 'https://www.microsoft.com/en-us/legal/terms-of-use'
    echo
    echo 'Press enter to continue!'
    read -r _
fi

echo -e "Dongle firmware installation\n"

urls=(
    'https://catalog.s.download.windowsupdate.com/d/msdownload/update/driver/drvs/2017/03/2ea9591b-f751-442c-80ce-8f4692cdc67b_6b555a3a288153cf04aec6e03cba360afe2fce34.cab'
    'https://catalog.s.download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab'
    'https://catalog.s.download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/06/1dbd7cb4-53bc-4857-a5b0-5955c8acaf71_9081931e7d664429a93ffda0db41b7545b7ac257.cab'
    'https://catalog.s.download.windowsupdate.com/d/msdownload/update/driver/drvs/2017/08/aeff215c-3bc4-4d36-a3ea-e14bfa8fa9d2_e58550c4f74a27e51e5cb6868b10ff633fa77164.cab'
)
hashes=(
    '080ce4091e53a4ef3e5fe29939f51fd91f46d6a88be6d67eb6e99a5723b3a223'
    '48084d9fa53b9bb04358f3bb127b7495dc8f7bb0b3ca1437bd24ef2b6eabdf66'
    '0023a7bae02974834500c665a281e25b1ba52c9226c84989f9084fa5ce591d9b'
    'e2710daf81e7b36d35985348f68a81d18bc537a2b0c508ffdfde6ac3eae1bad7'
)
filenames=('FW_ACC_00U.bin' 'FW_ACC_00U.bin' 'FW_ACC_CL.bin' 'FW_ACC_BR.bin')
pids=('02e6' '02fe' '02f9' '091e')

function download_firmware() {
    local firmware_name="xone_dongle_${1}.bin"
    local dest_file="/lib/firmware/$firmware_name"

    if [[ -f $dest_file ]]; then
        echo -e "$firmware_name found. Skipping download"
        return 0
    fi

    echo -n "Downloading $firmware_name..."
    curl -s -L -o driver.cab "$2"
    bsdtar -xf driver.cab "$3" > /dev/null 2>&1

    echo -n " Checking sha256..."
    echo "$4" "$3" | sha256sum -c --quiet
    mv "$3" $dest_file
    rm driver.cab

    echo -e " Done!"
}

for ((i = 0 ; i < "${#urls[@]}" ; i++)); do
    download_firmware "${pids[$i]}" "${urls[$i]}" "${filenames[$i]}" "${hashes[$i]}"
done

echo -e "\nDongle firmwares installed!"

