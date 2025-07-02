#! /usr/bin/env bash

OPERATION="insmod"
SUFFIX=".ko"
MESSAGE="Loading"

mapfile -t MODULES_TMP < modules.order
MODULES=("${MODULES_TMP[@]}")

LOADED_MODULES=$(lsmod)

if [[ $1 == "unload" ]]; then
    OPERATION="rmmod -f"
    SUFFIX=""
    MESSAGE="Unloading"

    # array reversing for rmmod
    len=${#MODULES[@]}
    for ((i = 0 ; i < $len ; i++)); do
        MODULES[$i]=${MODULES_TMP[(( $len - $i - 1 ))]}
    done

    [[ $LOADED_MODULES =~ "xpad" ]] && rmmod -f xpad
    [[ $LOADED_MODULES =~ "mt76x2u" ]] && rmmod -f mt76x2u
fi

# make sure ff-memless is loaded as it exports some needed symbols
if [[ $1 != "unload" && ! "$LOADED_MODULES" =~ "ff-memless" ]]; then
    modprobe ff-memless
fi

for module in "${MODULES[@]}"; do
    module="${module%.o}$SUFFIX"

    # skip rmmod if module is not loaded
    [[ $1 == "unload" && ! "$LOADED_MODULES" =~ "$module" ]] && continue

    echo "$MESSAGE $module"
    $OPERATION "$module"
done
