#ifndef KAYPRO_RUN_OPTS_H
#define KAYPRO_RUN_OPTS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kaypro_run_opts {
  char *rom_path;
  char *disk_a;
  char *disk_b;
  char *images_dir; /* directory of .dsk images for runtime picker */
  char *config_path; /* set when --config appears on the CLI */
  char *border_color; /* named ANSI color, #RRGGBB, or "none"; default green */
  char *model; /* "4" / "4-84", "ii", or "10"; NULL means "4" */
  bool trace;
  unsigned limit_mcycles; /* 0 = run until quit */
  unsigned chunk_mcycles;
  bool help;
} kaypro_run_opts_t;

void kaypro_run_opts_init(kaypro_run_opts_t *opts);
void kaypro_run_opts_free(kaypro_run_opts_t *opts);

/*
 * Fill unset rom/disk-a/images-dir from --model defaults.
 * path_base is typically the executable directory (assets live beside kaypro_run).
 * Returns false on unknown model or allocation failure.
 */
bool kaypro_run_opts_apply_model_defaults(kaypro_run_opts_t *opts,
                                          const char *path_base);

/* Directory containing the running executable. Returns false on failure. */
bool kaypro_exe_dir(char *buf, size_t buflen);

/*
 * Apply one option (CLI flag name, with or without leading "--").
 * If path_base is non-NULL, relative path values are resolved against it.
 * allow_config: when false, --config is rejected (e.g. inside a config file).
 * Returns true on success.
 */
bool kaypro_run_opts_apply(kaypro_run_opts_t *opts, const char *name,
                           const char *value, const char *path_base,
                           bool allow_config);

/*
 * Parse argv-style tokens (argv[0] is the program name and is skipped).
 * path_base is as for kaypro_run_opts_apply.
 */
bool kaypro_run_opts_parse_argv(kaypro_run_opts_t *opts, int argc, char **argv,
                                const char *path_base, bool allow_config);

/*
 * Load options from a config file. Lines use the same flags as the CLI
 * (e.g. "--rom FILE", "--trace"). Blank lines and # comments are ignored.
 * Relative paths in the file are resolved against the config file's directory.
 */
bool kaypro_run_opts_load_config(kaypro_run_opts_t *opts, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* KAYPRO_RUN_OPTS_H */
