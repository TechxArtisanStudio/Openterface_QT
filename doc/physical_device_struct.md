# Physical Device Structures

## Mini-KVM
The Mini-KVM device integrates a serial port and a composite device under the same universal USB hub in the USB tree. This setup allows for streamlined communication and multimedia handling.

- **Universal USB Hub** (Root hub for the device)
  - **Serial Port** (VID:PID 1A86:7523, Chip: CH9329)
  - **Composite Device** (VID:PID 534D:2109)
    - **Camera**
    - **Audio** 
    - **HID (Human Interface Device)** 

## KVM-GO (USB2.0 Hub)
The KVM-GO with USB2.0 hub places the serial port and composite device under the same universal USB hub, similar to Mini-KVM, ensuring compatibility and efficient data flow.

- **Universal USB Hub** (Root hub for the device)
  - **Serial Port** (VID:PID 1A86:FE0C, Chip: CH32V208)
  - **usb storge** 
  - **Composite Device** (VID:PID 345F:2132)
    - **Camera**
    - **Audio** 
    - **HID** 

## KVM-GO (USB3.0 Hub)
In the USB3.0 hub configuration, the KVM-GO separates the serial port and composite device under different universal USB hubs within the tree, which may improve bandwidth allocation for high-speed peripherals.

- **Universal USB Hub** (Root hub for the device)
  - **Universal USB Hub** (Sub-hub for serial port isolation)
    - **Serial Port** (VID:PID 1A86:FE0C, Chip: CH32V208)
    - **usb storge** 
  - **Composite Device** (VID:PID 345F:2132)
    - **Camera**
    - **Audio**
    - **HID**