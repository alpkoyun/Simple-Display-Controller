# Problem and Hardware Evolution

> Historical record: this document describes the superseded July 15 8 MiB
> design. For the current 64 MiB BAR and shared `0x3e000000` DDR contract, use
> [Shared 32 MiB DDR GOP Prerequisite](../shared_32m_ddr_gop_prerequisite/README.md).

## Why this work was required

The project already produced Linux display output through this path:

```text
Linux framebuffer
    -> XDMA H2C descriptors and AXI stream
    -> VDMA S2MM
    -> DDR3 frame ring
    -> VDMA MM2S
    -> video pipeline
    -> HDMI
```

That proves the PCI endpoint, XDMA H2C engine, VDMA, DDR3, video pipeline, and
HDMI output can work together. It does **not** prove that an ordinary CPU PCI
memory write can reach DDR. GOP needs a directly addressable linear
framebuffer: firmware publishes a `FrameBufferBase`, then GOP clients write
pixels through normal CPU memory accesses without Linux XDMA descriptors.

The missing prerequisite was therefore a second host-to-framebuffer path:

```text
CPU store -> PCIe BAR -> XDMA M_AXI_BYPASS -> DDR3 <- VDMA MM2S
```

## Development sequence

| Step | Observation | Decision or result |
|---|---|---|
| Initial GOP research | Pre-OS UEFI graphics requires a GOP driver and a CPU-visible framebuffer. | Prove the BAR-backed DDR mechanism before writing firmware. |
| First DDR-bypass hardware update | The bypass master could reach DDR, but the enlarged BAR prevented the PC from booting reliably. | Reduce the host aperture rather than reducing VDMA's DDR capacity. |
| Reduced 32 MiB BAR with translation `0x3f000000` | Linux display output worked, but the direct DDR scratch test failed. | Treat the H2C display path and bypass path as independent; visible output alone was not bypass proof. |
| Restored bypass AXI ILA | Host offset `0x01fff000` emitted AXI `0x3ffff000`, not additive address `0x40fff000`, and every DDR attempt returned `DECERR`. | Model XDMA translation as bit composition and keep required offsets out of the aliased upper half. |
| Intended 8 MiB alias export | The bypass MIG segment moved to `0x3f800000-0x3fffffff`; VDMA retained its 1 GiB map at `0x40000000-0x7fffffff`. | Initially assumed that the two master-relative addresses selected one physical region. |
| Updated driver and initial tests | Static contract, scratch read/write/restore, one-frame programming, and bypass AXI ILA all passed, but the color bars were corrupted. | The tests proved completed transactions, not a shared framebuffer. Paired ILAs rejected the address contract. |
| July 16 replacement | Bypass, VDMA MM2S, and VDMA S2MM all use `0x3e000000-0x3fffffff`. | Correct direct-BAR color bars validate the shared physical frame. |

## The aliasing failure

The 32 MiB BAR exposes host offsets `0x00000000-0x01ffffff`, but the XDMA
bypass translation has bit 24 set:

```text
translation = 0x3f000000
old offset  = 0x01fff000
AXI output  = translation OR offset
            = 0x3ffff000
```

It does not produce the arithmetic sum `0x40fff000`. The restored ILA observed
the OR-composed `0x3ffffxxx` transactions directly. In the failing export that
range was not assigned to MIG, so the interconnect returned `DECERR`.

The final contract intentionally uses only host offsets
`0x00000000-0x00ffffff`. The BAR's upper half remains allocated but unused
because its address bit aliases a bit already set by the translation value.

## Why the proposed master-relative alias failed

AXI addresses are interpreted in the address space of the issuing master. The
8 MiB design intended these independent segments to resolve to the same MIG
offset:

```text
M_AXI_BYPASS 0x3f800000 -> MIG memaddr offset 0
M_AXI_MM2S   0x40000000 -> MIG memaddr offset 0
M_AXI_S2MM   0x40000000 -> MIG memaddr offset 0
```

Paired captures showed that this intended alias was not realized: bypass writes
were observed at MIG `0x3f800000`, while the VDMA mapping of `0x40000000`
selected MIG offset zero. The two paths therefore completed against different
physical bytes. The current design avoids this ambiguity by assigning all
three masters the same numeric DDR range, `0x3e000000-0x3fffffff`.
