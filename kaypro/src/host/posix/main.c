#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kaypro.h"
#include "kaypro_host.h"
#include "run_opts.h"

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [--config FILE] [--model NAME] [--rom FILE] [--disk-a FILE] "
          "[--disk-b FILE] [--images-dir DIR] [--border-color COLOR] [--trace] "
          "[--mcycles N] [--chunk N]\n"
          "\n"
          "Options may also be set in a config file (same flags, one per line).\n"
          "Default config: <executable-dir>/kaypro.conf (if present).\n"
          "--config FILE is relative to the current working directory.\n"
          "Command-line options override the config file.\n"
          "\n"
          "--model NAME          Preset defaults for unset paths (4/4-84, ii, 10).\n"
          "                      Paths resolve against the executable directory.\n"
          "--images-dir DIR      Directory of .dsk images for menu Select drive.\n"
          "--border-color COLOR  Frame the CRT (default: green).\n"
          "                      Named: black/red/green/yellow/blue/magenta/cyan/white\n"
          "                      (and bright-* variants), #RRGGBB, or none/off.\n",
          prog);
}

static const char *model_banner(const char *model) {
  if (!model || !model[0] || strcmp(model, "4") == 0 ||
      strcmp(model, "4-84") == 0) {
    return "Kaypro 4/84";
  }
  if (strcmp(model, "10") == 0) return "Kaypro 10";
  if (strcmp(model, "ii") == 0 || strcmp(model, "2") == 0) return "Kaypro II";
  return "Kaypro";
}

int main(int argc, char **argv) {
  kaypro_run_opts_t opts;
  kaypro_run_opts_init(&opts);

  /* First pass: collect --config / --help from the CLI only. */
  kaypro_run_opts_t cli;
  kaypro_run_opts_init(&cli);
  if (!kaypro_run_opts_parse_argv(&cli, argc, argv, NULL, true)) {
    usage(argv[0]);
    kaypro_run_opts_free(&cli);
    kaypro_run_opts_free(&opts);
    return 1;
  }

  if (cli.help) {
    usage(argv[0]);
    kaypro_run_opts_free(&cli);
    kaypro_run_opts_free(&opts);
    return 0;
  }

  /* Load config, then let CLI values win. */
  if (cli.config_path) {
    if (!kaypro_run_opts_load_config(&opts, cli.config_path)) {
      kaypro_run_opts_free(&cli);
      kaypro_run_opts_free(&opts);
      return 1;
    }
  } else {
    char exe_dir[PATH_MAX];
    char default_config[PATH_MAX];
    if (kaypro_exe_dir(exe_dir, sizeof(exe_dir)) &&
        snprintf(default_config, sizeof(default_config), "%s/kaypro.conf",
                 exe_dir) < (int)sizeof(default_config) &&
        access(default_config, R_OK) == 0) {
      if (!kaypro_run_opts_load_config(&opts, default_config)) {
        kaypro_run_opts_free(&cli);
        kaypro_run_opts_free(&opts);
        return 1;
      }
    }
  }

  /* Re-apply CLI over config (paths relative to cwd). */
  if (!kaypro_run_opts_parse_argv(&opts, argc, argv, NULL, true)) {
    usage(argv[0]);
    kaypro_run_opts_free(&cli);
    kaypro_run_opts_free(&opts);
    return 1;
  }
  kaypro_run_opts_free(&cli);

  /* Model presets fill only unset paths (relative to executable dir). */
  {
    char exe_dir[PATH_MAX];
    const char *base = NULL;
    if (kaypro_exe_dir(exe_dir, sizeof(exe_dir))) base = exe_dir;
    if (!kaypro_run_opts_apply_model_defaults(&opts, base)) {
      usage(argv[0]);
      kaypro_run_opts_free(&opts);
      return 1;
    }
  }

  if (!opts.rom_path) {
    usage(argv[0]);
    kaypro_run_opts_free(&opts);
    return 1;
  }

  kaypro_t *m = kaypro_create();
  if (!m) {
    fprintf(stderr, "Failed to create machine\n");
    kaypro_run_opts_free(&opts);
    return 1;
  }

  kaypro_host_posix_cfg_t host_cfg = {
      .border_color = opts.border_color,
      .images_dir = opts.images_dir,
  };
  kaypro_host_posix_install(m, &host_cfg);

  if (!kaypro_load_rom(m, opts.rom_path)) {
    fprintf(stderr, "Failed to load ROM: %s\n", opts.rom_path);
    kaypro_destroy(m);
    kaypro_run_opts_free(&opts);
    return 1;
  }

  if (opts.disk_a && !kaypro_attach_disk(m, 0, opts.disk_a)) {
    fprintf(stderr, "Failed to load disk A: %s\n", opts.disk_a);
    kaypro_destroy(m);
    kaypro_run_opts_free(&opts);
    return 1;
  }
  if (opts.disk_b && !kaypro_attach_disk(m, 1, opts.disk_b)) {
    fprintf(stderr, "Failed to load disk B: %s\n", opts.disk_b);
    kaypro_destroy(m);
    kaypro_run_opts_free(&opts);
    return 1;
  }

  kaypro_set_trace_io(m, opts.trace);
  kaypro_reset(m);

  fprintf(stderr, "%s emulator running (Ctrl-O menu; Ctrl-C to CP/M)\n",
          model_banner(opts.model));

  if (opts.limit_mcycles > 0) {
    unsigned ran = 0;
    while (ran < opts.limit_mcycles && !kaypro_halted(m) &&
           !kaypro_fetch_trap_hit(m) && !kaypro_host_posix_quit_requested()) {
      unsigned slice = opts.chunk_mcycles;
      if (slice > opts.limit_mcycles - ran) slice = opts.limit_mcycles - ran;
      kaypro_step(m, slice);
      ran += slice;
    }
    fprintf(stderr,
            "Stopped after %u m-cycles: pc=%04X halted=%u trap=%u trap_pc=%04X "
            "sysport=%02X\n",
            ran, kaypro_pc(m), kaypro_halted(m), kaypro_fetch_trap_hit(m),
            kaypro_fetch_trap_addr(m), kaypro_sysport_state(m));
  } else {
    while (!kaypro_host_posix_quit_requested()) {
      kaypro_step(m, opts.chunk_mcycles);
      /* Yield so the terminal can process PTY I/O; a tight spin can make
       * interactive sessions look frozen after heavy ANSI screen paints. */
      poll(NULL, 0, 1); /* 1ms */
    }
  }

  kaypro_destroy(m);
  kaypro_run_opts_free(&opts);
  return 0;
}
