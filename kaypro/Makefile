CC = gcc
ASM = asmx
CFLAGS = -std=c11 -Wall -Wextra -Werror -O0 -g \
	-Iinclude \
	-I../emu \
	-I../z80/include \
	-I../z80/src \
	-I../z80/src/generated \
	-Isrc/machine \
	-Isrc/devices \
	-Isrc/host

LDFLAGS = -L../z80 -L../emu -lz80 -lemu

MACHINE_SRC = \
	src/machine/kaypro.c \
	src/machine/kaypro_mem.c \
	src/machine/kaypro_ports.c

DEVICE_SRC = \
	src/devices/sysport.c \
	src/devices/sio.c \
	src/devices/fdc1793.c \
	src/devices/keyboard.c \
	src/devices/crt6845.c \
	src/devices/hdc_wd1002.c

HOST_POSIX_SRC = src/host/posix/host_posix.c
RUN_OPTS_SRC = src/host/posix/run_opts.c
MAIN_SRC = src/host/posix/main.c
SMOKE_SRC = tests/smoke_test.c
FDC_SMOKE_SRC = tests/fdc_smoke_test.c
SIO_SMOKE_SRC = tests/sio_smoke_test.c
CRT_SMOKE_SRC = tests/crt_smoke_test.c
HDC_SMOKE_SRC = tests/hdc_smoke_test.c
ROM_SMOKE_ASM = tests/rom_smoke.asm
ROM_SMOKE_BIN = tests/rom_smoke.bin
ROM_SMOKE_LST = tests/rom_smoke.lst
ROM_SMOKE_RUNNER_SRC = tests/rom_smoke_runner.c
ROM_NMI_SMOKE_ASM = tests/rom_nmi_smoke.asm
ROM_NMI_SMOKE_BIN = tests/rom_nmi_smoke.bin
ROM_NMI_SMOKE_LST = tests/rom_nmi_smoke.lst
ROM_NMI_SMOKE_RUNNER_SRC = tests/rom_nmi_smoke_runner.c

CORE_SRC = $(MACHINE_SRC) $(DEVICE_SRC)

.DEFAULT_GOAL := help

.PHONY: help all clean test run fetch-assets \
	smoke fdc-smoke sio-smoke crt-smoke hdc-smoke rom-smoke rom-nmi-smoke

help: ## Show this help
	@awk 'BEGIN{FS=":.*## "; printf "Goals:\n"} /^[a-zA-Z0-9_-]+:.*## /{printf "  %-16s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

all: ../emu/libemu.a ../z80/libz80.a kaypro_run ## Build kaypro_run (+ libs)

../emu/libemu.a:
	$(MAKE) -C ../emu

../z80/libz80.a:
	$(MAKE) -C ../z80 lib

kaypro_run: ../emu/libemu.a ../z80/libz80.a $(CORE_SRC) $(HOST_POSIX_SRC) $(RUN_OPTS_SRC) $(MAIN_SRC) ## Build the emulator binary
	$(CC) $(CFLAGS) -o $@ $(CORE_SRC) $(HOST_POSIX_SRC) $(RUN_OPTS_SRC) $(MAIN_SRC) $(LDFLAGS)

run: kaypro_run ## Build and run kaypro_run (kaypro.conf)
	./kaypro_run

fetch-assets: ## Download/convert ROM and disk images
	bash ../tools/fetch_kaypro_assets.sh

smoke: ../emu/libemu.a ../z80/libz80.a tests/smoke_test ## Bank-1 ROM read smoke
	./tests/smoke_test

fdc-smoke: ../emu/libemu.a ../z80/libz80.a tests/fdc_smoke_test ## FDC attach/read smoke
	./tests/fdc_smoke_test

sio-smoke: ../emu/libemu.a ../z80/libz80.a tests/sio_smoke_test ## SIO smoke
	./tests/sio_smoke_test

crt-smoke: ../emu/libemu.a ../z80/libz80.a tests/crt_smoke_test ## CRT 80x25 cell buffer smoke
	./tests/crt_smoke_test

hdc-smoke: ../emu/libemu.a ../z80/libz80.a tests/hdc_smoke_test ## HDC stub smoke
	./tests/hdc_smoke_test

rom-smoke: ../emu/libemu.a ../z80/libz80.a tests/rom_smoke_runner ## Assembled ROM smoke
	./tests/rom_smoke_runner

rom-nmi-smoke: ../emu/libemu.a ../z80/libz80.a tests/rom_nmi_smoke_runner ## ROM NMI/FDC smoke
	./tests/rom_nmi_smoke_runner

tests/smoke_test: $(CORE_SRC) $(SMOKE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/fdc_smoke_test: $(CORE_SRC) $(HOST_POSIX_SRC) $(FDC_SMOKE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/sio_smoke_test: $(CORE_SRC) $(SIO_SMOKE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/crt_smoke_test: $(CORE_SRC) $(CRT_SMOKE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/hdc_smoke_test: $(CORE_SRC) $(HDC_SMOKE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(ROM_SMOKE_BIN) $(ROM_SMOKE_LST): $(ROM_SMOKE_ASM)
	$(ASM) -e -w -C Z80 -l $(ROM_SMOKE_LST) -b -o $(ROM_SMOKE_BIN) $(ROM_SMOKE_ASM)

tests/rom_smoke_runner: $(CORE_SRC) $(HOST_POSIX_SRC) $(ROM_SMOKE_RUNNER_SRC) $(ROM_SMOKE_BIN)
	$(CC) $(CFLAGS) -o $@ $(CORE_SRC) $(HOST_POSIX_SRC) $(ROM_SMOKE_RUNNER_SRC) $(LDFLAGS)

$(ROM_NMI_SMOKE_BIN) $(ROM_NMI_SMOKE_LST): $(ROM_NMI_SMOKE_ASM)
	$(ASM) -e -w -C Z80 -l $(ROM_NMI_SMOKE_LST) -b -o $(ROM_NMI_SMOKE_BIN) $(ROM_NMI_SMOKE_ASM)

tests/rom_nmi_smoke_runner: $(CORE_SRC) $(HOST_POSIX_SRC) $(ROM_NMI_SMOKE_RUNNER_SRC) $(ROM_NMI_SMOKE_BIN)
	$(CC) $(CFLAGS) -o $@ $(CORE_SRC) $(HOST_POSIX_SRC) $(ROM_NMI_SMOKE_RUNNER_SRC) $(LDFLAGS)

test: smoke fdc-smoke sio-smoke crt-smoke hdc-smoke rom-smoke rom-nmi-smoke ## Run all kaypro smoke tests

clean: ## Remove kaypro_run and test binaries
	rm -f kaypro_run tests/smoke_test tests/fdc_smoke_test tests/sio_smoke_test
	rm -f tests/crt_smoke_test tests/hdc_smoke_test
	rm -f tests/rom_smoke.bin tests/rom_smoke.lst tests/rom_smoke_runner
	rm -f tests/rom_nmi_smoke.bin tests/rom_nmi_smoke.lst tests/rom_nmi_smoke_runner
