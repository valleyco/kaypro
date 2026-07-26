# Video (Memory-Mapped Display)

## Purpose
Character display output is implemented through memory-mapped video RAM, with one control-port write used during initialization.

## Memory Map and Control
- Video RAM base: 3000H
- Logical line length: 80
- Physical stride: 128
- Display lines: 24
- Scroll register reset write: OUT (14H),17H in vidinit

## Behavior
- vidinit clears display state and resets scroll register.
- vidout writes characters into video RAM and handles control characters.
- Scrolling is done with LDIR memory moves across mapped display lines.
- Bell output path routes to keyboard output routine (`kbdout`) with code 04H.

## Minimal Snippet

```asm
vidinit:
    ld   a,17H
    out  (14H),a
```

## Notes
This is not a pure port-driven video adapter API. Most output operations are memory writes to mapped RAM.

## Source Pointers
- ../kii4tkit/81-149C.MAC
- ../kii4tkit/KPIVROM.MAC
