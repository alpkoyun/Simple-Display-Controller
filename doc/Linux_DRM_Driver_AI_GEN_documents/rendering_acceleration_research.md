# Rendering Acceleration Research

Research date: 2026-06-05
Updated validation checkpoint: 2026-06-10

This note answers whether the FPGA display controller can gain a rendering
operation that Linux will automatically use, and what would be required to make
that happen.

## Short Answer

The project is possible, but "add one render operation and Linux will
automatically pick it" is not how the Linux graphics stack usually works.

The current driver is already automatically usable as a display output because
it exposes a normal DRM/KMS scanout device. That is a display-controller
contract: userspace gives the driver finished pixels, and the driver moves
those pixels to the FPGA for HDMI output.

For automatic rendering acceleration there are two realistic paths:

| Goal | Is it automatic? | Scope |
|---|---|---|
| Let the compositor use FPGA display hardware for overlays, cursors, scaling, or blending | Sometimes, through standard KMS planes and atomic checks | Medium. Much smaller than a GPU, but still more than one feature |
| Let OpenGL/EGL/Vulkan apps render on the FPGA | Only after a render node plus Mesa/userspace driver exists | Large. This is effectively a GPU driver stack, even for a small feature set |

A private kernel ioctl for "fill rectangle", "copy rectangle", or "blend image"
can be useful for tests and custom applications, but the desktop will not use it
automatically unless a standard userspace component, normally the compositor or
Mesa, knows how to call it.

## Current Project State

The repository describes the hardware as a display scanout path, not a
general-purpose GPU. The README says Linux produces finished pixels, the driver
transports them to the FPGA, and the FPGA outputs the video signal.

The current kernel driver matches that model:

- `fpga_drm.ko` exposes a KMS device, not a render GPU.
- The DRM driver feature flags are `DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC`.
  There is no `DRIVER_RENDER`.
- The mode config uses standard framebuffer creation and atomic commit helpers.
- The driver uses explicit KMS objects for one CRTC, one primary plane, one
  virtual encoder, and one virtual connector.
- An experimental `enable_overlay=1` path exposes one extra standard KMS overlay
  plane and CPU-composites it into the existing XDMA upload staging path.
- The only advertised scanout format is `DRM_FORMAT_XRGB8888`.
- The only advertised modifier is `DRM_FORMAT_MOD_LINEAR`.
- The driver uses GEM SHMEM helpers for host-visible framebuffer objects.
- There are no private DRM ioctls in `fpga_drm_drv.c`.
- On KMS enable/update, the driver copies the active primary framebuffer, and
  optionally the overlay rectangle, into host line buffers and submits the
  composed frame to XDMA as per-line H2C packets.

Validated behavior on 2026-06-10:

- The repo-built and installed `fpga_drm.ko` matched with srcversion
  `E1CC69967649F5D3185976E`.
- The driver loaded with
  `debug_logging=1 enable_overlay=1 composition_backend=cpu connector_connected=1 connector_non_desktop=0 enable_fbdev=1`.
- GDM/Xorg picked up `/dev/dri/card0` automatically after the display manager
  was started, and the desktop was visible through the FPGA HDMI output.
- `drm_info /dev/dri/card0` showed an active CRTC, active primary plane, and
  inactive overlay plane. The overlay plane existed, but had `FB_ID=0`.
- Kernel logs showed repeated XDMA frame uploads with `overlay=0`, which means
  the desktop was using the driver as a KMS scanout device but was not using the
  CPU overlay composition path.
- A direct `modetest` SMPTE upload produced visible HDMI output and active XDMA
  stream traffic.
- Because the video-output ILA was removed from the current hardware, the
  validation script was updated to use `PCIe_i/xdma_ila/inst/ila_lib`. A
  trigger on `net_slot_0_axis_tvalid` captured 797 valid/ready handshakes in
  1024 samples, with nonzero `net_slot_0_axis_tdata`.

Relevant local source:

- `README.md`
- `Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c`
- `doc/Linux_DRM_Driver_AI_GEN_documents/userspace_api.md`
- `doc/Linux_DRM_Driver_AI_GEN_documents/architecture.md`
- `doc/Linux_DRM_Driver_AI_GEN_documents/data_flow.md`

The existing HLS blocks are also display-pipeline blocks:

- `pixel_unpack` converts stream packing/layout before the video path.
- `color_convert` applies a per-pixel color matrix and bias.
- `pixel_pack` packs 24-bit stream data back into wider words.

These blocks are useful scanout/video-path components, but they are not a
userspace-visible render command processor.

## Why Display Output Is Already Picked

Linux desktops discover displays through DRM/KMS. Your driver creates
`/dev/dri/cardN`, exposes a connector, advertises modes, accepts a framebuffer,
and commits atomic KMS state. That is enough for a compositor or direct KMS test
tool to treat the FPGA as an output.

This is separate from rendering. A compositor can render with the CPU, with the
host GPU, or with another render device, then present the final image through
your KMS output. In that setup your FPGA is the display controller, not the
renderer.

This separation is normal. Many systems have one block for rendering and a
different block for display scanout.

## What "Automatic Render Use" Means

There are three different meanings that are easy to mix together.

### 1. KMS Plane Acceleration

This is the most realistic near-term target for this project.

In DRM/KMS, a plane is an image source that can be placed, cropped, scaled,
rotated, alpha-blended, or assigned a z-order during scanout if the driver and
hardware expose those features. Compositors already know how to test KMS plane
states. They may use overlay planes or cursor planes automatically when the
atomic test commit says a configuration is valid.

This can accelerate useful desktop work without implementing a full GPU:

- hardware cursor
- second overlay plane
- simple alpha blending
- color-key or global-alpha composition
- scaling one image to the CRTC
- direct scanout of a fullscreen client buffer
- video overlay, if buffer formats and memory import are supported

This is not OpenGL rendering. It is display-composition acceleration. It is the
best fit if the hardware operation is "put this already-rendered image on the
screen in this rectangle".

Minimum pieces:

| Piece | Why it is needed |
|---|---|
| Hardware plane/compositor block | The FPGA must blend or select multiple image sources during scanout |
| Extra DRM plane objects | Userspace must see the operation through standard KMS, not a private API |
| Atomic check/commit support | Compositors probe plane feasibility with test-only atomic commits |
| Format and modifier declaration | Userspace needs to know which buffers the plane can consume |
| Buffer import or upload path | The plane needs access to the image data |
| Fence handling | KMS must not scan out a buffer before rendering/upload into it is complete |

Current driver state: the KMS object model has moved beyond
`drm_simple_display_pipe`, and the optional overlay plane can prove compositor
discovery and atomic-test behavior before FPGA composition hardware exists. The
overlay is still CPU-composited; it is not yet hardware acceleration.

The next immediate test should not depend on GNOME or Xorg choosing the overlay
plane. Write a direct KMS overlay test that creates a full-screen primary
framebuffer plus a smaller overlay framebuffer, commits both planes atomically,
and checks for `overlay=1` plus a nonzero `cpu_compositions` counter in the
driver logs. This isolates driver correctness from compositor policy.

### 2. Private 2D Engine

This is useful for development, but not automatically used by Linux desktops.

Example operations:

- clear/fill rectangle
- copy rectangle
- image format conversion
- alpha blend source over destination
- scale source to destination

You could expose these with custom ioctls or a temporary debug/test interface.
That would let a custom test program submit commands and validate the FPGA
engine. It would not make Mutter, KWin, Weston, Xorg, SDL, Qt, GTK, OpenGL, or
Vulkan automatically use the operation.

This path is still valuable as a prototype. It proves:

- the hardware command format
- buffer address handling
- interrupt/completion behavior
- bounds checking
- synchronization
- performance

But after the prototype, automatic use still requires integration through KMS
planes, Mesa, a compositor backend, or an application library.

### 3. DRM Render Node Plus Mesa Driver

This is the path for a real OS-visible renderer.

DRM render nodes exist so unprivileged userspace can submit non-modesetting GPU
work through `/dev/dri/renderD*`. A driver must advertise `DRIVER_RENDER`, and
driver-specific render ioctls must be safe for render-node use. Render nodes do
not allow privileged modesetting operations.

Adding `DRIVER_RENDER` alone is not useful. A render node needs a complete
contract around memory, command submission, synchronization, and userspace.

Minimum kernel-side pieces:

| Piece | Why it is needed |
|---|---|
| Stable UAPI header | Userspace needs documented ioctl structs and command semantics |
| GEM buffer allocation/import/export | Render operations need source/destination buffers |
| Memory placement model | The driver must know whether buffers live in host RAM, FPGA DDR, or both |
| DMA address construction | The FPGA must be given safe device-accessible addresses |
| Command submission ioctl | Userspace needs to submit render/blit jobs |
| Command validation | The kernel must reject out-of-bounds and unsafe commands |
| Completion interrupt or polling | Userspace needs to know when work is done |
| `dma_fence` and/or `syncobj` integration | Other DRM devices and compositors need synchronization |
| Scheduler or job queue | Multiple clients need ordering, timeout, and recovery behavior |
| Reset/wedge policy | A hung engine must not hang the display stack or the kernel |
| Open userspace implementation | Mesa/libdrm/tests need to exercise the UAPI |

Minimum userspace pieces:

| Piece | Why it is needed |
|---|---|
| libdrm/test tooling | Basic command submission and buffer tests |
| Mesa Gallium driver, if targeting OpenGL/OpenGL ES | Mesa is the normal userspace API driver layer for GL/GLES |
| Vulkan driver, if targeting Vulkan | Vulkan has a separate driver model and much larger requirements |
| Compositor/app selection support | The system must decide to use this render device |

For OpenGL/GLES, Mesa Gallium is not just "call one blit ioctl". Gallium exposes
a broad driver interface: screen capabilities, resources, contexts, drawing,
blitting, transfers, fences, queries, and format support. If the FPGA only has a
single simple blit/fill operation, a full Mesa GL driver is probably not worth
the cost unless the goal is research into driver stacks rather than practical
desktop acceleration.

## The Important Design Boundary

`DRIVER_GEM` does not mean "GPU rendering supported". It means the DRM driver
has GEM buffer-object support.

Current `fpga_drm` GEM buffers are used as display framebuffers. The hardware
does not render into those buffers. The CPU copies from the GEM SHMEM
framebuffer into driver-owned line buffers, and XDMA streams the result to the
FPGA.

To render on the FPGA, the FPGA must be able to write the destination image
somewhere, and Linux must be able to treat that destination as a synchronized
graphics buffer.

That requires answering these hardware questions first:

| Question | Why it matters |
|---|---|
| Can the FPGA read source buffers directly from host memory over PCIe? | Needed for blit/texture-like operations without an extra upload step |
| Can the FPGA write destination buffers directly to host memory? | Needed if rendered output should be visible to CPU or another GPU without readback |
| Or does rendering happen only in FPGA DDR? | Then the driver needs a memory manager and import/export/copy paths |
| Can the engine address scatter-gather buffers? | GEM SHMEM buffers are not necessarily physically contiguous |
| Is there an IOMMU in the path? | The driver may need DMA mappings rather than physical addresses |
| Is completion interrupt-driven? | Fences and nonblocking userspace depend on reliable completion |
| What happens on a bad command or timeout? | A render engine needs reset or wedge handling |

If the hardware can only receive a stream of final scanout pixels, it cannot be
an OS-visible renderer without new hardware blocks.

## What Will Not Work

These changes are not sufficient by themselves:

- Adding `DRIVER_RENDER` to `driver_features` without render-safe ioctls and
  synchronization.
- Adding a custom "render rectangle" ioctl and expecting the desktop to call it.
- Relying on dumb buffers for acceleration. Kernel DRM documentation explicitly
  treats dumb buffers as scanout buffers, not GPU acceleration buffers.
- Expecting Mesa to select the FPGA without a matching Mesa driver.
- Exposing `/dev/accel/accel*` and expecting graphics software to use it as a
  GPU. The accelerator subsystem intentionally separates compute accelerators
  from graphics GPUs.
- Assuming the existing `color_convert` HLS block counts as a renderer. It is
  currently a scanout-stream transform.

## Software-First Rendering Workflow

The driver-contained software renderer workflow is applicable. In this model,
the kernel driver exposes a small render engine contract, but the first backend
executes that contract on the CPU. Later, the CPU executor is replaced one
operation at a time with FPGA hardware.

This is different from using Mesa LLVMpipe or a userspace CPU renderer. The
goal is not "draw pixels on the CPU and present them through KMS"; your current
display path already proves that class of flow. The goal is to build the driver
architecture as if a render engine exists, while initially implementing the
engine with bounded CPU code.

The important rule is to keep the render contract stable while changing only
the backend underneath it. If userspace submits the same command to a CPU
backend today and an FPGA backend later, then the CPU prototype validates the
driver architecture. If the CPU prototype has a completely different API from
the future hardware path, it only validates the pixel algorithm.

### What CPU-First Can Prove

| It can validate | Notes |
|---|---|
| Pixel math | Fill, blit, blend, color conversion, scaling, clipping, and rounding can be tested with golden images |
| Driver UAPI | Command structs, handles, flags, rectangles, formats, and error returns can be tested before hardware exists |
| GEM ownership | Source/destination GEM lookup, mapping, lifetime, mmap, and framebuffer import can be exercised |
| Job validation | Bounds checks, format checks, pitch checks, overlap rules, and unsupported-operation paths can be stabilized |
| Scheduling shape | Blocking submit, workqueue execution, future fence signaling, and timeout policy can be developed early |
| KMS handoff | The rendered destination buffer can still be displayed through the existing KMS/XDMA output path |
| Test infrastructure | The same command vectors can later be run against the FPGA backend |

### What CPU-First Cannot Prove

| It cannot validate | Why |
|---|---|
| FPGA DDR behavior | CPU memory does not model FPGA DDR placement, bandwidth, or addressing limits |
| PCIe DMA behavior | CPU writes do not validate source/destination DMA descriptors or device-side scatter-gather handling |
| Hardware interrupts | CPU completion does not prove interrupt, timeout, or reset behavior |
| Real synchronization latency | CPU completion may hide fence and scheduling problems that hardware will expose |
| Hardware precision differences | Scaling/blending in hardware may round differently from the CPU reference |
| Automatic GPU selection | A CPU executor is still not a Mesa/OpenGL render driver unless Mesa integration exists |

It also should not become a full general-purpose software GPU inside the
kernel. Kernel-side CPU execution should mimic a small fixed hardware command
set, such as fill, blit, blend, or scale. Arbitrary shaders, OpenGL state
tracking, and complex rasterization belong in userspace drivers such as Mesa,
not in this kernel module.

### Recommended Backend Model

Use a render-backend split from the start:

```text
render UAPI or KMS plane state
    -> command validation
        -> render job
            -> backend executor: cpu or fpga
                -> destination GEM buffer
                    -> existing KMS/XDMA display output
```

The destination buffer is the connection between rendering and display. The
software renderer should produce a GEM buffer or update an existing GEM buffer.
The existing KMS path can then scan out/upload that buffer to the FPGA.

For the current driver, a clean internal split would look like this:

| Component | Responsibility |
|---|---|
| `fpga_drm_render.h` | Internal render job structs and backend ops |
| `fpga_drm_render.c` | ioctl entry, command validation, job creation, workqueue scheduling |
| `fpga_drm_render_cpu.c` | CPU implementation of fill, blit, blend, scale |
| future `fpga_drm_render_hw.c` | FPGA register/DMA programming for the same jobs |
| `fpga_drm_drv.c` | Existing KMS display, XDMA upload, mode setting |

The backend ops should be narrow:

```text
validate_job(job)
prepare_job(job)
run_job_cpu(job) or run_job_fpga(job)
finish_job(job, status)
```

The render command contract should be the thing you try to keep stable. The
backend can change.

For example:

```text
struct fpga_2d_fill {
    dst_buffer
    dst_x, dst_y, width, height
    xrgb8888_color
}

struct fpga_2d_blit {
    src_buffer
    dst_buffer
    src_x, src_y
    dst_x, dst_y
    width, height
}

struct fpga_2d_blend {
    src_buffer
    dst_buffer
    src_x, src_y
    dst_x, dst_y
    width, height
    global_alpha
}
```

Start with strict constraints that match the existing display path and likely
first hardware:

- `DRM_FORMAT_XRGB8888` only.
- Linear buffers only.
- Rectangles clipped to buffer bounds.
- One destination buffer per operation.
- Optional source buffer depending on operation.
- No arbitrary userspace pointers in kernel interfaces.
- No unbounded command streams.
- Fixed-point or integer math for anything that may move into the kernel or
  FPGA.
- Explicit rounding rules for blend and scale operations.
- Clear error codes for unsupported sizes, formats, alignments, and overlaps.

### Phase A: Define A Hardware-Like Render Contract

Start by defining the small render engine that the FPGA will eventually
implement. Do not start with "support rendering" as a broad goal.

Good first command set:

- `FILL`: write a solid XRGB8888 color into a rectangle.
- `BLIT`: copy a rectangle from source to destination.
- `BLEND`: source-over blend a rectangle with fixed global alpha.
- Optional later `SCALE`: nearest-neighbor or fixed bilinear scaling.

For each operation, specify:

- source and destination buffer handles
- source and destination rectangles
- supported format and pitch rules
- clipping behavior
- overlap behavior
- alpha and rounding behavior
- whether the ioctl blocks or completes asynchronously
- exact error returns for invalid inputs

Treat this as a draft hardware spec. The CPU backend is then an executable
model of that spec.

### Phase B: Add A Driver CPU Backend

Add a CPU executor inside the driver before any FPGA render block exists.

Suggested first implementation:

1. Add an experimental private render ioctl on `/dev/dri/cardN`.
2. Copy one small command struct from userspace.
3. Look up source/destination GEM objects by handle.
4. Validate format, pitch, bounds, operation, and flags.
5. Map the GEM objects into kernel virtual address space.
6. Execute the operation on the CPU.
7. Unmap/release the GEM objects.
8. Return completion status.

The first version can be blocking. That is acceptable for validating command
shape. The second version should move execution to a workqueue and return a
fence or sync object if you want to mimic real hardware behavior more closely.

Keep it obviously experimental:

- gate it behind a module parameter such as `software_render=1`
- keep command structs in a local experimental header until the design is stable
- execute long operations in a workqueue, not while holding modeset locks
- avoid floating point in kernel code
- do not accept raw userspace pointers as image memory
- use GEM buffers or driver-owned staging buffers
- hard-limit command count, image size, and operation runtime
- return `-EINVAL`, `-EOPNOTSUPP`, `-ENOENT`, or `-ETIMEDOUT` consistently

This is the phase that validates the driver, not just the display path.

For the current driver, the natural connection point is not replacing KMS
modeset. It is adding a render path beside KMS:

```text
test app
    -> render ioctl
        -> CPU backend modifies GEM destination buffer
            -> KMS commit displays that destination buffer
                -> existing fpga_drm_copy_frame()
                    -> existing XDMA upload
```

### Phase C: Shared Operation Test Suite

Create test vectors before adding hardware.

Recommended tests:

- full-buffer solid fill
- clipped fill
- same-size blit
- overlapping blit, either explicitly rejected or precisely specified
- source-over blend with alpha `0`, `128`, and `255`
- out-of-bounds rectangles
- unsupported format rejection
- odd pitch and stride cases
- mode-size cases for every advertised display mode

The expected output can be produced by a userspace reference implementation,
but the driver CPU backend is the first target under test. Later, the FPGA
backend must pass the same vectors.

The test program should do both:

1. Submit the command to the render ioctl.
2. Display the destination GEM buffer through normal KMS.

This separates correctness from presentation. If the rendered memory is wrong,
the render backend failed. If the memory is correct but the monitor is wrong,
the existing display/upload path is the suspect.

### Phase D: Add Hardware-Like Scheduling

Once the blocking CPU backend works, make the driver look more like a hardware
engine.

Useful upgrades:

- ordered job queue
- per-file or per-context state, if needed
- async submit
- completion fence
- timeout path
- job cancellation on file close
- debug counters for submitted/completed/failed jobs
- optional syncobj integration

At this point the CPU backend is doing what the future hardware backend should
do from the kernel's point of view: accept a job, execute it asynchronously,
signal completion, and leave the result in a buffer.

### Phase E: Swap CPU Executor For FPGA Backend

After the driver contract works, move one operation at a time to hardware.

Use a backend selector:

```text
render_backend=cpu
render_backend=fpga
```

The test flow should be:

1. Run the operation through the driver CPU backend.
2. Run the same operation through the FPGA backend.
3. Compare destination buffers or CRCs.
4. Display the destination buffer through normal KMS.

Only one backend operation should change at a time. Move `FILL` first, then
`BLIT`, then `BLEND`, then `SCALE`. Keep unsupported operations falling back to
CPU only if that fallback is explicitly part of the design and visible in debug
state.

The hardware backend should consume the same validated job object that the CPU
backend consumes. The difference should be inside the backend executor:

| CPU backend | FPGA backend |
|---|---|
| map GEM buffers to CPU vaddr | map/pin buffers for DMA or stage into FPGA DDR |
| run C loops over pixels | program FPGA registers/descriptors |
| complete from workqueue | complete from interrupt or polling |
| signal software completion | signal fence after hardware completion |

### Phase F: Decide Whether To Expose It Automatically

The private render ioctl path validates the renderer, but ordinary desktop
software will not automatically call private ioctls.

For automatic use, choose an integration layer after the backend works:

| Integration | Meaning |
|---|---|
| KMS cursor/overlay/scaler plane | Compositors may use it automatically through atomic KMS |
| Custom userspace library | Your applications can use it directly |
| DRM render node | Unprivileged render clients can submit jobs through `/dev/dri/renderD*` |
| Mesa driver | GL/GLES userspace can target it, but this is a much larger project |

If the operation is really display composition, such as cursor, overlay, or
scaling a plane, expose it through KMS planes. A CPU backend can emulate the
future plane hardware first, similar in spirit to `vkms`, while your driver
still sends the final frame to the real FPGA output.

If the operation is a separate 2D render engine, such as fill/blit/blend into a
buffer, keep it as a render ioctl first. Add `DRIVER_RENDER` and render-node
support only when the ioctls are render-node safe and synchronization is mature.

### Practical Project Flow

Recommended order for this repository:

1. Define a tiny hardware-like command set: `FILL`, `BLIT`, then `BLEND`.
2. Add internal render job structs and a backend ops table.
3. Add a blocking experimental render ioctl on `/dev/dri/cardN`.
4. Implement the ioctl with a CPU backend inside the driver.
5. Write `fpga-render-test` to allocate GEM buffers, submit commands, compare
   results, and optionally display the destination buffer through KMS.
6. Add golden vectors and debug counters.
7. Convert blocking execution to queued jobs and completion signaling.
8. Implement the FPGA block for `FILL`.
9. Add an FPGA backend for the same validated `FILL` job.
10. Compare CPU and FPGA results using the same test vectors.
11. Repeat for `BLIT`, `BLEND`, and optional `SCALE`.
12. Decide later whether the mature operation should become a KMS plane, custom
    app API, render node, or Mesa-facing driver.

This gives you a clean migration path:

```text
driver render contract -> driver CPU backend -> tests -> driver FPGA backend
-> optional KMS-plane/render-node/Mesa integration
```

## Recommended Path For This Project

### Phase 1: Keep The Current Driver As The Display Controller

Do not start by trying to make OpenGL run on the FPGA. The current project is a
working KMS output path. Preserve that.

Useful improvements:

- keep KMS mode setting stable
- improve damage/partial update behavior if the hardware transport can benefit
- consider enabling a hardware cursor if the FPGA can overlay it cheaply
- consider a real connector type and EDID/DDC if the hardware path supports it
- keep `connector_non_desktop=0` for desktop use

This phase keeps the board useful as a display.

### Phase 2: Add One Hardware Operation As A Prototype

Build a small FPGA-side operation, but treat it as a prototype first.

Good first operations:

- solid fill into FPGA DDR
- copy rectangle inside FPGA DDR
- source-over blend from one FPGA DDR buffer into another
- simple scaler from one buffer to the scanout buffer

Expose it with a private test ioctl or a narrow test interface. Write a small
userspace test that:

1. Allocates buffers.
2. Uploads known source data.
3. Submits the command.
4. Waits for completion.
5. Displays or reads back the result.

This validates the engine before you commit to a public ABI.

### Phase 3: Choose The Integration Layer

After the operation works, choose one of these:

| Integration | Best when | Automatic desktop use |
|---|---|---|
| KMS overlay/cursor/scaler plane | Operation affects final display composition | Possible, compositor-dependent |
| Private app/library API | You control the application | No, but simple and practical |
| Mesa Gallium render driver | You want GL/GLES apps to render on FPGA | Possible, but large |
| DRM accel driver | You want non-graphics compute/AI acceleration | No, intentionally not a graphics path |

For this repository, KMS plane/composition support is the most realistic
"automatic" target. It matches the current display-controller architecture and
does not require building a shader-capable GPU.

### Phase 4: Only Then Consider A Render Node

A render node is justified if you want the FPGA to be treated as a graphics
render device, not only a display composer.

For a minimal 2D render node, the likely design would be:

- keep `/dev/dri/cardN` for KMS display output
- add `DRIVER_RENDER` after the render UAPI exists
- create private ioctls such as `FPGA_GEM_CREATE`, `FPGA_GEM_MMAP`,
  `FPGA_SUBMIT_BLIT`, and `FPGA_WAIT` or syncobj/fence equivalents
- implement DMA-safe buffer addressing
- implement interrupt-backed completion and timeout recovery
- expose dma-buf import/export if buffers must cross devices
- write libdrm-style tests
- write a Mesa winsys/driver only if GL/GLES integration is a real goal

This is no longer "one operation". It is a small render driver stack.

## Practical Recommendation

If the goal is "the Linux desktop automatically benefits from the FPGA", do not
build a full GPU first. Add KMS-visible display-composition features:

1. Hardware cursor.
2. One overlay plane.
3. Basic alpha or z-order.
4. Optional scaler.
5. Proper atomic check/commit and fence handling.

Compositors already understand those concepts. They may use them when the plane
constraints fit a window, cursor, fullscreen surface, or video surface.

From the 2026-06-10 checkpoint, the next concrete steps are:

1. Write a direct KMS overlay-plane test that commits primary plus overlay.
2. Confirm the driver logs `overlay=1` and increments `cpu_compositions`.
3. Add focused failure logging for overlay atomic-check rejects, including
   format, scaling, bounds, CRTC, and framebuffer reasons.
4. Add compositor-friendly standard plane properties, starting with immutable
   `rotation=0`, then global alpha and pixel blend mode if the CPU backend can
   implement the same semantics.
5. Test with Weston DRM backend before GNOME/KDE, because Weston makes KMS
   plane assignment easier to observe and reason about.
6. After CPU overlay is proven, choose the first FPGA-backed display operation:
   hardware cursor or fixed-format overlay blend are the smallest useful
   candidates.

If the goal is "an application can ask the FPGA to render something", build a
private 2D command engine and a small userspace library. That is much more
achievable than Mesa integration, but it is not automatic OS rendering.

If the goal is "OpenGL runs on the FPGA", then the scope becomes a real GPU
software stack. You need kernel render UAPI plus Mesa. A single custom hardware
operation is not enough for Linux to select it as a GL renderer.

## External References

- Linux DRM userland interfaces and render nodes:
  <https://docs.kernel.org/6.18/gpu/drm-uapi.html>
- Linux DRM/KMS atomic mode setting and plane abstraction:
  <https://docs.kernel.org/gpu/drm-kms.html>
- Linux accelerator subsystem introduction:
  <https://docs.kernel.org/accel/introduction.html>
- Mesa Gallium introduction:
  <https://docs.mesa3d.org/gallium/intro.html>
- Mesa Gallium screen capabilities:
  <https://docs.mesa3d.org/gallium/screen.html>
- Mesa Gallium context operations:
  <https://docs.mesa3d.org/gallium/context.html>
- Linux VKMS software KMS driver:
  <https://docs.kernel.org/gpu/vkms.html>
- Mesa LLVMpipe software rasterizer:
  <https://docs.mesa3d.org/drivers/llvmpipe.html>
