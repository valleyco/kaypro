# Baud Rate Generator (COM8116)

## Purpose
Programs serial speed for channel A (and in BIOS tables for channel B).

## Port Map

| Port | Symbol(s)    | Use                                        |
| ---- | ------------ | ------------------------------------------ |
| 00H  | BAUDA, baud  | Channel A baud divisor write               |
| 0CH  | BAUDB, baudb | Channel B baud divisor write (BIOS tables) |

## Common Divisors
- 02H: 110
- 05H: 300
- 06H: 600
- 07H: 1200
- 0AH: 2400
- 0CH: 4800
- 0EH: 9600
- 0FH: 19.2K

## Example

```asm
ld   a,0EH
out  (00H),a    ; set 9600 baud on channel A
```

## Source Pointers
- ../kii4tkit/BAUD.ASM
- ../kii4tkit/TERM.ASM
- ../kii4tkit/CONFIG.MAC
- ../kii4tkit/81-149C.MAC
