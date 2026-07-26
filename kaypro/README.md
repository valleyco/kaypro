# Kaypro 4/84 emulator

Terminal-first CP/M machine built on `libz80` and the shared `emu/` port-device framework.

## Layers

| Layer | Path | Rule |
|-------|------|------|
| CPU | `../z80/` | Freestanding; bus callbacks only |
| Port framework | `../emu/` | Freestanding port mux |
| Machine + devices | `src/machine/`, `src/devices/` | Pure Kaypro; I/O via host ops / memory blobs |
| Host (POSIX) | `src/host/posix/` | CLI, `fopen`, stdin/stdout, ANSI frame dump; later `host/esp32/` |

Host console/keyboard/logging/display is injected with `kaypro_set_host()` / `kaypro_host_posix_install()`.
ROM and disks load as bytes (`kaypro_load_rom_bytes`, `kaypro_attach_disk_mem`); path helpers live in the POSIX host.

### Display split

- **Portable:** CRT keeps an 80×25 character cell buffer. Data-port writes store at the CRT memory address (R18/R19). Visible cursor is R14/R15.
- **PC host:** `display_refresh` dumps that buffer inside an optional colored border and moves the terminal cursor to the hardware cursor cell. ESP32/SDL can replace only this callback later.

## Build

From `kaypro/`, bare `make` lists goals (it does not build). From the repo root,
`make` is a short umbrella help; Kaypro-specific goals live here.

```bash
make                # list kaypro goals
make all            # build kaypro_run (+ libs)
make test           # all smoke tests
make fetch-assets   # download/convert ROM and disk images
make run            # build and run (uses kaypro.conf)

# from repo root
make                # umbrella help
make kaypro         # build via kaypro/ (make -C kaypro all)
make -C kaypro run
```

## Prerequisites

Download the Universal ROM and CP/M disk:

```bash
make fetch-assets                 # from kaypro/
# or from repo root:
bash tools/fetch_kaypro_assets.sh
```

That places ROMs under `assets/rom/`, TD0s under `assets/td0/`, and raw
images under `assets/images/{ssdd,dsdd}/` (SS disks are also rewritten to
DSDD via cpmtools for Universal CP/M).

## Run

CLI paths are relative to your current working directory. Model presets fill
only unset `--rom` / `--disk-a` / `--images-dir` paths, resolved against the
executable directory:

```bash
# from kaypro/ directory (uses kaypro.conf or --model defaults)
./kaypro_run --model 4

# from repo root
./kaypro/kaypro_run --rom kaypro/assets/rom/81-478a.rom --disk-a kaypro/assets/images/dsdd/kaypro1.dsk

# from kaypro/ directory
./kaypro_run --rom assets/rom/81-478a.rom --disk-a assets/images/dsdd/kaypro1.dsk

# log IN/OUT during bring-up
./kaypro_run --rom assets/rom/81-478a.rom --disk-a assets/images/dsdd/kaypro1.dsk --trace
```

| `--model` | Defaults | Notes |
|-----------|----------|--------|
| `4` / `4-84` | Universal ROM + `images/dsdd` | Known-good path (default) |
| `10` | Same as `4` | HDC still stub → floppy boot |
| `ii` / `2` | Early ROM + `images/ssdd` | Experimental: SSDD geometry on Universal CRT/ports |

FDC accepts native **SSDD** (`40×1×10×512` = 204800) and **DSDD**
(`40×2×10×512` = 409600) images. Side-1 access on SSDD fails cleanly (no
alias into later tracks).

### Config file

The same options can live in a config file (one CLI-style flag per line; `#` comments allowed):

```
# kaypro.conf
--model 4
--rom assets/rom/81-478a.rom
--disk-a assets/images/dsdd/kaypro1.dsk
--images-dir assets/images/dsdd
--border-color green
--trace
```

- Default: `<dir-of-kaypro_run>/kaypro.conf` if that file exists
- Override path: `--config FILE` (relative to the current working directory)
- Paths inside the config file are relative to the config file's directory
- Command-line options override the config file
- `--model` fills only paths not set by config/CLI
- `--images-dir` is the directory of `.dsk` files listed by **Select drive** in the host menu
- `--border-color` frames the 80×25 CRT (`green` by default; named ANSI colors, `#RRGGBB`, or `none`)

### Keyboard (POSIX host)

| Key | Action |
|-----|--------|
| `Ctrl+C` | Sent to CP/M as `0x03` (does **not** quit the emulator) |
| `Ctrl+O` | Open host menu (`O` select drive, `X` exit, Esc close) |
| `Ctrl+\` | Quit (backup; prefer menu `X`) |

A status line under the CRT border shows `Ctrl-O menu   Ctrl-C to CP/M`.

**Select drive:** pick A/B, then type image id + Enter (`n`/`p` page, Esc cancel).
Remounting updates the host FDC immediately. If CP/M already has files open on
that drive, the guest may see inconsistent disk state — prefer swapping when
the drive is idle.

At the MBASIC `Ok` prompt, `Ctrl+C` only echoes as `^C` — it does not break
or quit. Type a command such as `PRINT 1` and press Return. Universal CP/M
expects DSDD under `assets/images/dsdd/`; native SSDD images live under
`assets/images/ssdd/` (and are also rewritten to DSDD by `fetch-assets` for
Universal). `ii` is a convenience preset, not a full ’83 video/port map.


## Layout

| Path | Role |
|------|------|
| `src/machine/` | Kaypro wiring, memory map, port registration |
| `src/devices/` | sysport, SIO, FDC1793, keyboard, CRT (SY6545 VRAM), HDC stub |
| `src/host/kaypro_host.h` | Host ops + path-loader declarations |
| `src/host/posix/` | Desktop CLI, stdin/stdout, file loaders, ANSI CRT dump |
| `docs/ports.md` | II-era I/O port index (see note on Universal vs II maps) |
| `docs/device-ports/` | Per-device hardware notes from early BIOS/ROM |
| `docs/kii4tkit/` | Original Kaypro II / KPIV assembly sources (reference) |
| `tests/smoke_test.c` | Bank-1 ROM read regression |
| `tests/crt_smoke_test.c` | SY6545 ports + 80×25 cell buffer / cursor |
| `tests/hdc_smoke_test.c` | WD1002 absent-controller fail-fast |

## Status

Phase 1: memory banking, port dispatch, FDC/SIO, NMI on HALT, CRT 80×25
cell buffer (Universal 1Ch-1Fh), and a WD1002-HD0 stub at 80h-87h that fails
`winrest` so ROM falls back to floppies.

Universal ROM boots CP/M from a 40×2×10×512 `.dsk` and reaches the `A0>`
prompt. Display is a VRAM dump; the host cursor follows CRT R14/R15.
