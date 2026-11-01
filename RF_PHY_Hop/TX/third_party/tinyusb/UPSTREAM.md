# TinyUSB Host provenance and CH585 adaptation

The `src/` snapshot and `LICENSE` are from speedypotato/tinyusb commit
`a1244c6d54207b36992091e8aad40b856323a2d4`, the submodule pinned by
OpenStickCommunity/GP2040-CE `0e33f08fbaf8bd3528950ed0d6f2ea73d259b153`.
`UPSTREAM.json` records both upstream and vendored SHA-256 values.

Only `tusb.c`, `common/tusb_fifo.c`, `host/usbh.c`, `host/hub.c`, and
`class/hid/hid_host.c` are compiled. Device and other portable drivers in this
snapshot are unmodified reference material and are not compiled. The existing
STM32 TinyUSB copy is independent and was not modified.

Local upstream changes are recorded in `patches/0001-*.patch`:

- Token-specific cancellation removes queued control requests before aborting an
  active request. This avoids dispatching cancelled authentication after F3.
- HCD completion epochs are checked before processing events, preventing queued
  completions from advancing a new control stage after abort/address reuse.
- A read-only enumeration phase accessor feeds metadata diagnostics.

`../../USB/hcd_ch585.c` is the CH585 port. It uses the CH585 SDK's register names,
USB_IRQn, independent 64-byte DMA buffers, round-robin packet scheduling,
low-speed PRE behind a full-speed hub, manual toggles, bounded completion
watchdogs, and SIE drain on cancellation. It does not compile or include CH32
register definitions. IRQ completion handling is deferred to the USB main loop;
NAK does not busy-wait or monopolize the bus. No heap is used by this port or the
OS_NONE host configuration.

`../../USB/usb_host.c` adapts HID/Hub plus a vendor class for XInput and Xbox GIP.
Host-ready is controller initialization only. An authentication interface becomes
eligible after all class setup has completed, never merely at SET_CONFIGURATION.

Upstream report fixture/license and source hash are under
`../../../../tools/tests/fixtures/gp2040-0e33f08/`. Protocol changes retain CRC and
length validation; those bounded checks are stricter than the upstream listener's
zero-length callback filtering. See `../../../../docs/ps-usb-gp2040-alignment.md`
for validation and hardware acceptance limits.
