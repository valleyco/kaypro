# CTC (Z80 Counter/Timer Channels)

## Purpose
Provides timer/counter channels and interrupt vector support for system timing and interrupt framework.

## Port Map

| Port | Symbol(s) | Use           |
| ---- | --------- | ------------- |
| 18H  | ctc0      | CTC channel 0 |
| 19H  | ctc1      | CTC channel 1 |
| 1AH  | ctc2      | CTC channel 2 |
| 1BH  | ctc3      | CTC channel 3 |

## Control Bits Defined
- ctccmd = 01H
- ctcint = 80H
- ctcm1 = 40H (counter mode)
- ctcm0 = 00H (timer mode)
- range = 20H (prescaler 256, else 16)
- slope = 10H
- trigger = 08H
- ltc = 04H (time constant follows)
- rsetctc = 02H (reset channel)

## Notes
The BIOS files define complete CTC equates and vector masks (`ctcivec`, `ctcvmsk`). In the provided sources, CTC is primarily documented/configured via equates rather than extensive standalone service routines.

## Source Pointers
- ../kii4tkit/81-149C.MAC
- ../kii4tkit/KPIVROM.MAC
