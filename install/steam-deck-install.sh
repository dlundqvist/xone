#! /usr/bin/env bash

if [[ $1 == "remove" ]]; then
	sudo pacman -Rcns xone-dkms

	echo ""
	echo ""
	echo "Done!"
	echo "Just reboot your Deck :)"

	exit 0
fi

ro_status=$(steamos-readonly status)
if [[ $ro_status == "enabled" ]]; then
    echo "Disabling readonly"
    echo ""
    steamos-readonly disable
fi

pacman-key --init
pacman-key --populate archlinux
pacman-key --populate holo

mkdir xone-install
cd xone-install || exit 1

AUR_LINK="https://aur.archlinux.org/cgit/aur.git/plain/PKGBUILD?h="
ITER=0
while [[ ! -e PKGBUILD_XONE && "$ITER" -lt 5 ]]; do
	curl "${AUR_LINK}xone-dkms" -o PKGBUILD_XONE
	ITER=$(( ITER + 1 ))
done

if [[ $ITER -eq 5 ]]; then
	echo "Error when downloading PKGBUILD for xone. Exiting..."
	exit 1
fi

ITER=0
while [[ ! -e PKGBUILD_FIRMWARE && "$ITER" -lt 5 ]]; do
	curl "${AUR_LINK}xone-dkms" -o PKGBUILD_FIRMWARE
	ITER=$(( ITER + 1 ))
done

if [[ $ITER -eq 5 ]]; then
	echo "Error when downloading PKGBUILD for xone firmware. Exiting..."
	exit 1
fi

# to ABSOLUTELY make sure we have acces when running sudo -u deck
chown -R deck:deck .
chmod 777 .

echo ""
echo "Don't worry about \"error: command failed to execute correctly\""
echo ""

linux=$(pacman -Qsq linux-neptune | grep -e "[0-9]$" | tail -n 1)
pacman -Syu --noconfirm base-devel fakeroot glibc git \
    "$linux" "$linux-headers" linux-api-headers

# Install build dependencies manually
pacman -Syu --noconfirm --asdeps dkms w3m html-xml-utils

# build and install seaprately to avoid repeated password prompts
sudo -u deck makepkg -Cc -p PKGBUILD_XONE
sudo -u deck makepkg -Cc -p PKGBUILD_FIRMWARE

pacman -U --noconfirm xone-dkms-*.tar.zst
pacman -U --noconfirm --asdeps xone-dongle-firmware-*.tar.zst

# Remove unneeded build dependencies
pacman -Rcns --noconfirm w3m html-xml-utils

echo ""
echo "Again, don't worry about this ^"

cd ..
rm -rf xone-install

echo ""
echo ""
echo "Done!"
echo "Just reboot your Deck :)"
