# ch341 linux serial driver
## Description

USB serial driver for USB to UART chip ch340, ch341, etc. ch341 supports multiple working modes, this driver only supports its serial port mode.

In fact the ch341 serial driver has been built-in since the Linux mainline kernel version 2.6.24. The location is: drivers/usb/serial/ch341.c, it's a pity that the built-in driver cannot be kept up to date and cannot support all features of the chip. We suggest our customers to use this driver.

1. Open "Terminal"
2. Switch to "driver" directory
3. Compile the driver using "make", you will see the module "ch341.ko" if successful
4. Type "sudo make load" or "sudo insmod ch341.ko" to load the driver dynamically
5. Type "sudo make unload" or "sudo rmmod ch341.ko" to unload the driver
6. Type "sudo make install" to make the driver work permanently
7. Type "sudo make uninstall" to remove the driver
8. You can refer to the link below to acquire uart application, you can use gcc or Cross-compile with cross-gcc
   https://github.com/WCHSoftGroup/tty_uart

Before the driver works, you should make sure that the usb device has been plugged in and is working properly, you can use shell command "lsusb" or "dmesg" to confirm that, USB VID of these devices are [1a86], you can view all IDs from the id table which defined in "ch341.c".

If the device works well, the driver will created tty devices named "ttyCH341USBx" in /dev directory.

## Note

Any question, you can send feedback to mail: tech@wch.cn
