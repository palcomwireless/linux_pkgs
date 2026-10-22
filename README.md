# Palcom Wireless Linux service packages
This repository contents below pwl linux service packages to be used with WWAN modules.

`Core service` is the core controller of pwl services.

`Module adapter service` is the module communication adapter service.

`Preference service` is the config preference for the packages.

`FW Update service` is the firmware update service.

`FCC unlock service` performs the FCC unlock procedure for the module. (not open source)

## License
This Core, Module adapter, Preference and FW Update services are licensed under the [Apache License 2.0](LICENSE-APACHE) and the [GNU General Public License v2.0](LICENSE).

And FCC unlock service is licensed under BSD-3-Clause.

# Notice
* The minimum Ubuntu version that can run this package is Ubuntu 22.04.
* FW Update service must be used with pwl-firmware package.  Before installing the service package, ensure that pwl-firmware has been installed. Obtain the pwl firmware from the corresponding OEM.
* FCC unlock service default not enable, if you want to enable it, please manually copy `/opt/pwl/pwl-unlock/fcc-unlock.d` to `/usr/lib/x86_64-linux-gnu/ModemManager/` with following commands
  
    - cp -raf  `/opt/pwl/pwl-unlock/fcc-unlock.d`  `/usr/lib/x86_64-linux-gnu/ModemManager/`
  
    - rm -rf `/opt/pwl/pwl-unlock/fcc-unlock.d`
  
    - chown -R root:root  `/usr/lib/x86_64-linux-gnu/ModemManager/fcc-unlock.d`
  
    - chmod 755 -R `/usr/lib/x86_64-linux-gnu/ModemManager/fcc-unlock.d`
* For platform with SELinux mode enforcing, please manually install modemmanager_fccunlock.cil module
  
    - semodule -i `deb_extra/modemmanager_fccunlock.cil`

# Building on Ubuntu

## 1. Install
- sudo apt update
- sudo apt install cmake
- sudo apt install build-essential
- sudo apt install pkg-config
- sudo apt install libglib2.0-dev
- sudo apt install libmbim-glib-dev
- sudo apt install libxml2-dev
- sudo apt install openssl

## 2. Build
To compile and build services:

    cmake -S . -B build
    cmake --build build
    
To install services

    sudo cmake --install build

## 3. Manage System services in Ubuntu
- reload config file

    sudo systemctl daemon-reload
- enable system service

    sudo systemctl enable pwl-xxx.service
- start system service

    sudo systemctl start pwl-xxx.service
- stop system service

    sudo systemctl stop pwl-xxx.service
- check the system service status

    sudo systemctl status pwl-xxx.service

# Release History
- version: 1.0.0
  initial version.

- version: 2.0.0
    - Modem AT control over AT port
    - Message box popup showing flash progress during module firmware update
    - GPIO control reset feature implement
    - HW/SW reset for error handling with module firmware update

- version: 2.0.1
    - Build failure & memory leak fix

- version: 3.0.0
    - Support firmware downgrade
    - Enable modem auto switch when sim carrier change
    - After firmware upgrade, wait for modem config to complete before perform reboot
    - Control flow after GPIO HW reset
    - Enhance error case handling
    - pwl_unlock completely independent from other services

- version: 3.1.0
    - SIM carrier switch feature

- version: 3.1.1
    - config FORTIFY_SOURCE=3 for CMake build

- version: 3.2.0
    - Modem AT control over MBIM

- version: 4.0.0
    - pcie device support
    - pcie device FCC unlock & Module recovery

- version: 4.2.0
    - Firmware update with fastboot support for pcie device
    - pcie device carrier firmware update switch when sim switch
    - Enable '--test-quick-suspend-resume feature' flag in ModemManager
    - Temporary modify wwan module pcie device's autosuspend_delay_ms to 5000

- version: 4.2.1
    - Override Test options in ModemManager
    - Fix firmware update memory leak

- version: 4.2.2
    - Device list update

- version: 4.3.0
    - Modem CXP reboot feature

- version: 4.3.1
    - Modem CXP list update

- version: 4.3.2
    - Enhance GPIO reset and update reset control table

- version: 4.3.3
    - Implement modem recovery state check
    - Firmware update flow tuning

- version: 4.3.4
    - Fix eSIM enabled issue

- version: 4.3.5
    - Delay PWL Madpt service start till recovery check completed

- version: 4.4.0
    - Update preferred carrier list

- version: 4.4.1
    - config of power/control to 'auto' for usb device

- version: 4.4.2
    - Update SELinux policy definitions for pcie device

- version: 4.4.3
    - Override Test options in ModemManager for usb device

- version: 4.5.0
    - pcie device Modem AT control over MBIM

- version: 4.6.0
    - Add eSIM test profiles erase mechanism
    - Add one additional supported PCIe SSID.

- version: 4.6.1
    - Add more support list

- version: 4.7.0
    - Implement EFS recovery mechanism
    - Check SSID & FW belong to the same category

- version: 4.7.1
    - Add one additional supported PCIe SSID.

- version: 4.7.2
    - Add one additional supported USB SSID.

- version: 4.7.4
    - Fix fcc unlock issue.
    - Add USB VID/PID 413c:81ea

- version: 4.7.5
    - Fix missing "test low power mode" parameter issue.

- version: 4.7.6
    - Add one additional supported USB SSID.
    - Fix PCIoT and Non-PCIoT cross download issue.

- version: 4.7.7
    - Add one additional supported PCIe SSID.
    - Sync Windows FW update mechanism.

- version: 4.7.8
    - Add one additional supported PCIe SSID.

- version: 4.7.9
    - Add one additional supported PCIe SSID.
    - Add one additional supported USB SSID.

- version: 4.7.10
    - Add ten additional supported USB SSIDs (ZXL2 IOT table 2026-09-16).
    - Bound every send between the daemons to 5 s so a full queue no longer
      hangs madpt, pref and fwupdate in a ring.
    - Check for a firmware update only after pref has refreshed the versions
      and carrier, instead of a fixed 10 s wait; retry the check when the
      module did not answer instead of recording "no update needed".
    - Fix the PCIe flash retry counter (never left 0) and bound the retry
      loop, so a failed flash no longer retries forever with the progress
      dialog stuck on screen.
    - A module firmware version that cannot be read is "unknown", not
      "non-IOT": FCC unlock and the USB update check retry instead of
      aborting with "SKU & FW not match".
    - FCC unlock: abort cleanly when the MBIM device does not open instead of
      attempting the challenge anyway; leave the verdict in
      /run/pwl/fcc-unlock.status.
    - pwl_core: when the unlock hook failed for a transient reason and
      ModemManager parked the modem (disabled, low power), ask ModemManager to
      enable again once the module is reachable -- ModemManager itself never
      retries, which left the radio off after every firmware flash.
    - After a USB flash, once the module has re-enumerated, keep ModemManager
      away from it until it answers AT again, run the FCC unlock, then start
      ModemManager: the stale mbim-proxy and ModemManager's 5 s unlock budget
      against a module on its first boot are what left the radio off after
      every flash.
    - FCC unlock: log the module's answer when the challenge fails instead of
      exiting silently.
    - pwl-mm-options.sh execs ModemManager, so that when ModemManager aborts
      (seen with 1.24.2 after a module reset during the unlock hook) systemd's
      Restart=on-abort brings it back instead of leaving WWAN down.
    - The post-flash unlock run by fwupdate gets a 20 s MBIM open wait
      (PWL_UNLOCK_OPEN_WAIT) instead of the hook's 4 s.
    - FCC unlock on the PCIe module (ZM16) no longer compares the module's
      IOT category with the SSID: that module has one firmware line, and
      telling "unknown" from "non-IOT" had made the check abort there.
    - madpt: after a module reset, wait up to 3 s for the MBIM re-open
      instead of failing every queued command into another re-open, which
      left the preferred carrier unset after a firmware update.
    - FCC unlock: end the challenge at the final result code; a challenge
      containing "OK" (about 8 %) was cut short and the unlock failed.
    - Package: depend on libmbim-glib4 >= 1.32, and conflict with / replace
      Canonical's split pwl-* packages, which install the same /opt/pwl.
