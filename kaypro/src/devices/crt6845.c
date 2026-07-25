#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "crt6845.h"

#define CRT_REG_START_HI 0x0C
#define CRT_REG_START_LO 0x0D
#define CRT_REG_CURSOR_HI 0x0E
#define CRT_REG_CURSOR_LO 0x0F
#define CRT_REG_ADDR_HI 0x12
#define CRT_REG_ADDR_LO 0x13

typedef struct {
  EmuDevice emu;
  uint8_t reg_sel;
  uint8_t regs[32];
  uint16_t mem_addr;
  bool attr_plane;
  bool dirty;
  bool view_valid;
  uint8_t vram[KAYPRO_CRT_VRAM];
  uint8_t view[KAYPRO_CRT_CELLS];
} crt_impl_t;

static void crt_fill_spaces(crt_impl_t *crt) {
  memset(crt->vram, ' ', sizeof(crt->vram));
  crt->view_valid = false;
  crt->dirty = true;
}

static uint16_t crt_start_addr(const crt_impl_t *crt) {
  return (uint16_t)(((crt->regs[CRT_REG_START_HI] & 0x07) << 8) |
                    crt->regs[CRT_REG_START_LO]);
}

static uint16_t crt_cursor_addr(const crt_impl_t *crt) {
  return (uint16_t)(((crt->regs[CRT_REG_CURSOR_HI] & 0x07) << 8) |
                    crt->regs[CRT_REG_CURSOR_LO]);
}

static void crt_rebuild_view(crt_impl_t *crt) {
  uint16_t start = (uint16_t)(crt_start_addr(crt) & KAYPRO_CRT_VRAM_MASK);
  for (unsigned i = 0; i < KAYPRO_CRT_CELLS; i++) {
    crt->view[i] = crt->vram[(start + i) & KAYPRO_CRT_VRAM_MASK];
  }
  crt->view_valid = true;
}

static int crt_read_status(void *dev, int port) {
  (void)dev;
  (void)port;
  /* Bit 7 set = update ready (ROM waits with `or a; jp p,...`). */
  return 0x80;
}

static int crt_read_regdata(void *dev, int port) {
  crt_impl_t *crt = (crt_impl_t *)dev;
  (void)port;
  if (crt->reg_sel < 32) return crt->regs[crt->reg_sel];
  return 0;
}

static int crt_read_unused(void *dev, int port) {
  (void)dev;
  (void)port;
  return 0;
}

static int crt_read_data(void *dev, int port) {
  crt_impl_t *crt = (crt_impl_t *)dev;
  (void)port;
  return crt->vram[crt->mem_addr & KAYPRO_CRT_VRAM_MASK];
}

static void crt_write_cmd(void *dev, int port, int value) {
  crt_impl_t *crt = (crt_impl_t *)dev;
  (void)port;
  crt->reg_sel = (uint8_t)(value & 0x1F);
}

static void crt_apply_addr(crt_impl_t *crt) {
  uint16_t hi = crt->regs[CRT_REG_ADDR_HI];
  uint16_t lo = crt->regs[CRT_REG_ADDR_LO];
  crt->attr_plane = (hi & 0x08) != 0;
  crt->mem_addr = (uint16_t)(((hi & 0x07) << 8) | lo);
}

static void crt_write_regdata(void *dev, int port, int value) {
  crt_impl_t *crt = (crt_impl_t *)dev;
  (void)port;
  if (crt->reg_sel < 32) crt->regs[crt->reg_sel] = (uint8_t)value;
  if (crt->reg_sel == CRT_REG_ADDR_HI || crt->reg_sel == CRT_REG_ADDR_LO)
    crt_apply_addr(crt);
  /* Hardware scroll: BIOS advances R12/R13 instead of moving VRAM. */
  if (crt->reg_sel == CRT_REG_START_HI || crt->reg_sel == CRT_REG_START_LO) {
    crt->view_valid = false;
    crt->dirty = true;
  }
  /* Visible hardware cursor (R14/R15) — host must repaint to move it. */
  if (crt->reg_sel == CRT_REG_CURSOR_HI || crt->reg_sel == CRT_REG_CURSOR_LO)
    crt->dirty = true;
}

static void crt_write_unused(void *dev, int port, int value) {
  (void)dev;
  (void)port;
  (void)value;
}

static void crt_write_data(void *dev, int port, int value) {
  crt_impl_t *crt = (crt_impl_t *)dev;
  (void)port;
  if (!crt->attr_plane) {
    unsigned idx = (unsigned)crt->mem_addr & KAYPRO_CRT_VRAM_MASK;
    crt->vram[idx] = (uint8_t)value;
    crt->view_valid = false;
    crt->dirty = true;
  }
  crt->mem_addr = (uint16_t)((crt->mem_addr + 1) & KAYPRO_CRT_VRAM_MASK);
}

static void crt_reset(EmuDevice *dev) {
  crt_impl_t *crt = (crt_impl_t *)dev->ctx;
  memset(crt->regs, 0, sizeof(crt->regs));
  crt->reg_sel = 0;
  crt->mem_addr = 0;
  crt->attr_plane = false;
  crt->view_valid = false;
  crt_fill_spaces(crt);
}

static void crt_destroy(EmuDevice *dev) { free(dev->ctx); }

static int (*crt_reads[])(void *, int) = {
    crt_read_status, crt_read_regdata, crt_read_unused, crt_read_data};
static void (*crt_writes[])(void *, int, int) = {
    crt_write_cmd, crt_write_regdata, crt_write_unused, crt_write_data};

const uint8_t *kaypro_crt_cells(const kaypro_crt_t *crt) {
  crt_impl_t *impl = (crt_impl_t *)crt;
  if (!impl->view_valid) crt_rebuild_view(impl);
  return impl->view;
}

unsigned kaypro_crt_cols(void) { return KAYPRO_CRT_COLS; }

unsigned kaypro_crt_rows(void) { return KAYPRO_CRT_ROWS; }

void kaypro_crt_cursor(const kaypro_crt_t *crt, unsigned *col, unsigned *row) {
  const crt_impl_t *impl = (const crt_impl_t *)crt;
  uint16_t start = (uint16_t)(crt_start_addr(impl) & KAYPRO_CRT_VRAM_MASK);
  uint16_t cur = (uint16_t)(crt_cursor_addr(impl) & KAYPRO_CRT_VRAM_MASK);
  unsigned off = (unsigned)((cur - start) & KAYPRO_CRT_VRAM_MASK);
  if (off >= KAYPRO_CRT_CELLS) off = KAYPRO_CRT_CELLS - 1;
  if (col) *col = off % KAYPRO_CRT_COLS;
  if (row) *row = off / KAYPRO_CRT_COLS;
}

bool kaypro_crt_is_dirty(const kaypro_crt_t *crt) {
  return ((const crt_impl_t *)crt)->dirty;
}

void kaypro_crt_clear_dirty(kaypro_crt_t *crt) {
  ((crt_impl_t *)crt)->dirty = false;
}

void kaypro_crt_mark_dirty(kaypro_crt_t *crt) {
  if (!crt) return;
  ((crt_impl_t *)crt)->dirty = true;
}

kaypro_crt_t *kaypro_crt_create(void) {
  crt_impl_t *crt = (crt_impl_t *)calloc(1, sizeof(crt_impl_t));
  if (!crt) return NULL;

  crt_fill_spaces(crt);
  crt->emu.ctx = crt;
  crt->emu.port.port_count = 4;
  crt->emu.port.read = crt_reads;
  crt->emu.port.write = crt_writes;
  crt->emu.reset = crt_reset;
  crt->emu.destroy = crt_destroy;
  return (kaypro_crt_t *)crt;
}
