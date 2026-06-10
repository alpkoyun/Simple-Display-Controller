# Call Graphs

## Main Entry Points

| Entry point | Role |
|---|---|
| `fpga_drm_probe()` | PCI probe and DRM/XDMA initialization. |
| `fpga_drm_configure_static_pipeline()` | Host-side static FPGA video-IP setup through the XDMA bypass BAR. |
| `fpga_drm_program_mode()` | Programs the video clock wizard, VDMA, and VTC for a KMS mode. |
| `fpga_drm_remove()` | PCI remove and upload shutdown. |
| `fpga_drm_shutdown()` | System shutdown KMS cleanup. |
| `fpga_drm_crtc_atomic_enable()` | DRM CRTC enable callback; programs the selected whitelist mode. |
| `fpga_drm_crtc_atomic_flush()` | DRM CRTC flush callback; snapshots primary/overlay plane state and queues upload. |
| `fpga_drm_primary_atomic_check()` | Primary-plane atomic validation. |
| `fpga_drm_overlay_atomic_check()` | Optional overlay-plane atomic validation when `enable_overlay=1`. |
| `fpga_drm_upload_work()` | Workqueue function that submits the current frame. |
| `fpga_drm_xdma_done()` | XDMA async completion callback. |
| `fpga_drm_dma_complete_work()` | Workqueue completion finalizer. |

## Probe / Init Path

```mermaid
flowchart TD
    A[module_pci_driver fpga_drm_pci_driver] --> B[fpga_drm_probe]
    B --> C[devm_drm_dev_alloc]
    B --> D[init upload and DMA locks/work]
    B --> E[fpga_drm_alloc_frame_buffers]
    E --> F[sg_alloc_table frame_sgt with 1080 entries]
    E --> G[drmm_kmalloc max-width line_bufs 0..1079]
    B --> H[fpga_drm_open_xdma]
    H --> I[xdma_device_open]
    I --> J[enable PCI and map BARs]
    H --> U[select bypass BAR for AXI-Lite MMIO]
    I --> K[probe engines and setup IRQs]
    B --> Q[fpga_drm_configure_static_pipeline]
    Q --> R[program pixel unpack and color convert]
    Q --> T[program HDMI I2C and video-lock GPIO]
    B --> L[fpga_drm_modeset_init]
    L --> M[drm_universal_plane_init primary]
    L --> N[drm_crtc_init_with_planes]
    L --> V[drm_universal_plane_init overlay if enabled]
    L --> W[drm_encoder_init virtual encoder]
    L --> X[drm_connector_init virtual connector]
    B --> O[drm_dev_register]
    B --> P[drm_fbdev_generic_setup if enabled]
```

## DRM Userspace Path

```mermaid
flowchart TD
    App[Userspace opens /dev/dri/cardN] --> Core[DRM core/GEM fops]
    Core --> AddFB[addfb or dumb/GEM buffer ioctls]
    AddFB --> FBCreate[drm_gem_fb_create_with_dirty]
    Core --> Commit[atomic commit]
    Commit --> CheckP[fpga_drm_primary_atomic_check]
    Commit --> CheckO[fpga_drm_overlay_atomic_check if enabled]
    Commit --> Enable[fpga_drm_crtc_atomic_enable]
    Commit --> Flush[fpga_drm_crtc_atomic_flush]
    Enable --> Mode[fpga_drm_program_mode]
    Flush --> Snap[snapshot primary and overlay plane state]
    Snap --> Queue[fpga_drm_queue_commit_upload]
    Queue --> Schedule[schedule upload_work]
```

## DMA Path Used by `fpga_drm`

```mermaid
flowchart TD
    Work[fpga_drm_upload_work] --> Busy{DMA busy?}
    Busy -- yes --> Pending[set upload_pending]
    Busy -- no --> Submit[fpga_drm_submit_frame_nowait]
    Submit --> Copy[fpga_drm_copy_frame]
    Copy --> Lines[memcpy into active lines]
    Lines --> CB[initialize frame_cb]
    CB --> Xfer[xdma_xfer_submit_lines_nowait]
    Xfer --> Count[xdma_count_line_descriptors]
    Xfer --> Map[dma_map_sg if needed]
    Xfer --> Desc[build descriptors with EOP per line]
    Desc --> Queue[transfer_queue]
    Queue --> HW[XDMA H2C stream engine]
```

## Completion Path

```mermaid
flowchart TD
    HW[XDMA completion] --> Callback[fpga_drm_xdma_done]
    Timeout[dma_timeout_work] --> Queue[fpga_drm_queue_dma_completion]
    Callback --> Queue
    Queue --> Work[dma_complete_work]
    Work --> Complete[xdma_xfer_completion]
    Complete --> Finish[fpga_drm_dma_finish]
    Finish --> Wake[wake dma_idle_wq]
    Finish --> Maybe{upload_pending?}
    Maybe -- yes --> Upload[schedule upload_work]
    Maybe -- no --> Idle[DMA idle]
```

## Standalone XDMA Char Path

This path is documented because the code is present, but it is not compiled
into `fpga_drm.ko`.

```mermaid
flowchart TD
    Open[open /dev/xdmaN_h2c_0 or c2h_0] --> CharOpen[char_sgdma_open]
    Write[write/read] --> RW[char_sgdma_read_write]
    RW --> Check[check_transfer_align]
    RW --> MapUser[char_sgdma_map_user_buf_to_sgl]
    MapUser --> GUP[get_user_pages_fast]
    RW --> Submit[xdma_xfer_submit]
    Submit --> InitReq[xdma_init_request]
    Submit --> TransferInit[transfer_init]
    Submit --> Queue[transfer_queue]
    Queue --> EngineStart[engine_start]
    Submit --> Wait[wait on transfer wq]
    Wait --> Unmap[char_sgdma_unmap_user_buf]
```

## Error Cleanup Path

```mermaid
flowchart TD
    Probe[fpga_drm_probe] --> OpenXDMA[fpga_drm_open_xdma]
    OpenXDMA --> XOpen[xdma_device_open]
    XOpen --> ErrMSIX[disable MSI/MSI-X on failure]
    ErrMSIX --> ErrEng[remove engines]
    ErrEng --> ErrMap[unmap BARs]
    ErrMap --> ErrRegions[release PCI regions]
    ErrRegions --> ErrEnable[disable PCI device]
    ErrEnable --> Free[free xdma_dev]
    Probe --> Managed[DRM-managed cleanup for local resources]
```
