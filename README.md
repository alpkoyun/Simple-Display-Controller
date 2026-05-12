# Simple-Display-Controller
This repo contains Simple Display Contoller FPGA Hardware, Embedded Software and the Linux Host Driver. The controller aims to enable usage of AX7203 DEMO Board as a Display Controller.

The Display Controler will be used as a display output for the Linux Host PC. Display data will be sent over to the Card via PCIe. The card then outputs the frames via HDMI(DVI Mode).

# Hardware Architecture
AX7203 Offers 4x PCIe 2.0 lanes which is used as the main data path between the Linux Host PC and the FPGA. Each line of the frame is taken as a AXIS frame from the XDMA PCIe IP to the VDMA IP. VDMA Write Channel then saves the lines to the frame buffers residing on the DDR3 SDRAM. At the same time VDMA Read Channel keeps reading the frames to the Video Output Subsystem. VDMA Write Channel is configured as GENLOCK MASTER and Read Channel is configured as GENLOCK SLAVE such that read channel channel switches frames depending on the availabilatiy of new frames on the write channel.
The configuration of the IPs are done with a Microblaze Soft CPU. 
![Hardware Shematic](doc/visuals/outputs/Hardware_Schematic.drawio.svg)
# Current Status of the Project
## Linux DRM Driver
Basic driver with static resolution and frame rate is ready and working. Current Linux DRM Driver Exposes a PCIe device driver (bound to Xilinx PCIe IDs). No Mode setting is available yet, monitor is configured as always connected.
![Demo Setup](doc/visuals/Demo.png)
## Hardware
Hardware is configured to work on 1280*720 resolution frames at 60 FPS. Configuration of the IPs are done via Microblaze Soft CPU

# Next Step

## Hardware
The Microblaze will be removed from the project. The configuration of the IPs will be done with the SAXI-Lite Master port coming from the XDMA IP.
## Linux DRM Driver
The configuration of the IPs will be done inside the Linux DRM driver. 
