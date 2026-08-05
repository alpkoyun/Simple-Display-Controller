# Call Graphs

## Automatic Boot Path

```text
platform firmware
  -> read Expansion ROM BAR
  -> parse PCI ROM / PCIR / EFI headers
  -> load embedded SimpleDisplayGopDxe.efi
  -> SimpleDisplayGopEntryPoint()
     -> EfiLibInstallDriverBindingComponentName2()
  -> UEFI connect/controller dispatch
     -> SimpleDisplayDriverSupported()
     -> SimpleDisplayDriverStart()
        -> parent claim
        -> enable PCI memory decode
        -> SimpleDisplayHwInitializeContext()
        -> install HDMI child protocols
  -> firmware or bootloader locates GOP child
     -> QueryMode()
     -> SetMode(0)
     -> Blt(...)
```

ROM transport, image dispatch, parent binding, child creation, and GOP use are
separate nodes in this graph and require separate evidence.

## Driver Binding Supported

```text
SimpleDisplayDriverSupported(controller, remaining_path)
  -> validate remaining path
  -> OpenProtocol(PciIo, BY_DRIVER)
  -> if EFI_ALREADY_STARTED
       -> OpenProtocol(CallerId, GET_PROTOCOL)
       -> inspect retained private state
       -> return SUCCESS or ALREADY_STARTED
  -> SimpleDisplayHwMatchPci()
       -> PciIo.Pci.Read(PCI_TYPE00)
       -> check vendor/device/base class
  -> CloseProtocol(PciIo, BY_DRIVER)
```

No BAR operation is reachable from this path.

## Driver Binding Start

```text
SimpleDisplayDriverStart(controller, remaining_path)
  -> OpenProtocol(PciIo, BY_DRIVER)
  -> if new parent
       -> OpenProtocol(DevicePath, BY_DRIVER)
       -> SimpleDisplayHwMatchPci()
       -> AllocateZeroPool(private)
       -> InstallProtocolInterface(CallerId/private)
  -> if remaining path is end node
       -> return EFI_SUCCESS
  -> PciIo.Pci.Read(Command)
  -> PciIo.Attributes(Get)
  -> EnablePciMemoryDecode()
       -> PciIo.Attributes(Enable, MEMORY)
       -> PciIo.Pci.Read(Command)
       -> optional PciIo.Pci.Write(Command | MEMORY_SPACE)
       -> verify raw bit
  -> SimpleDisplayHwInitializeContext()
       -> PciIo.GetBarAttributes(BAR2)
       -> read SDC1 magic/version/features/ROM size
  -> AppendDevicePathNode(HDMI ADR)
  -> initialize GOP and empty EDID objects
  -> InstallMultipleProtocolInterfaces(child)
  -> OpenProtocol(parent PciIo, BY_CHILD_CONTROLLER)
```

## SetMode Path

```text
SimpleDisplaySetMode(0)
  -> SimpleDisplayHwValidateModeBounds()
  -> SimpleDisplayHwProgramMode()
       -> ConfigureGpio()
       -> ConfigurePixelUnpack()
       -> ConfigureColor()
       -> ConfigureHdmi()
          -> reset and enable AXI IIC
          -> program HDMI transmitter registers
       -> ConfigureClock()
          -> write dynamic clock registers
          -> request load
          -> wait for load clear and clock lock
       -> ConfigureVdma()
          -> reset S2MM and MM2S
          -> configure one MM2S frame store
          -> program stride, line bytes, AXI frame address, and height
       -> ConfigureVtc()
          -> program active, total, sync, polarity, and generator enable
       -> read VTC error
  -> publish GOP mode 0 and zero framebuffer fields
  -> SimpleDisplayBlt(VideoFill, black, full frame)
```

S2MM is reset but not used for GOP pixel uploads. GOP writes shared DDR through
BAR2; VDMA MM2S reads that same storage for scanout.

## BLT Paths

```text
SimpleDisplayBlt()
  -> validate initialized mode, operation, dimensions, bounds, and Delta
  -> allocate one-row bounce buffer for fill or video-to-video
  -> RaiseTPL(TPL_NOTIFY)
  -> operation
       VideoFill
         -> build one constant row
         -> WriteFrameBufferRow() for each destination row
       BufferToVideo
         -> calculate source row from Delta
         -> WriteFrameBufferRow() for each row
       VideoToBltBuffer
         -> calculate destination row from Delta
         -> ReadFrameBufferRow() for each row
       VideoToVideo
         -> select forward or reverse vertical order
         -> ReadFrameBufferRow() into bounce row
         -> WriteFrameBufferRow() to destination
  -> RestoreTPL()
  -> free bounce row
```

Both row helpers calculate `BAR2 + 0x02000000 + (y * ppsl + x) * 4`, check
BAR containment, and call PCI I/O with `EfiPciIoWidthUint32`.

## Child Stop and Reconnect

```text
Stop(parent, one child)
  -> validate child GOP/private relation
  -> CloseProtocol(PciIo, BY_CHILD_CONTROLLER)
  -> UninstallMultipleProtocolInterfaces(child)
  -> FreeChildResources()
  -> RestorePciAttributes()

Supported(parent, HDMI path)
  -> find retained parent private state
  -> return EFI_SUCCESS

Start(parent, HDMI path)
  -> capture current PCI state
  -> enable memory decode
  -> validate BAR2/SDC1
  -> install a new child
```

## Parent Stop

```text
Stop(parent, zero children)
  -> require child == NULL
  -> RestorePciAttributes()
  -> UninstallProtocolInterface(CallerId/private)
  -> CloseProtocol(DevicePath, BY_DRIVER)
  -> CloseProtocol(PciIo, BY_DRIVER)
  -> FreePrivate()
```

## Diagnostic Application Paths

```text
SimpleDisplayBringup.efi
  -> locate PCI I/O handles
  -> match controller
  -> enable memory access
  -> initialize context and test scratch
  -> program mode 0 and fill color bars
  -> read status
  -> BRINGUP_PASS

SimpleDisplayGopTest.efi
  -> locate GOP handles
  -> identify child by GOP + empty EDID + PixelBltOnly contract
  -> test mode 0 and invalid mode
  -> test all four BLT operations
  -> test overlap and invalid inputs
  -> GOP_TEST_PASS
```
