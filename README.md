# mirror of https://gitlab.com/visti_how/nrfdfu_ng



## nrfdfu-mp

A Linux utility for performing Bluetooth DFU firmware upgrades for Nordic Semiconductor nRF52 and probably nRF51 using a regular BT interface. 

### Tested: 

DFU Controller: 	Raspberry Pi 4B+ (with in-built BT Chip) (OS: Raspbian)   \
DFU Target:			Nordic nRF52832 Evalboard (PCA10040)

### License:
I have asked the original author on [https://devzone.nordicsemi.com/f/nordic-q-a/1620/linux-dfu-client-for-nrf51822-over-the-air-bootloader](https://devzone.nordicsemi.com/f/nordic-q-a/1620/linux-dfu-client-for-nrf51822-over-the-air-bootloader) about the original license.  
As of writing this I have not received an answer but it is probably something along "GPL-2 or later" as most files in bluez is released under this license and the original code is supposed to be based on bluez code.

### How To:
#### Dependencies

`bluez`: installed already, if not then follow [this tutorial](https://learn.adafruit.com/install-bluez-on-the-raspberry-pi/installation) to install manually

`sudo apt-get install autoconf libzip-dev libjson-c-dev`

#### Build
```
autoreconf --verbose --install --force
./configure
make
````
(find the new generated binary named **nrfdfu** after executing the make successfully)

#### Usage
`sudo ./nrfdfu -b bt_addr -p zip_pkg_file`  \
example:  `sudo ./nrfdfu -b c2:b6:80:22:85:8a -p ble_app_hrs_s140.zip`
