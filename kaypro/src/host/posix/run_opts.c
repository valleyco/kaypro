#define _POSIX_C_SOURCE 200809L

#include "run_opts.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

void kaypro_run_opts_init(kaypro_run_opts_t *opts) {
  if (!opts) return;
  memset(opts, 0, sizeof(*opts));
  opts->chunk_mcycles = 10000;
  opts->border_color = strdup("green");
}

void kaypro_run_opts_free(kaypro_run_opts_t *opts) {
  if (!opts) return;
  free(opts->rom_path);
  free(opts->disk_a);
  free(opts->disk_b);
  free(opts->images_dir);
  free(opts->config_path);
  free(opts->border_color);
  free(opts->model);
  memset(opts, 0, sizeof(*opts));
}

bool kaypro_exe_dir(char *buf, size_t buflen) {
  if (!buf || buflen < 2) return false;

  char path[PATH_MAX];

#if defined(__APPLE__)
  /* macOS has no /proc/self/exe; resolve the running binary path. */
  uint32_t size = (uint32_t)sizeof(path);
  if (_NSGetExecutablePath(path, &size) != 0) return false;
  char resolved[PATH_MAX];
  if (realpath(path, resolved) != NULL) {
    if (strlen(resolved) + 1 > sizeof(path)) return false;
    memcpy(path, resolved, strlen(resolved) + 1);
  }
#elif defined(__linux__)
  ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (n <= 0) return false;
  path[n] = '\0';
#else
  (void)path;
  return false;
#endif

  char *slash = strrchr(path, '/');
  if (!slash) return false;
  *slash = '\0';

  if (strlen(path) + 1 > buflen) return false;
  memcpy(buf, path, strlen(path) + 1);
  return true;
}

static bool parse_u32(const char *text, unsigned *value) {
  char *end = NULL;
  unsigned long parsed = strtoul(text, &end, 0);
  if (!text || !text[0] || (end && *end) || parsed > 0xffffffffUL) return false;
  *value = (unsigned)parsed;
  return true;
}

static bool is_abs_path(const char *path) { return path && path[0] == '/'; }

static char *dup_resolved_path(const char *path_base, const char *path) {
  if (!path) return NULL;
  if (!path_base || !path_base[0] || is_abs_path(path)) {
    return strdup(path);
  }

  size_t base_len = strlen(path_base);
  while (base_len > 0 && path_base[base_len - 1] == '/') base_len--;

  size_t need = base_len + 1 + strlen(path) + 1;
  char *out = malloc(need);
  if (!out) return NULL;
  snprintf(out, need, "%.*s/%s", (int)base_len, path_base, path);
  return out;
}

static bool set_owned_path(char **slot, const char *path_base, const char *path) {
  char *resolved = dup_resolved_path(path_base, path);
  if (!resolved) {
    fprintf(stderr, "Out of memory\n");
    return false;
  }
  free(*slot);
  *slot = resolved;
  return true;
}

static const char *normalize_opt_name(const char *name) {
  if (!name) return "";
  if (name[0] == '-' && name[1] == '-') return name + 2;
  return name;
}

bool kaypro_run_opts_apply(kaypro_run_opts_t *opts, const char *name,
                           const char *value, const char *path_base,
                           bool allow_config) {
  if (!opts || !name) return false;

  const char *key = normalize_opt_name(name);

  if (strcmp(key, "rom") == 0) {
    if (!value) {
      fprintf(stderr, "Missing value for --rom\n");
      return false;
    }
    return set_owned_path(&opts->rom_path, path_base, value);
  }
  if (strcmp(key, "disk-a") == 0) {
    if (!value) {
      fprintf(stderr, "Missing value for --disk-a\n");
      return false;
    }
    return set_owned_path(&opts->disk_a, path_base, value);
  }
  if (strcmp(key, "disk-b") == 0) {
    if (!value) {
      fprintf(stderr, "Missing value for --disk-b\n");
      return false;
    }
    return set_owned_path(&opts->disk_b, path_base, value);
  }
  if (strcmp(key, "images-dir") == 0) {
    if (!value) {
      fprintf(stderr, "Missing value for --images-dir\n");
      return false;
    }
    return set_owned_path(&opts->images_dir, path_base, value);
  }
  if (strcmp(key, "trace") == 0) {
    if (value) {
      fprintf(stderr, "--trace does not take a value\n");
      return false;
    }
    opts->trace = true;
    return true;
  }
  if (strcmp(key, "mcycles") == 0) {
    if (!value || !parse_u32(value, &opts->limit_mcycles) ||
        opts->limit_mcycles == 0) {
      fprintf(stderr, "Invalid --mcycles value: %s\n", value ? value : "(missing)");
      return false;
    }
    return true;
  }
  if (strcmp(key, "chunk") == 0) {
    if (!value || !parse_u32(value, &opts->chunk_mcycles) ||
        opts->chunk_mcycles == 0) {
      fprintf(stderr, "Invalid --chunk value: %s\n", value ? value : "(missing)");
      return false;
    }
    return true;
  }
  if (strcmp(key, "config") == 0) {
    if (!allow_config) {
      fprintf(stderr, "--config is not allowed in a config file\n");
      return false;
    }
    if (!value) {
      fprintf(stderr, "Missing value for --config\n");
      return false;
    }
    /* CLI config path is relative to the current working directory. */
    return set_owned_path(&opts->config_path, NULL, value);
  }
  if (strcmp(key, "border-color") == 0) {
    if (!value) {
      fprintf(stderr, "Missing value for --border-color\n");
      return false;
    }
    char *copy = strdup(value);
    if (!copy) {
      fprintf(stderr, "Out of memory\n");
      return false;
    }
    free(opts->border_color);
    opts->border_color = copy;
    return true;
  }
  if (strcmp(key, "model") == 0) {
    if (!value) {
      fprintf(stderr, "Missing value for --model\n");
      return false;
    }
    char *copy = strdup(value);
    if (!copy) {
      fprintf(stderr, "Out of memory\n");
      return false;
    }
    free(opts->model);
    opts->model = copy;
    return true;
  }
  if (strcmp(key, "help") == 0 || strcmp(name, "-h") == 0) {
    if (value) {
      fprintf(stderr, "--help does not take a value\n");
      return false;
    }
    opts->help = true;
    return true;
  }

  fprintf(stderr, "Unknown argument: %s\n", name);
  return false;
}

static bool option_takes_value(const char *name) {
  const char *key = normalize_opt_name(name);
  return strcmp(key, "rom") == 0 || strcmp(key, "disk-a") == 0 ||
         strcmp(key, "disk-b") == 0 || strcmp(key, "images-dir") == 0 ||
         strcmp(key, "mcycles") == 0 || strcmp(key, "chunk") == 0 ||
         strcmp(key, "config") == 0 || strcmp(key, "border-color") == 0 ||
         strcmp(key, "model") == 0;
}

typedef struct {
  const char *rom;
  const char *disk_a;
  const char *images_dir;
} model_preset_t;

static bool model_preset_lookup(const char *model, model_preset_t *out) {
  if (!model || !model[0] || strcmp(model, "4") == 0 ||
      strcmp(model, "4-84") == 0) {
    out->rom = "assets/rom/81-478a.rom";
    out->disk_a = "assets/images/dsdd/kaypro1.dsk";
    out->images_dir = "assets/images/dsdd";
    return true;
  }
  if (strcmp(model, "10") == 0) {
    /* Universal board + floppy boot; HDC remains a stub. */
    out->rom = "assets/rom/81-478a.rom";
    out->disk_a = "assets/images/dsdd/kaypro1.dsk";
    out->images_dir = "assets/images/dsdd";
    return true;
  }
  if (strcmp(model, "ii") == 0 || strcmp(model, "2") == 0) {
    /* SSDD geometry + early ROM on Universal ports (experimental). */
    out->rom = "assets/rom/81-149c.rom";
    out->disk_a = "assets/images/ssdd/kii-mbas.dsk";
    out->images_dir = "assets/images/ssdd";
    return true;
  }
  return false;
}

bool kaypro_run_opts_apply_model_defaults(kaypro_run_opts_t *opts,
                                          const char *path_base) {
  if (!opts) return false;

  const char *model = opts->model ? opts->model : "4";
  model_preset_t preset;
  if (!model_preset_lookup(model, &preset)) {
    fprintf(stderr, "Unknown --model '%s' (use 4, 4-84, ii, or 10)\n", model);
    return false;
  }

  if (!opts->rom_path &&
      !set_owned_path(&opts->rom_path, path_base, preset.rom)) {
    return false;
  }
  if (!opts->disk_a &&
      !set_owned_path(&opts->disk_a, path_base, preset.disk_a)) {
    return false;
  }
  if (!opts->images_dir &&
      !set_owned_path(&opts->images_dir, path_base, preset.images_dir)) {
    return false;
  }
  return true;
}

bool kaypro_run_opts_parse_argv(kaypro_run_opts_t *opts, int argc, char **argv,
                                const char *path_base, bool allow_config) {
  if (!opts) return false;

  for (int i = 1; i < argc; i++) {
    const char *name = argv[i];
    const char *value = NULL;
    if (option_takes_value(name)) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing value for %s\n", name);
        return false;
      }
      value = argv[++i];
    }
    if (!kaypro_run_opts_apply(opts, name, value, path_base, allow_config)) {
      return false;
    }
  }
  return true;
}

static char *dir_of_path(const char *path) {
  if (!path) return NULL;
  const char *slash = strrchr(path, '/');
  if (!slash) return strdup(".");
  if (slash == path) return strdup("/");
  size_t len = (size_t)(slash - path);
  char *dir = malloc(len + 1);
  if (!dir) return NULL;
  memcpy(dir, path, len);
  dir[len] = '\0';
  return dir;
}

static bool parse_config_line(char *line, char **name_out, char **value_out) {
  /* Trim leading space. */
  while (*line && isspace((unsigned char)*line)) line++;
  if (*line == '\0' || *line == '#') {
    *name_out = NULL;
    *value_out = NULL;
    return true;
  }

  /* Trim trailing space / CR. */
  char *end = line + strlen(line);
  while (end > line && isspace((unsigned char)end[-1])) end--;
  *end = '\0';

  char *name = line;
  char *value = NULL;

  /* Split on first whitespace (same shape as CLI tokens). */
  char *sp = name;
  while (*sp && !isspace((unsigned char)*sp)) sp++;
  if (*sp) {
    *sp++ = '\0';
    while (*sp && isspace((unsigned char)*sp)) sp++;
    if (*sp) value = sp;
  }

  *name_out = name;
  *value_out = value;
  return true;
}

bool kaypro_run_opts_load_config(kaypro_run_opts_t *opts, const char *path) {
  if (!opts || !path) return false;

  FILE *fp = fopen(path, "r");
  if (!fp) {
    fprintf(stderr, "Failed to open config: %s\n", path);
    return false;
  }

  char *path_base = dir_of_path(path);
  if (!path_base) {
    fprintf(stderr, "Out of memory\n");
    fclose(fp);
    return false;
  }

  char line[4096];
  unsigned lineno = 0;
  bool ok = true;
  while (fgets(line, sizeof(line), fp)) {
    lineno++;
    char *name = NULL;
    char *value = NULL;
    if (!parse_config_line(line, &name, &value)) {
      ok = false;
      break;
    }
    if (!name) continue;

    if (option_takes_value(name)) {
      if (!value) {
        fprintf(stderr, "%s:%u: missing value for %s\n", path, lineno, name);
        ok = false;
        break;
      }
    } else if (value) {
      fprintf(stderr, "%s:%u: %s does not take a value\n", path, lineno, name);
      ok = false;
      break;
    }

    if (!kaypro_run_opts_apply(opts, name, value, path_base, false)) {
      fprintf(stderr, "%s:%u: failed to apply option\n", path, lineno);
      ok = false;
      break;
    }
  }

  free(path_base);
  fclose(fp);
  return ok;
}
