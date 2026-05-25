# Simple-Display-Controller
This repo contains Simple Display Controller FPGA hardware, embedded software, and the Linux host driver. The controller aims to enable use of the AX7203 demo board as a display controller connected to a Linux host PC. Display data is sent to the card over PCIe, and the card outputs the frames over HDMI in DVI mode.

This is not a general-purpose GPU. It does not render graphics. It is a display scanout path. Linux produces finished pixels, the driver transports them to the FPGA, and the FPGA outputs them as a video
signal to the monitor.

# Hardware Architecture
AX7203 offers 4 PCIe 2.0 lanes used as the main data path between the Linux host PC and the FPGA. Each frame line is transported as one AXI-stream packet from the XDMA PCIe IP to the VDMA IP. The VDMA write channel saves lines into frame buffers in DDR3 SDRAM, while the VDMA read channel continuously feeds the video output subsystem. The VDMA write channel is configured as GENLOCK master and the read channel as GENLOCK slave so the read channel can switch frames as new frames arrive. The Linux DRM driver configures the video IPs through the XDMA AXI-Lite bypass BAR.
![Hardware Shematic](doc/visuals/outputs/Hardware_Schematic.drawio.svg)

In this Hardware Architecture, the XDMA IP acts as the master of the transfers. Host PC only configures the DMA descriptor tables on the XDMA and instructs to start the transactions but the transactions are done by the XDMA IP, not the HOST PC.



# Current Status of the Project
## Linux DRM Driver
The current `fpga_drm.ko` driver is a working fixed-mode DRM/KMS driver. It binds the Xilinx PCIe endpoint, exposes one virtual connector, advertises one `1280x720@60` mode, accepts `XRGB8888` framebuffers, and uploads complete frames through the XDMA H2C stream path. The driver also programs the FPGA video pipeline through the XDMA bypass BAR during probe, including pixel unpack, color conversion, VDMA, HDMI I2C, VTC, and video-lock readbacks.

The current bring-up has been validated with `drm_info`, `modetest -M fpga_drm`, and Vivado ILA capture. After a `modetest` SMPTE pattern upload, the video-stream ILA showed active `tvalid && tready` handshakes and nonzero 24-bit pixel data on the HDMI output stream.
![Demo Setup](doc/visuals/Demo.png)
## Hardware
Hardware is configured for 1280x720 frames at 60 FPS. Configuration of the IPs is done by `fpga_drm.ko` through the XDMA bypass BAR. The pixel format is 32-bit XRGB on the PCIe input side and 24-bit RGB on the HDMI output side.

# Next Step

## Hardware
Keep the exported `fpga_hardware/PCIe_wrapper/PCIe.hwh` address map synchronized with the Linux driver when the block design changes.
## Linux DRM Driver
Keep the documentation and validation scripts synchronized with the current bypass BAR map and rerun the DRM plus ILA validation flow after bitstream or driver changes.
