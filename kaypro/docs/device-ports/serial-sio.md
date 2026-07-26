# Serial Device (Z80 SIO)

## Purpose
Channel A is used for RS-232 terminal I/O. Channel B is used by keyboard-facing routines in BIOS and utility code.

## Port Map

| Port | Symbol(s)     | Use                      |
| ---- | ------------- | ------------------------ |
| 04H  | SIODPA, sioa1 | Channel A data           |
| 05H  | SIODPB, siob1 | Channel B data           |
| 06H  | SIOCPA, sioa0 | Channel A control/status |
| 07H  | SIOCPB, siob0 | Channel B control/status |

## Key Status Bits
- RCA (bit 0): receive character available
- TBE (bit 2): transmit buffer empty

## Typical Polling Pattern

```asm
; Wait for received char on channel A
rx_wait:
    in   a,(sioa0)
    and  rca
    jr   z,rx_wait
    in   a,(sioa1)

; Wait for transmitter ready on channel A
tx_wait:
    in   a,(sioa0)
    and  tbe
    jr   z,tx_wait
    ld   a,c
    out  (sioa1),a
```

## Source Pointers
- ../kii4tkit/TERM.ASM
- ../kii4tkit/BAUD.ASM
- ../kii4tkit/81-149C.MAC
- ../kii4tkit/KPIVROM.MAC
