# FPGA DRM Primary + iGPU HDMI Secondary Display

## Objective

Keep the FPGA output driven by `fpga_drm` as the primary desktop display, and clone the same desktop output onto a monitor connected to the Intel iGPU HDMI connector.

The desired arrangement is:

```text
fpga_drm / Virtual-1  -> primary display
i915 / HDMI-A-2       -> secondary display, cloned output
```

The iGPU monitor does not need to become the primary display, and the FPGA connector should not be marked `non_desktop`.

## Observed hardware and DRM state

The machine currently enumerates both display devices correctly:

| PCI function | Device | Driver | DRM card |
|---|---|---|---|
| `00:02.0` | Intel integrated graphics `8086:0412` | `i915` | `card1` |
| `01:00.0` | Xilinx FPGA `10ee:7024` | `fpga_drm` | `card0` |

Current connector state:

```text
/sys/class/drm/card0-Virtual-1       connected
/sys/class/drm/card1-HDMI-A-1       disconnected
/sys/class/drm/card1-HDMI-A-2       connected
/sys/class/drm/card1-VGA-1          disconnected
```

The iGPU HDMI connector has a valid EDID and advertised modes, but is not currently being scanned out:

```text
/sys/class/drm/card1-HDMI-A-2/status  = connected
/sys/class/drm/card1-HDMI-A-2/enabled = disabled
```

The FPGA connector is the active desktop output:

```text
/sys/class/drm/card0-Virtual-1/status  = connected
/sys/class/drm/card0-Virtual-1/enabled = enabled
```

The current FPGA module parameters include:

```text
connector_connected=Y
connector_non_desktop=N
enable_fbdev=Y
enable_overlay=Y
upload_enabled=Y
```

These settings are compatible with keeping FPGA as the primary display.

## Root cause

This is not a PCIe or `i915` detection failure. The iGPU is bound to `i915`, detects the physical monitor, and exposes its modes.

The current Xorg session opens both DRM cards but chooses the FPGA card as Screen 0:

```text
Adding drm device (/dev/dri/card0)
Adding drm device (/dev/dri/card1)
no primary bus or device found
falling back to .../01:00.0/drm/card0
modeset(0): using drv /dev/dri/card0
modeset(G0): using drv /dev/dri/card1
Output Virtual-1 using initial mode 1280x720
```

The iGPU is therefore treated as a secondary GPU screen (`G0`). Its HDMI connector is probed, but it is not attached to the primary Xorg display layout and remains disabled.

The current `xrandr --listproviders` result is:

```text
Provider 0: id: 0x40 cap: 0x0
           crtcs: 1 outputs: 1
           name:modesetting

Provider 1: id: 0x71 cap: 0x6, Sink Output, Source Offload
           crtcs: 3 outputs: 3
           name:modesetting
```

Provider 0 is the FPGA primary provider. It has no PRIME provider capability (`cap: 0x0`). Provider 1 is the iGPU and already exposes `Sink Output`, which is the role needed for the iGPU physical HDMI output to receive frames from the primary provider.

The current `fpga_drm` driver declares:

```c
.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
```

It does not currently advertise `DRIVER_PRIME` or provide the PRIME buffer-sharing path required for Xorg to connect the FPGA primary screen to the iGPU output provider.

## Why the virtual connector is not the fundamental problem

The FPGA connector is intentionally registered as:

```c
DRM_MODE_ENCODER_VIRTUAL
DRM_MODE_CONNECTOR_VIRTUAL
```

This describes the current driver architecture: the FPGA driver provides a KMS scanout endpoint with a curated mode list and does not currently perform physical HDMI EDID/hotplug detection through the HDMI transmitter.

Changing the connector type to `HDMI-A` would not make two independent DRM cards share a desktop. It could also misrepresent the current hardware interface.

The important missing capability is PRIME output sharing from the FPGA provider, not the connector name.

## Proposed solution

Add PRIME support to `fpga_drm` while preserving its current KMS behavior.

### Driver work

1. Add `DRIVER_PRIME` to the driver feature flags.
2. Wire the standard GEM PRIME handle-to-file-descriptor and file-descriptor-to-handle helpers.
3. Provide the required GEM PRIME export/import operations for the existing shmem-backed GEM objects.
4. Verify that imported dma-buf framebuffers can be mapped by the existing shadow-plane path:

   ```c
   DRM_GEM_SHADOW_PLANE_HELPER_FUNCS
   shadow_state->data[0]
   ```

5. Preserve the existing restrictions:

   - `DRM_FORMAT_XRGB8888`
   - linear modifier
   - full-screen primary plane
   - current whitelist modes
   - existing CPU composition and XDMA upload behavior

6. Confirm that the new driver advertises `Source Output` to Xorg/RandR for the FPGA provider.

The intended provider relationship is:

```text
FPGA provider 0x40: Source Output  (new capability)
iGPU provider 0x71: Sink Output    (already present)
```

### Userspace display setup

After the driver supports PRIME and is reloaded, re-check provider IDs because they are not stable across sessions:

```bash
xrandr --listproviders
```

With the current IDs, the expected provider connection is:

```bash
xrandr --setprovideroutputsource 0x71 0x40
```

The command means: use the FPGA provider as the image source for the iGPU output sink.

Then clone a common mode onto the iGPU HDMI output:

```bash
xrandr --output HDMI-1-2 --mode 1280x720 --same-as Virtual-1
```

`1920x1080` can be used instead if both displays are configured to the same supported 1080p mode. The FPGA and iGPU must use a compatible geometry for a direct clone.

After linking the providers, verify:

```bash
xrandr --query
cat /sys/class/drm/card0-Virtual-1/enabled
cat /sys/class/drm/card1-HDMI-A-2/enabled
```

Expected result:

```text
Virtual-1     connected and active
HDMI-1-2      connected and active
```

The desktop should remain visible through the FPGA output and appear identically on the iGPU HDMI monitor.

## Validation checklist for the implementation session

### Build and live-module identity

Build the repository driver and compare the repository module with the installed module before claiming live behavior:

```bash
make -C Linux_DRM_Driver/fpga_drm
modinfo Linux_DRM_Driver/fpga_drm/fpga_drm.ko | rg 'filename|vermagic|srcversion'
modinfo fpga_drm | rg 'filename|vermagic|srcversion'
```

Install/reload the rebuilt module using the established project helper. Do not load standalone `xdma` at the same time because both drivers own the same PCI function `0000:01:00.0`.

### DRM and provider checks

```bash
readlink /sys/bus/pci/devices/0000:01:00.0/driver
readlink /sys/bus/pci/devices/0000:00:02.0/driver
cat /sys/class/drm/card0-Virtual-1/status
cat /sys/class/drm/card1-HDMI-A-2/status
xrandr --listproviders
```

The FPGA must remain bound to `fpga_drm`, the iGPU must remain bound to `i915`, and provider 0 must gain `Source Output`.

### Clone setup

Use the actual provider IDs and output names reported by the current X session:

```bash
xrandr --setprovideroutputsource <igpu-sink-provider> <fpga-source-provider>
xrandr --output <igpu-hdmi-output> --mode 1280x720 --same-as <fpga-output>
```

Then verify both monitors visually and confirm both connectors are enabled.

### FPGA stream validation

The PRIME change must not break the existing FPGA stream. Confirm that:

- `0000:01:00.0` remains bound to `fpga_drm`.
- `Virtual-1` remains connected and active.
- Existing FPGA desktop output remains visible.
- XDMA upload traffic continues.
- The current XDMA ILA still captures `tvalid && tready` handshakes and nonzero `tdata` when the desktop changes.

## Non-goals and cautions

- Do not change the FPGA connector to `DRM_MODE_CONNECTOR_HDMIA` solely to solve this problem.
- Do not set `connector_non_desktop=1`; the FPGA must remain desktop-primary.
- Do not disable `enable_fbdev` unless a separate test specifically requires it.
- Do not load standalone `xdma.ko` together with `fpga_drm.ko` for the same endpoint.
- Do not assume `xrandr` provider IDs remain `0x40` and `0x71` after a reload or reboot.
- A successful `xrandr --listproviders` result alone is insufficient; verify the provider link, both active connectors, visible output, and continued FPGA XDMA traffic.

## Relevant source anchors

- Connector registration: `Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c:2074-2087`
- Current driver features: `Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c:2000-2009`
- Shadow-plane framebuffer mapping: `Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c:1281-1305`
- Primary shadow-plane helper functions: `Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c:1884-1903`
- Current Xorg evidence: `/home/alpk/.local/share/xorg/Xorg.1.log:55-160`
