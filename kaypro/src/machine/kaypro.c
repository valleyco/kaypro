#include <stdlib.h>
#include <string.h>

#include "kaypro.h"
#include "kaypro_host.h"
#include "kaypro_internal.h"
#include "crt6845.h"
#include "fdc1793.h"
#include "hdc_wd1002.h"
#include "keyboard.h"
#include "sio.h"
#include "sysport.h"

kaypro_t *kaypro_create(void) {
  kaypro_t *m = (kaypro_t *)calloc(1, sizeof(kaypro_t));
  if (!m) return NULL;

  kaypro_mem_init(m);
  m->bank1 = true;

  m->fdc = kaypro_fdc_create();
  m->sio = kaypro_sio_create();
  m->sysport = kaypro_sysport_create(m, m->fdc);
  m->keyboard = kaypro_keyboard_create(m->sio);
  m->crt = kaypro_crt_create();
  m->hdc = kaypro_hdc_create();

  if (!m->fdc || !m->sio || !m->sysport || !m->keyboard || !m->crt || !m->hdc) {
    kaypro_destroy(m);
    return NULL;
  }

  kaypro_add_device(m, &m->fdc->emu);
  kaypro_add_device(m, &m->sio->emu);
  kaypro_add_device(m, &m->sysport->emu);
  kaypro_add_device(m, &m->keyboard->emu);
  kaypro_add_device(m, &m->crt->emu);
  kaypro_add_device(m, &m->hdc->emu);
  kaypro_register_ports(m);

  z80_bus_t bus = {
      .mem_read = kaypro_bus_mem_read,
      .mem_write = kaypro_bus_mem_write,
      .io_read = kaypro_bus_io_read,
      .io_write = kaypro_bus_io_write,
      .bus_ctx = m,
  };
  z80_init(&m->cpu, bus);
  kaypro_reset(m);
  return m;
}

void kaypro_destroy(kaypro_t *m) {
  if (!m) return;
  for (int i = 0; i < m->device_count; i++) {
    EmuDevice *dev = m->devices[i];
    if (dev && dev->destroy) dev->destroy(dev);
  }
  free(m->devices);
  free(m);
}

void kaypro_set_host(kaypro_t *m, const kaypro_host_ops_t *ops) {
  if (!m) return;
  if (ops)
    m->host = *ops;
  else
    memset(&m->host, 0, sizeof(m->host));
}

bool kaypro_load_rom_bytes(kaypro_t *m, const uint8_t *data, size_t len) {
  if (!m || !data || len == 0) return false;
  size_t n = len < KAYPRO_ROM_SIZE ? len : KAYPRO_ROM_SIZE;
  memset(m->rom, 0, KAYPRO_ROM_SIZE);
  memcpy(m->rom, data, n);
  return true;
}

bool kaypro_attach_disk_mem(kaypro_t *m, int drive, uint8_t *data, size_t size) {
  if (!m || !m->fdc) return false;
  return kaypro_fdc_attach_mem(m->fdc, drive, data, size);
}

void kaypro_mark_display_dirty(kaypro_t *m) {
  if (!m || !m->crt) return;
  kaypro_crt_mark_dirty(m->crt);
}

void kaypro_reset(kaypro_t *m) {
  for (int i = 0; i < m->device_count; i++) {
    EmuDevice *dev = m->devices[i];
    if (dev && dev->reset) dev->reset(dev);
  }
  m->needs_nmi = false;
  z80_reset(&m->cpu);
  m->bank1 = true;
  kaypro_sysport_sync_bank(m->sysport);
}

void kaypro_step(kaypro_t *m, unsigned m_cycles) {
  /* Host keyboard → SIO channel B (Kaypro console keyboard).
   * Drain all pending host bytes each step so typed-ahead input is not
   * limited to one character per m-cycle chunk. */
  if (m->host.console_poll) {
    for (int n = 0; n < 64; n++) {
      int c = m->host.console_poll(m->host.ctx);
      if (c < 0) break;
      kaypro_sio_push_rx_b(m->sio, (uint8_t)(c & 0x7F));
    }
  }

  for (int i = 0; i < m->device_count; i++) {
    EmuDevice *dev = m->devices[i];
    if (dev && dev->poll) dev->poll(dev);
  }

  z80_run_m(&m->cpu, m_cycles);

  /* FDC DRQ/INTRQ are delivered as NMI while the ROM is HALTed. */
  for (int guard = 0; guard < 2048; guard++) {
    if (kaypro_fdc_needs_nmi(m->fdc)) m->needs_nmi = true;
    if (!m->cpu.halted || !m->needs_nmi) break;
    z80_nmi(&m->cpu);
    m->needs_nmi = false;
    kaypro_fdc_clear_nmi(m->fdc);
    z80_run_m(&m->cpu, 32);
  }

  if (m->host.display_refresh && kaypro_crt_is_dirty(m->crt)) {
    unsigned cursor_col = 0;
    unsigned cursor_row = 0;
    kaypro_crt_cursor(m->crt, &cursor_col, &cursor_row);
    if (m->host.display_refresh(m->host.ctx, kaypro_crt_cells(m->crt),
                                kaypro_crt_cols(), kaypro_crt_rows(),
                                cursor_col, cursor_row)) {
      kaypro_crt_clear_dirty(m->crt);
    }
  }
}

void kaypro_run(kaypro_t *m) {
  while (!m->cpu.halted) {
    kaypro_step(m, 1000);
  }
}

void kaypro_set_trace_io(kaypro_t *m, bool enable) { m->trace_io = enable; }

uint8_t kaypro_mem_read(kaypro_t *m, uint16_t addr) {
  return kaypro_bus_mem_read(m, addr);
}

void kaypro_set_bank1(kaypro_t *m, bool bank1) {
  m->bank1 = bank1;
  kaypro_sysport_set_latch(m->sysport,
                           (uint8_t)((kaypro_sysport_latch(m->sysport) & 0x7F) |
                                     (bank1 ? 0x80 : 0)));
}

bool kaypro_halted(const kaypro_t *m) { return m->cpu.halted; }

uint16_t kaypro_pc(const kaypro_t *m) { return m->cpu.pc; }

bool kaypro_fetch_trap_hit(const kaypro_t *m) { return z80_fetch_trap(&m->cpu); }

uint16_t kaypro_fetch_trap_addr(const kaypro_t *m) {
  return z80_fetch_trap_pc(&m->cpu);
}

uint8_t kaypro_sysport_state(const kaypro_t *m) {
  return kaypro_sysport_latch(m->sysport);
}
