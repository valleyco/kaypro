# Printer and System Bit Port

## Purpose
Parallel printer output and shared control/status bits for drive, density, motor, ROM banking, and printer handshaking.

## Port Map

| Port | Symbol(s)       | Use                            |
| ---- | --------------- | ------------------------------ |
| 08H  | pioad           | Printer data byte output       |
| 1CH  | bitport, spioad | System control and status bits |

## bitport Bit Meanings
- bit 0: drive select A
- bit 1: drive select B
- bit 2: board/variant dependent control (marked n/c in one BIOS comment)
- bit 3: Centronics ready input
- bit 4: Centronics strobe output
- bit 5: density select (0 means double density)
- bit 6: motor off (0 means motor on)
- bit 7: ROM enable (0 means ROM off)

## Printer Write Pattern

```asm
call liststat        ; busy check using bit 3
ld   a,c
out  (pioad),a       ; write byte
in   a,(bitport)
set  4,a             ; strobe high
out  (bitport),a
res  4,a             ; strobe low
out  (bitport),a
```

## Source Pointers
- ../kii4tkit/81-149C.MAC
- ../kii4tkit/KPIVROM.MAC
- ../kii4tkit/CONFIG.MAC
