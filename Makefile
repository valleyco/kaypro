.DEFAULT_GOAL := help

.PHONY: help all z80 kaypro emu test clean

help:
	@printf 'Goals:\n'
	@printf '  %-10s %s\n' all 'Build z80 + kaypro'
	@printf '  %-10s %s\n' z80 'Build libz80'
	@printf '  %-10s %s\n' emu 'Build libemu'
	@printf '  %-10s %s\n' kaypro 'Build kaypro_run (via kaypro/)'
	@printf '  %-10s %s\n' test 'Run z80 tests'
	@printf '  %-10s %s\n' clean 'Clean z80, emu, kaypro'
	@printf '\nKaypro-specific goals (fetch-assets, run, smokes):\n'
	@printf '  make -C kaypro\n'
	@printf '  make -C kaypro help\n'

all: z80 kaypro

z80:
	$(MAKE) -C z80

kaypro: z80 emu
	$(MAKE) -C kaypro all

emu:
	$(MAKE) -C emu

test:
	$(MAKE) -C z80 test

clean:
	$(MAKE) -C z80 clean
	$(MAKE) -C emu clean
	$(MAKE) -C kaypro clean
