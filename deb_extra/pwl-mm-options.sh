#!/bin/bash

mm_options=('--test-low-power-suspend-resume' '--test-quick-suspend-resume')
device=`lspci -D -nn | grep 14c0:4d75`

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

if [[ -z "$device" ]]; then
    $exec_command
else
    help=`ModemManager -h`
    available_options=()
    for opt in ${mm_options[@]}
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
