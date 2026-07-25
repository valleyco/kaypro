#ifndef KAYPRO_CRT6845_H
#define KAYPRO_CRT6845_H

#include <stdbool.h>
#include <stdint.h>

#include "device.h"

/* Kaypro Universal text mode: 80x25 visible in a 2K circular VRAM. */
#define KAYPRO_CRT_COLS 80
#define KAYPRO_CRT_ROWS 25
#define KAYPRO_CRT_CELLS (KAYPRO_CRT_COLS * KAYPRO_CRT_ROWS)
#define KAYPRO_CRT_VRAM 2048
#define KAYPRO_CRT_VRAM_MASK (KAYPRO_CRT_VRAM - 1)

typedef struct kaypro_crt {
  EmuDevice emu;
} kaypro_crt_t;

/* SY6545-compatible ports at 1Ch-1Fh: status must report ready (bit7). */
kaypro_crt_t *kaypro_crt_create(void);

/* Portable 80x25 character cell view (row-major), linearized from VRAM
 * using CRTC start address (R12/R13). */
const uint8_t *kaypro_crt_cells(const kaypro_crt_t *crt);
unsigned kaypro_crt_cols(void);
unsigned kaypro_crt_rows(void);
/* Visible cursor from CRT R14/R15 relative to start address. */
void kaypro_crt_cursor(const kaypro_crt_t *crt, unsigned *col, unsigned *row);
bool kaypro_crt_is_dirty(const kaypro_crt_t *crt);
void kaypro_crt_clear_dirty(kaypro_crt_t *crt);
void kaypro_crt_mark_dirty(kaypro_crt_t *crt);

#endif /* KAYPRO_CRT6845_H */
