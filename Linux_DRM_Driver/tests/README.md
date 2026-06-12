# fpga_drm KMS Overlay Test

`kms_overlay_test` is a direct atomic KMS test for the experimental
`fpga_drm` overlay plane. It uses standard DRM ioctls only; no private driver
ABI or render node is involved.

Record each hardware test command and result in
`Linux_DRM_Driver/tests/TEST_LOG.md`. Include failed attempts when they explain
a code or workflow change.

For the mechanism and driver interaction details, see
`Linux_DRM_Driver/tests/doc/kms_overlay_test_design.md`.

Build:

```bash
make -C Linux_DRM_Driver/tests
```

Recommended validation setup:

```bash
sudo systemctl stop display-manager
sudo -n modprobe -r fpga_drm
sudo -n modprobe fpga_drm debug_logging=1 enable_overlay=1 composition_backend=cpu connector_connected=1 connector_non_desktop=0 enable_fbdev=1
sudo setfacl -m u:alpk:rw /dev/dri/card0
```

Run a test-only atomic commit first:

```bash
Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only
```

Then run a real commit and hold the image on screen:

```bash
Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --overlay 100,80,320,180 --hold 10
```

The current driver advertises framebuffer limits of at least `640x480`, so the
test may allocate a larger overlay backing framebuffer and display only the
requested source rectangle. That is expected and still exercises the overlay
plane with no scaling.

Expected success evidence:

```bash
sudo -n dmesg | grep -Ei 'fpga_drm|overlay|cpu_compositions|atomic'
```

The direct overlay milestone is proven when the atomic commit succeeds, the
monitor shows the full-screen primary pattern plus the smaller overlay pattern,
kernel logs show an upload with `overlay=1`, and the driver unload stats later
show `cpu_compositions > 0`.
