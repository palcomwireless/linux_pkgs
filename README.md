# Palcom Wireless Linux service packages
This repository contents below pwl linux service packages to be used with WWAN modules.

`Core service` is the core controller of pwl services.

`Module adapter service` is the module communication adapter service.

`Preference service` is the config preference for the packages.

`FW Update service` is the firmware update service.

`FCC unlock service` performs the FCC unlock procedure for the module. (not open source)

## License
This project is dual-licensed under the [Apache License 2.0](LICENSE-APACHE) and the [GNU General Public License v2.0](LICENSE).

# Notice
* The minimum Ubuntu version that can run this package is Ubuntu 22.04.
* FW Update service must be used with pwl-firmware package.  Before installing the service package, ensure that pwl-firmware has been installed. Obtain the pwl firmware from the corresponding OEM.
* FCC unlock service default not enable, if you want to enable it, please manually copy `/opt/pwl/pwl-unlock/fcc-unlock.d` to `/usr/lib/x86_64-linux-gnu/ModemManager/` with following commands
  
    - cp -raf  `/opt/pwl/pwl-unlock/fcc-unlock.d`  `/usr/lib/x86_64-linux-gnu/ModemManager/`
  
    - rm -rf `/opt/pwl/pwl-unlock/fcc-unlock.d`
  
    - chown -R root:root  `/usr/lib/x86_64-linux-gnu/ModemManager/fcc-unlock.d`
  
    - chmod 755 -R `/usr/lib/x86_64-linux-gnu/ModemManager/fcc-unlock.d`

# Building on Ubuntu

## 1. Install
- sudo apt update
- sudo apt install cmake
- sudo apt install build-essential
- sudo apt install pkg-config
- sudo apt install libglib2.0-dev
- sudo apt install libmbim-glib-dev

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
