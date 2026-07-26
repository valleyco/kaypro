# Disk Controller (Floppy)

## Purpose
Implements CP/M sector reads/writes through BIOS deblocking and low-level controller transfers.

## Controller Port Map

| Port | Symbol(s)    | Use                                           |
| ---- | ------------ | --------------------------------------------- |
| 10H  | status, cmnd | Status read / command write                   |
| 11H  | track        | Track register                                |
| 12H  | sector       | Sector register                               |
| 13H  | data         | Data register                                 |
| 1CH  | bitport      | Drive, side, density, motor, ROM bank control |

## Core Commands
- rdcmd = 10001000B
- wrtcmd = 10101100B
- seekcmd = 00010000B
- rstcmd = 00000000B
- ficmd = 11010000B

## BIOS Flow (Recommended)
1. Use read/write BIOS entry points, not raw controller loops.
2. BIOS translates CP/M sectors to host sectors and manages hstbuf.
3. BIOS seeks track and sets sector via hstcom.
4. Physical transfer is done with INI/OUTI loops.
5. BIOS retries, re-homes, and verifies write paths.

## Variant Note (KPIVROM)
KPIVROM adds side-select handling via bitport bit 2/side logic during trkset and sector adjustment in secset.

## Minimal Direct Snippets

```asm
; Seek track in C
ld   a,c
out  (13H),a
ld   a,00010000B
out  (10H),a

; Set sector in C
ld   a,c
out  (12H),a

; Read or write command
ld   a,10001000B
out  (10H),a
```

## Source Pointers
- ../kii4tkit/81-149C.MAC
- ../kii4tkit/KPIVROM.MAC
- ../kii4tkit/COPY.MAC
- ../kii4tkit/FORMAT.MAC
