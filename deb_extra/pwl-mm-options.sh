#!/bin/bash

MM_VERSION=$(ModemManager --version | head -n 1 | awk '{print $2}')

mm_version_ge() {
    [ "$(printf '%s\n' "$1" "$2" | sort -V | head -n1)" = "$2" ]
}

mm_options=('--test-low-power-suspend-resume' '--test-quick-suspend-resume')
mm_base_option=('--test-quick-suspend-resume')
device=`lspci -D -nn | grep 14c0:4d75`

usb_mm_options=('--test-quick-suspend-resume')
usb_device=`lsusb | grep 413c:8217`

service_file=$(systemctl show ModemManager -p FragmentPath --value)
if [[ -z "$service_file" ]]; then
    echo "Service file not found for ModemManager."
    exit 1
fi

exec_start=$(grep -m 1 '^ExecStart=' "$service_file")
if [[ -z "$exec_start" ]]; then
    echo "ExecStart not found in $service_file."
    exit 1
fi

exec_command=${exec_start#ExecStart=}

if [ -z "$device" ] && [ -z "$usb_device" ]; then
    $exec_command
else
    help=`ModemManager -h`
    available_options=()
    options_to_use=()

    if [ -n "$device" ]; then
        if mm_version_ge "$MM_VERSION" "1.24.2"; then
            options_to_use=("${mm_options[@]}")
        else
            options_to_use=("${mm_base_option[@]}")
        fi
    fi
    if [ -n "$usb_device" ]; then
        options_to_use=("${usb_mm_options[@]}")
    fi

    for opt in "${options_to_use[@]}"
    do
        if [[ -z "${help##*$opt*}" ]]; then
            available_options[${#available_options[@]}]=$opt
            break
        else
            echo "$opt not available!"
        fi
    done
    exec="$exec_command ${available_options[*]}"
    $exec
fi
