# Kaypro Port Manual Index

Hardware notes from early Kaypro II / KPIV sources (`kii4tkit/`). The running
emulator defaults to the Universal 4/84 map (SIO `04–07`, FDC `10–13`, sysport
`14H`, CRT `1C–1F`, HDC stub `80–87`); treat the II-era bitport/video details
below as reference for `--model ii`, not as the Universal wiring.

## Quick summary

I/O PORT ADDRESSES
Port #	Use and/or assignment
00H	Baud rate. (Write only)
	Writing a number between 0 and F to this port will set the RS-232C baud rate.
04H	RS-232C Serial Data. (R/W)
	Data register of the Z-80 SIO
05H	Keyboard Data. (R/W)
	Eight-bit data from detachable keyboard. See the following S-BASIC program for an example of writing to this port.
06H	RS-232C Status. (R/W)
	Control/status port for the Z-80 SIO. See Zilog and Mostek Microcomputer Data Books.
08H 	Printer Port. (Write only)
	Eight-bit data to parallel printer connector
1CH	System Port. (R/W)
	This port is used for system control. The various bits are used for memory bank selection, disk drive control, and printer handshaking.

---

## Device Files

- Serial SIO: [device-ports/serial-sio.md](device-ports/serial-sio.md)
- Keyboard on SIO-B: [device-ports/keyboard-sio-b.md](device-ports/keyboard-sio-b.md)
- Baud generator (COM8116): [device-ports/baud-generator.md](device-ports/baud-generator.md)
- Printer and shared system bit port: [device-ports/printer-and-system-port.md](device-ports/printer-and-system-port.md)
- Disk controller and floppy flow: [device-ports/disk-controller.md](device-ports/disk-controller.md)
- CTC timer/counter channels: [device-ports/ctc-timer-counter.md](device-ports/ctc-timer-counter.md)
- Video memory-mapped display path: [device-ports/video-memory-mapped.md](device-ports/video-memory-mapped.md)

## Source kit

Original Kaypro assembly used for these notes lives in [kii4tkit/](kii4tkit/).
