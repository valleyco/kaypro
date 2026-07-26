# Keyboard Path (SIO Channel B)

## Purpose
Keyboard I/O uses SIO channel B routines in BIOS, including status polling, input mapping, and bell/command output.

## Port Map

| Port | Symbol(s) | Use                      |
| ---- | --------- | ------------------------ |
| 07H  | siob0     | Channel B status/control |
| 05H  | siob1     | Channel B data           |

## BIOS Routines
- kbdstat: poll siob0 with RCA bit
- kbdin: wait for char, read siob1, then run key map translation
- kbdout: wait for TBE and write output byte to siob1

## Typical Pattern

```asm
kbdin:
    call kbdstat
    jr   z,kbdin
    in   a,(siob1)
    call kbdmap
```

## Notes
The vector keypad translation table maps special key codes into BIOS-internal codes.

## Source Pointers
- ../kii4tkit/81-149C.MAC
- ../kii4tkit/KPIVROM.MAC
