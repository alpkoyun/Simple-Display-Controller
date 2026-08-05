# 2026-08-03 SPI-boot targeted-connect attempt

## Raw evidence preservation

The new Shell files were copied byte-for-byte from the FAT USB into `raw/`.
The hashes in `usb_raw_sha256.txt` and `workspace_raw_sha256.txt` match for
every file. The five non-empty files are US-ASCII. `load.log` is an untouched
zero-byte file.

## Handle evidence

`drivers-loaded.txt` records the SimpleDisplay driver image at handle `119`.
It is not shown as managing a device or child in that snapshot.

`devices-loaded.txt` records:

- `E8`: `PciRoot(0x0)/Pci(0x1,0x0)`;
- `E9`: `PciRoot(0x0)/Pci(0x1,0x0)/Pci(0x0,0x0)`.

The latter is the downstream PCI function corresponding to the FPGA endpoint
in the preceding validated UEFI topology.

The file `connect=targeted.txt` contains exactly:

```text
Connect - Handle [E9] Result Success.
```

Therefore the only captured `connect` result accepted controller handle `E9`;
it did not reject that handle. The output does not echo the requested driver
handle, so this line by itself does not prove that `119` was supplied or that
the SimpleDisplay driver installed a GOP child.

## Timestamp ordering

The files were written in this order:

```text
17:47:12  drivers-loaded.txt
17:47:50  devices-loaded.txt
18:07:26  drivers-bound.txt
18:08:20  devices-after-connect.txt
18:15:14  connect=targeted.txt
18:24:14  load.log (zero bytes)
```

`drivers-bound.txt` is byte-identical to `drivers-loaded.txt`, and
`devices-after-connect.txt` is byte-identical to `devices-loaded.txt`.
However, both purported post-connect snapshots predate the successful E9
connect result by seven to eight minutes. They cannot be used to determine
what changed after that successful command.

This attempt consequently does not yet prove driver binding, GOP protocol
installation, framebuffer preservation during Driver Binding `Start()`, or
`GOP_TEST_PASS`.

## Current host state

The read-only JTAG probe after Linux boot finds `xc7a200t_0` programmed with a
design that matches the shell-stage LTX and contains both expected ILAs. This
confirms the FPGA currently holds the intended design without another JTAG
program operation during this inspection.

The current Linux boot does not enumerate the CPU PCIe root port at `00:01.0`
or the FPGA endpoint at `01:00.0`. The UEFI device table above did contain that
root-port/downstream-function topology in the earlier Shell session. Thus the
current missing Linux endpoint is a separate reboot/enumeration result, not
evidence that UEFI rejected handle `E9`.

## Next controlled capture

After a cold power-on, first capture fresh `drivers` and `devices` tables.
Discover both handles again; do not assume they remain `E9` and `119`. Then
run the targeted command without `-r` and capture the post-command tables only
after it returns:

```text
connect <current-fpga-controller> <current-simpledisplay-driver> >a evidence\connect-targeted-2.txt
drivers >a evidence\drivers-postconnect-2.txt
devices >a evidence\devices-postconnect-2.txt
dh -p GraphicsOutput >a evidence\graphics-output-postconnect-2.txt
```

For the captured boot, the candidate command was `connect E9 119`. Stop and
reset if either display changes during targeted binding. Do not use
`connect -r` for this isolation test.
