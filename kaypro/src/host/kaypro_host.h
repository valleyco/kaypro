#ifndef KAYPRO_HOST_H
#define KAYPRO_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kaypro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host I/O callbacks. NULL function pointers = silent / no input. */
typedef struct kaypro_host_ops {
  void (*console_write)(void *ctx, const uint8_t *data, size_t len);
  int (*console_poll)(void *ctx); /* -1 if none, else byte for SIO B */
  /* Return true if the frame was painted (so the machine may clear dirty).
   * cursor_col/row are 0-based positions within the text grid. */
  bool (*display_refresh)(void *ctx, const uint8_t *cells, unsigned cols,
                          unsigned rows, unsigned cursor_col,
                          unsigned cursor_row);
  void (*log)(void *ctx, const char *msg);
  void *ctx;
} kaypro_host_ops_t;

void kaypro_set_host(kaypro_t *m, const kaypro_host_ops_t *ops);

/* Path-based loaders (POSIX host). Prefer blob APIs on freestanding targets. */
bool kaypro_load_rom(kaypro_t *m, const char *path);
bool kaypro_attach_disk(kaypro_t *m, int drive, const char *path);

/* Optional POSIX host display chrome. NULL cfg (or NULL fields) = defaults. */
typedef struct kaypro_host_posix_cfg {
  /* Named ANSI color, #RRGGBB, or "none"/"off". Default: green. */
  const char *border_color;
  /* Directory of .dsk images for host menu Select drive. NULL = unavailable. */
  const char *images_dir;
} kaypro_host_posix_cfg_t;

/* Install stdin/stdout/stderr console + log ops for desktop runs. */
void kaypro_host_posix_install(kaypro_t *m, const kaypro_host_posix_cfg_t *cfg);
/* True after the host quit key (Ctrl-\) is pressed. */
bool kaypro_host_posix_quit_requested(void);

#ifdef __cplusplus
}
#endif

#endif /* KAYPRO_HOST_H */
