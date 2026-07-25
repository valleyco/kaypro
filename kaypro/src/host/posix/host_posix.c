#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "kaypro_host.h"

#define POSIX_DISPLAY_MIN_MS 50
#define POSIX_QUIT_BYTE 0x1C /* Ctrl-\ */
#define POSIX_MENU_BYTE 0x0F /* Ctrl-O */
#define UI_COLS 80u
#define UI_ROWS 25u
#define UI_PAGE_SIZE 18u
#define UI_ID_BUF 8
#define STATUS_HINT "Ctrl-O menu   Ctrl-C to CP/M"

typedef enum {
  UI_IDLE = 0,
  UI_MENU,
  UI_PICK_DRIVE,
  UI_PICK_IMAGE,
  UI_ERROR,
} ui_mode_t;

typedef struct posix_border {
  bool enabled;
  bool truecolor;
  uint8_t r, g, b;
  int ansi_fg; /* 30-37 or 90-97 */
} posix_border_t;

typedef struct disk_image {
  char *name; /* basename for display */
  char *path; /* full path for attach */
} disk_image_t;

static bool display_cleared;
static bool termios_saved;
static bool quit_requested;
static bool prev_was_cr;
static struct termios termios_original;
static struct timeval display_last_paint;
static posix_border_t border = {.enabled = true, .truecolor = false, .ansi_fg = 32};

static kaypro_t *host_machine;
static char *images_dir; /* owned copy, or NULL */
static ui_mode_t ui_mode;
static uint8_t ui_cells[UI_COLS * UI_ROWS];
static int ui_drive; /* 0=A, 1=B */
static unsigned ui_page;
static char ui_id_buf[UI_ID_BUF];
static size_t ui_id_len;
static char ui_status[UI_COLS + 1];
static disk_image_t *ui_images;
static size_t ui_image_count;

static void posix_restore_terminal(void) {
  if (termios_saved) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios_original);
    termios_saved = false;
  }
  fputs("\033[0m\033[?25h", stdout);
  fflush(stdout);
}

/* stdin/stdout/stderr often share one open-file description on a PTY. Setting
 * O_NONBLOCK on stdin therefore makes stdout nonblocking too; full-frame ANSI
 * paints then get EAGAIN, stdio errors out, and the screen freezes while the
 * guest still runs. Use termios VMIN/VTIME (or poll) instead. */
static void posix_ensure_stdout_blocking(void) {
  int flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
  if (flags >= 0 && (flags & O_NONBLOCK)) {
    fcntl(STDOUT_FILENO, F_SETFL, flags & ~O_NONBLOCK);
  }
  clearerr(stdout);
}

static void posix_setup_terminal(void) {
  if (!isatty(STDIN_FILENO)) return;
  if (tcgetattr(STDIN_FILENO, &termios_original) != 0) return;
  termios_saved = true;
  atexit(posix_restore_terminal);

  struct termios raw = termios_original;
  raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG);
  raw.c_iflag &= (tcflag_t)~IXON;
  /* Non-blocking-style read without O_NONBLOCK: return immediately if empty. */
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  posix_ensure_stdout_blocking();
}

static void posix_console_write(void *ctx, const uint8_t *data, size_t len) {
  (void)ctx;
  if (!data || len == 0) return;
  fwrite(data, 1, len, stdout);
  fflush(stdout);
}

static void posix_log(void *ctx, const char *msg) {
  (void)ctx;
  if (msg) fprintf(stderr, "%s\n", msg);
}

static long posix_elapsed_ms(const struct timeval *then, const struct timeval *now) {
  return (now->tv_sec - then->tv_sec) * 1000L +
         (now->tv_usec - then->tv_usec) / 1000L;
}

static uint8_t posix_glyph(uint8_t c) {
  if (c >= 0x20 && c < 0x7F) return c;
  return (uint8_t)' ';
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool parse_hex_byte(const char *p, uint8_t *out) {
  int hi = hex_nibble(p[0]);
  int lo = hex_nibble(p[1]);
  if (hi < 0 || lo < 0) return false;
  *out = (uint8_t)((hi << 4) | lo);
  return true;
}

static bool posix_parse_border_color(const char *color, posix_border_t *out) {
  posix_border_t b = {.enabled = false, .truecolor = false, .ansi_fg = 32};
  if (!color || !color[0] || strcmp(color, "none") == 0 ||
      strcmp(color, "off") == 0) {
    *out = b;
    return true;
  }

  b.enabled = true;

  /* #RRGGBB or RRGGBB */
  const char *hex = color;
  if (hex[0] == '#') hex++;
  if (strlen(hex) == 6 && parse_hex_byte(hex, &b.r) &&
      parse_hex_byte(hex + 2, &b.g) && parse_hex_byte(hex + 4, &b.b)) {
    b.truecolor = true;
    *out = b;
    return true;
  }

  static const struct {
    const char *name;
    int ansi;
  } named[] = {
      {"black", 30},         {"red", 31},
      {"green", 32},         {"yellow", 33},
      {"blue", 34},          {"magenta", 35},
      {"cyan", 36},          {"white", 37},
      {"bright-black", 90},  {"bright-red", 91},
      {"bright-green", 92},  {"bright-yellow", 93},
      {"bright-blue", 94},   {"bright-magenta", 95},
      {"bright-cyan", 96},   {"bright-white", 97},
  };

  char lower[32];
  size_t n = strlen(color);
  if (n >= sizeof(lower)) return false;
  for (size_t i = 0; i < n; i++) {
    lower[i] = (char)tolower((unsigned char)color[i]);
  }
  lower[n] = '\0';

  for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
    if (strcmp(lower, named[i].name) == 0) {
      b.ansi_fg = named[i].ansi;
      *out = b;
      return true;
    }
  }
  return false;
}

static void posix_set_border_sgr(void) {
  if (!border.enabled) return;
  if (border.truecolor) {
    fprintf(stdout, "\033[38;2;%u;%u;%um", border.r, border.g, border.b);
  } else {
    fprintf(stdout, "\033[%dm", border.ansi_fg);
  }
}

static void posix_draw_border(unsigned cols, unsigned rows) {
  if (!border.enabled) return;

  posix_set_border_sgr();

  /* Top edge. */
  fputs("\033[1;1H", stdout);
  fputs("┌", stdout);
  for (unsigned c = 0; c < cols; c++) fputs("─", stdout);
  fputs("┐", stdout);

  /* Sides. */
  for (unsigned r = 0; r < rows; r++) {
    fprintf(stdout, "\033[%u;1H│", r + 2);
    fprintf(stdout, "\033[%u;%uH│", r + 2, cols + 2);
  }

  /* Bottom edge. */
  fprintf(stdout, "\033[%u;1H", rows + 2);
  fputs("└", stdout);
  for (unsigned c = 0; c < cols; c++) fputs("─", stdout);
  fputs("┘", stdout);

  fputs("\033[0m", stdout);
}

/* Hint under the CRT frame while the guest display is showing. */
static void posix_draw_status_line(unsigned cols, unsigned rows, bool show) {
  unsigned term_row = border.enabled ? (rows + 3u) : (rows + 1u);
  unsigned width = border.enabled ? (cols + 2u) : cols;
  fprintf(stdout, "\033[%u;1H\033[0m", term_row);
  if (show) {
    fputs(STATUS_HINT, stdout);
    size_t n = strlen(STATUS_HINT);
    for (size_t i = n; i < width; i++) fputc(' ', stdout);
  } else {
    for (unsigned i = 0; i < width; i++) fputc(' ', stdout);
  }
}

/* Shared ANSI dump for CRT cells or the host overlay buffer. */
static bool posix_paint_cells(const uint8_t *cells, unsigned cols, unsigned rows,
                              unsigned cursor_col, unsigned cursor_row,
                              bool throttle) {
  if (!cells || cols == 0 || rows == 0) return true;

  struct timeval now;
  gettimeofday(&now, NULL);
  if (throttle && display_cleared &&
      posix_elapsed_ms(&display_last_paint, &now) < POSIX_DISPLAY_MIN_MS) {
    return false; /* keep dirty so the latest frame is painted later */
  }

  posix_ensure_stdout_blocking();
  clearerr(stdout);

  if (!display_cleared) {
    fputs("\033[2J", stdout);
    display_cleared = true;
  }

  unsigned row_off = border.enabled ? 1u : 0u;
  unsigned col_off = border.enabled ? 1u : 0u;

  if (border.enabled) {
    posix_draw_border(cols, rows);
  }

  /* Paint rows with absolute CUP so we do not scroll past the grid. */
  for (unsigned r = 0; r < rows; r++) {
    fprintf(stdout, "\033[%u;%uH", r + 1 + row_off, 1 + col_off);
    for (unsigned c = 0; c < cols; c++) {
      uint8_t ch = posix_glyph(cells[r * cols + c]);
      fputc((int)ch, stdout);
    }
  }

  /* Guest CRT: show hint. Overlay: clear the line so stale text does not linger. */
  posix_draw_status_line(cols, rows, ui_mode == UI_IDLE);

  if (cursor_col >= cols) cursor_col = cols - 1;
  if (cursor_row >= rows) cursor_row = rows - 1;
  fprintf(stdout, "\033[%u;%uH\033[?25h", cursor_row + 1 + row_off,
          cursor_col + 1 + col_off);
  if (fflush(stdout) != 0 || ferror(stdout)) {
    clearerr(stdout);
    return false; /* retry later; do not clear dirty */
  }
  display_last_paint = now;
  return true;
}

static void ui_clear_cells(void) {
  memset(ui_cells, ' ', sizeof(ui_cells));
}

static void ui_put_str(unsigned row, unsigned col, const char *s) {
  if (!s || row >= UI_ROWS || col >= UI_COLS) return;
  for (unsigned c = col; c < UI_COLS && *s; c++, s++) {
    ui_cells[row * UI_COLS + c] = (uint8_t)*s;
  }
}

static void ui_free_images(void) {
  if (!ui_images) {
    ui_image_count = 0;
    return;
  }
  for (size_t i = 0; i < ui_image_count; i++) {
    free(ui_images[i].name);
    free(ui_images[i].path);
  }
  free(ui_images);
  ui_images = NULL;
  ui_image_count = 0;
}

static int ui_cmp_images(const void *a, const void *b) {
  const disk_image_t *ia = (const disk_image_t *)a;
  const disk_image_t *ib = (const disk_image_t *)b;
  return strcmp(ia->name, ib->name);
}

static bool ui_ends_with_dsk(const char *name) {
  size_t n = strlen(name);
  if (n < 4) return false;
  const char *ext = name + n - 4;
  return (ext[0] == '.') &&
         (tolower((unsigned char)ext[1]) == 'd') &&
         (tolower((unsigned char)ext[2]) == 's') &&
         (tolower((unsigned char)ext[3]) == 'k');
}

static bool ui_load_images(void) {
  ui_free_images();
  if (!images_dir || !images_dir[0]) return false;

  DIR *dir = opendir(images_dir);
  if (!dir) return false;

  size_t cap = 0;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    if (!ui_ends_with_dsk(ent->d_name)) continue;

    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", images_dir, ent->d_name) >=
        (int)sizeof(path)) {
      continue;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

    if (ui_image_count == cap) {
      size_t ncap = cap ? cap * 2 : 32;
      disk_image_t *ni = realloc(ui_images, ncap * sizeof(*ni));
      if (!ni) {
        closedir(dir);
        ui_free_images();
        return false;
      }
      ui_images = ni;
      cap = ncap;
    }

    char *name = strdup(ent->d_name);
    char *full = strdup(path);
    if (!name || !full) {
      free(name);
      free(full);
      closedir(dir);
      ui_free_images();
      return false;
    }
    ui_images[ui_image_count].name = name;
    ui_images[ui_image_count].path = full;
    ui_image_count++;
  }
  closedir(dir);

  if (ui_image_count > 1) {
    qsort(ui_images, ui_image_count, sizeof(ui_images[0]), ui_cmp_images);
  }
  return true;
}

static unsigned ui_page_count(void) {
  if (ui_image_count == 0) return 1;
  return (unsigned)((ui_image_count + UI_PAGE_SIZE - 1) / UI_PAGE_SIZE);
}

static void ui_paint_overlay(void) {
  (void)posix_paint_cells(ui_cells, UI_COLS, UI_ROWS, 0, UI_ROWS - 1, false);
}

static void ui_render_menu(void) {
  ui_clear_cells();
  ui_put_str(0, 0, "Host menu");
  ui_put_str(2, 0, "  O  Select drive");
  ui_put_str(3, 0, "  X  Exit");
  ui_put_str(5, 0, "Esc  Close menu");
  if (ui_status[0]) ui_put_str(7, 0, ui_status);
  ui_paint_overlay();
}

static void ui_render_drive(void) {
  ui_clear_cells();
  ui_put_str(0, 0, "Select drive");
  ui_put_str(2, 0, "Drive?  A / B");
  ui_put_str(4, 0, "Esc = cancel");
  if (ui_status[0]) ui_put_str(6, 0, ui_status);
  ui_paint_overlay();
}

static void ui_render_error(const char *msg) {
  ui_clear_cells();
  ui_put_str(0, 0, "Host menu");
  ui_put_str(2, 0, msg ? msg : "Error");
  ui_put_str(4, 0, "Press any key to close");
  ui_paint_overlay();
}

static void ui_render_images(void) {
  ui_clear_cells();

  char line[UI_COLS + 1];
  char drive = (char)('A' + ui_drive);
  unsigned pages = ui_page_count();
  if (ui_page >= pages) ui_page = pages - 1;

  snprintf(line, sizeof(line), "Disk %c  -  page %u/%u  (%zu images)", drive,
           ui_page + 1, pages, ui_image_count);
  ui_put_str(0, 0, line);

  size_t start = (size_t)ui_page * UI_PAGE_SIZE;
  for (unsigned i = 0; i < UI_PAGE_SIZE; i++) {
    size_t idx = start + i;
    if (idx >= ui_image_count) break;
    snprintf(line, sizeof(line), "%3zu  %s", idx + 1, ui_images[idx].name);
    ui_put_str(2 + i, 0, line);
  }

  if (ui_id_len > 0) {
    snprintf(line, sizeof(line), "id: %s_", ui_id_buf);
  } else {
    snprintf(line, sizeof(line), "id: _");
  }
  ui_put_str(UI_ROWS - 3, 0, line);

  if (ui_status[0]) {
    ui_put_str(UI_ROWS - 2, 0, ui_status);
  }
  ui_put_str(UI_ROWS - 1, 0, "Type id+Enter   n/p page   Esc cancel");
  ui_paint_overlay();
}

static void ui_close(void) {
  ui_mode = UI_IDLE;
  ui_id_len = 0;
  ui_id_buf[0] = '\0';
  ui_status[0] = '\0';
  ui_free_images();
  /* Overlay may have painted over a clean CRT; force a restore paint. */
  if (host_machine) kaypro_mark_display_dirty(host_machine);
}

static void ui_open(void) {
  ui_id_len = 0;
  ui_id_buf[0] = '\0';
  ui_status[0] = '\0';
  ui_page = 0;
  ui_drive = 0;
  ui_mode = UI_MENU;
  ui_render_menu();
}

static void ui_start_select_drive(void) {
  ui_id_len = 0;
  ui_id_buf[0] = '\0';
  ui_status[0] = '\0';
  ui_page = 0;
  ui_drive = 0;

  if (!images_dir || !images_dir[0]) {
    ui_mode = UI_ERROR;
    ui_render_error("No --images-dir configured");
    return;
  }

  ui_mode = UI_PICK_DRIVE;
  ui_render_drive();
}

static void ui_enter_images(void) {
  ui_id_len = 0;
  ui_id_buf[0] = '\0';
  ui_status[0] = '\0';
  ui_page = 0;

  if (!ui_load_images()) {
    ui_mode = UI_ERROR;
    ui_render_error("Cannot read --images-dir");
    return;
  }
  if (ui_image_count == 0) {
    ui_mode = UI_ERROR;
    ui_render_error("No .dsk images in --images-dir");
    return;
  }

  ui_mode = UI_PICK_IMAGE;
  ui_render_images();
}

static void ui_try_attach(void) {
  if (ui_id_len == 0) {
    snprintf(ui_status, sizeof(ui_status), "Enter an image id");
    ui_render_images();
    return;
  }

  char *end = NULL;
  unsigned long id = strtoul(ui_id_buf, &end, 10);
  if (!ui_id_buf[0] || (end && *end) || id < 1 || id > ui_image_count) {
    ui_id_len = 0;
    ui_id_buf[0] = '\0';
    snprintf(ui_status, sizeof(ui_status), "Invalid id (1-%zu)", ui_image_count);
    ui_render_images();
    return;
  }

  const disk_image_t *img = &ui_images[id - 1];
  if (!host_machine || !kaypro_attach_disk(host_machine, ui_drive, img->path)) {
    ui_id_len = 0;
    ui_id_buf[0] = '\0';
    snprintf(ui_status, sizeof(ui_status), "Failed to load %s", img->name);
    ui_render_images();
    return;
  }

  ui_close();
}

static void ui_handle_key(unsigned char byte) {
  if (byte == 0x1B) { /* Esc */
    ui_close();
    return;
  }

  if (ui_mode == UI_ERROR) {
    ui_close();
    return;
  }

  if (ui_mode == UI_MENU) {
    char c = (char)tolower((unsigned char)byte);
    if (c == 'o') {
      ui_start_select_drive();
    } else if (c == 'x') {
      quit_requested = true;
      ui_close();
    } else {
      snprintf(ui_status, sizeof(ui_status), "Type O, X, or Esc");
      ui_render_menu();
    }
    return;
  }

  if (ui_mode == UI_PICK_DRIVE) {
    char c = (char)tolower((unsigned char)byte);
    if (c == 'a') {
      ui_drive = 0;
      ui_enter_images();
    } else if (c == 'b') {
      ui_drive = 1;
      ui_enter_images();
    } else {
      snprintf(ui_status, sizeof(ui_status), "Type A or B");
      ui_render_drive();
    }
    return;
  }

  if (ui_mode != UI_PICK_IMAGE) return;

  char c = (char)tolower((unsigned char)byte);
  if (c == 'n') {
    if (ui_page + 1 < ui_page_count()) ui_page++;
    ui_status[0] = '\0';
    ui_render_images();
    return;
  }
  if (c == 'p') {
    if (ui_page > 0) ui_page--;
    ui_status[0] = '\0';
    ui_render_images();
    return;
  }
  if (byte == '\r' || byte == '\n') {
    ui_try_attach();
    return;
  }
  if (byte == 0x7F || byte == 0x08) { /* Backspace */
    if (ui_id_len > 0) {
      ui_id_buf[--ui_id_len] = '\0';
      ui_status[0] = '\0';
      ui_render_images();
    }
    return;
  }
  if (byte >= '0' && byte <= '9') {
    if (ui_id_len + 1 < sizeof(ui_id_buf)) {
      ui_id_buf[ui_id_len++] = (char)byte;
      ui_id_buf[ui_id_len] = '\0';
      ui_status[0] = '\0';
      ui_render_images();
    }
    return;
  }
}

static int posix_console_poll(void *ctx) {
  (void)ctx;

  struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
  int pr = poll(&pfd, 1, 0);
  if (pr <= 0) return -1;
  if (!(pfd.revents & (POLLIN | POLLHUP))) return -1;

  unsigned char byte = 0;
  ssize_t n = read(STDIN_FILENO, &byte, 1);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    return -1;
  }
  if (n == 0) return -1;

  if (byte == POSIX_QUIT_BYTE) {
    quit_requested = true;
    return -1;
  }

  if (ui_mode != UI_IDLE) {
    /* Normalize CR/LF for Enter in the picker. */
    if (byte == '\n') {
      if (prev_was_cr) {
        prev_was_cr = false;
        return -1;
      }
      byte = '\r';
    }
    prev_was_cr = (byte == '\r');
    ui_handle_key(byte);
    return -1;
  }

  if (byte == POSIX_MENU_BYTE) {
    ui_open();
    return -1;
  }

  byte = (unsigned char)(byte & 0x7F);
  /* CP/M / MBASIC terminate lines on CR. Many terminals send LF or CRLF. */
  if (byte == '\n') {
    if (prev_was_cr) {
      prev_was_cr = false;
      return -1; /* drop LF of CRLF */
    }
    prev_was_cr = false;
    return '\r';
  }
  prev_was_cr = (byte == '\r');
  return (int)byte;
}

static bool posix_display_refresh(void *ctx, const uint8_t *cells, unsigned cols,
                                  unsigned rows, unsigned cursor_col,
                                  unsigned cursor_row) {
  (void)ctx;

  if (ui_mode != UI_IDLE) {
    /* Keep CRT dirty so the guest screen is restored when the picker closes. */
    (void)posix_paint_cells(ui_cells, UI_COLS, UI_ROWS, 0, UI_ROWS - 1, true);
    return false;
  }

  return posix_paint_cells(cells, cols, rows, cursor_col, cursor_row, true);
}

bool kaypro_host_posix_quit_requested(void) { return quit_requested; }

void kaypro_host_posix_install(kaypro_t *m, const kaypro_host_posix_cfg_t *cfg) {
  quit_requested = false;
  prev_was_cr = false;
  display_cleared = false;
  memset(&display_last_paint, 0, sizeof(display_last_paint));
  host_machine = m;
  ui_mode = UI_IDLE;
  ui_id_len = 0;
  ui_id_buf[0] = '\0';
  ui_status[0] = '\0';
  ui_free_images();

  free(images_dir);
  images_dir = NULL;
  if (cfg && cfg->images_dir && cfg->images_dir[0]) {
    images_dir = strdup(cfg->images_dir);
  }

  const char *color = (cfg && cfg->border_color) ? cfg->border_color : "green";
  if (!posix_parse_border_color(color, &border)) {
    fprintf(stderr,
            "Invalid --border-color '%s' (use named color, #RRGGBB, or none)\n",
            color);
    posix_parse_border_color("green", &border);
  }

  posix_setup_terminal();
  posix_ensure_stdout_blocking();

  kaypro_host_ops_t ops = {
      .console_write = posix_console_write,
      .console_poll = posix_console_poll,
      .display_refresh = posix_display_refresh,
      .log = posix_log,
      .ctx = NULL,
  };
  kaypro_set_host(m, &ops);
}

static uint8_t *posix_read_file(const char *path, size_t *out_size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long sz = ftell(f);
  if (sz <= 0) {
    fclose(f);
    return NULL;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }
  uint8_t *buf = (uint8_t *)malloc((size_t)sz);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  *out_size = (size_t)sz;
  return buf;
}

bool kaypro_load_rom(kaypro_t *m, const char *path) {
  size_t size = 0;
  uint8_t *data = posix_read_file(path, &size);
  if (!data) return false;
  bool ok = kaypro_load_rom_bytes(m, data, size);
  free(data);
  return ok;
}

bool kaypro_attach_disk(kaypro_t *m, int drive, const char *path) {
  size_t size = 0;
  uint8_t *data = posix_read_file(path, &size);
  if (!data) return false;
  /* FDC takes ownership of data. */
  if (!kaypro_attach_disk_mem(m, drive, data, size)) {
    free(data);
    return false;
  }
  return true;
}
