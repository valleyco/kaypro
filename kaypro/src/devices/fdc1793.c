#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fdc1793.h"

/* WD1793 type I/II status bits used by Universal ROM. */
#define FDC_BUSY 0x01
#define FDC_INDEX 0x02
#define FDC_DRQ 0x02
#define FDC_TRACK0 0x04
#define FDC_NOT_READY 0x80

/* Kaypro SSDD 40×1×10×512; DSDD 40×2×10×512 (side-1 IDs 10–19). */
#define KAYPRO_TRACKS 40
#define KAYPRO_SPT 10
#define KAYPRO_SSIZE 512
#define KAYPRO_SSDD_SIZE ((size_t)KAYPRO_TRACKS * 1 * KAYPRO_SPT * KAYPRO_SSIZE)
#define KAYPRO_DSDD_SIZE ((size_t)KAYPRO_TRACKS * 2 * KAYPRO_SPT * KAYPRO_SSIZE)

typedef struct {
  uint8_t *data;
  size_t size;
  unsigned sides; /* 1 = SSDD, 2 = DSDD */
} fdc_disk_t;

typedef struct {
  EmuDevice emu;
  fdc_disk_t drives[2];
  uint8_t sysport;
  int selected_drive;
  bool double_density;
  bool motors_on;
  uint8_t track;
  uint8_t sector;
  uint8_t side;
  uint8_t command;
  uint8_t status;
  bool drq;
  bool busy;
  bool needs_nmi;
  bool type1_status; /* Type I bits (index/track0) valid after type-I / FI */
  uint8_t sector_buf[512];
  unsigned sector_idx;
  unsigned transfer_len; /* bytes in current DRQ transfer (sector or ID) */
  bool write_mode;
  unsigned index_phase;
} fdc_impl_t;

static fdc_impl_t *fdc_impl(const kaypro_fdc_t *fdc) {
  return (fdc_impl_t *)fdc;
}

static bool fdc_drive_ready(const fdc_impl_t *fdc) {
  const fdc_disk_t *disk = &fdc->drives[fdc->selected_drive];
  return disk->data != NULL;
}

static bool fdc_sector_offset(const fdc_impl_t *fdc, size_t *out) {
  const fdc_disk_t *disk = &fdc->drives[fdc->selected_drive];
  unsigned sides = disk->sides;
  unsigned track = fdc->track;
  unsigned side = fdc->side;
  unsigned sector = fdc->sector;
  unsigned phys;

  if (sides == 0 || side >= sides) return false;

  /* Side 0: IDs 0-9; side 1: IDs 10-19 (GETBUF dset2 adds 10). */
  if (sides > 1 && sector >= 10)
    phys = sector - 10;
  else
    phys = sector;
  if (phys >= KAYPRO_SPT) phys = KAYPRO_SPT - 1;
  if (track >= KAYPRO_TRACKS) track = KAYPRO_TRACKS - 1;
  *out = ((size_t)track * sides + side) * KAYPRO_SPT * KAYPRO_SSIZE +
         phys * KAYPRO_SSIZE;
  return true;
}

static void fdc_finish_type1(fdc_impl_t *fdc) {
  fdc->busy = false;
  fdc->drq = false;
  fdc->type1_status = true;
  fdc->status = 0;
  if (fdc->track == 0) fdc->status |= FDC_TRACK0;
  if (!fdc_drive_ready(fdc)) fdc->status |= FDC_NOT_READY;
  fdc->needs_nmi = true;
}

static void fdc_begin_read_address(fdc_impl_t *fdc) {
  /* WD1793 Read Address: 6-byte ID field. */
  const fdc_disk_t *disk = &fdc->drives[fdc->selected_drive];
  uint8_t id_sec = fdc->sector;
  if (disk->sides > 1 && fdc->side == 1 && id_sec < 10)
    id_sec = (uint8_t)(id_sec + 10);
  fdc->sector_buf[0] = fdc->track;
  fdc->sector_buf[1] = fdc->side;
  fdc->sector_buf[2] = id_sec;
  fdc->sector_buf[3] = 2; /* 512-byte sectors */
  fdc->sector_buf[4] = 0;
  fdc->sector_buf[5] = 0;
  fdc->sector_idx = 0;
  fdc->transfer_len = 6;
  fdc->write_mode = false;
  fdc->type1_status = false;
  fdc->busy = true;
  fdc->drq = true;
  fdc->status = FDC_BUSY | FDC_DRQ;
  fdc->needs_nmi = true;
}

static void fdc_begin_read_sector(fdc_impl_t *fdc) {
  fdc_disk_t *disk = &fdc->drives[fdc->selected_drive];
  size_t off = 0;
  fdc->type1_status = false;
  if (!disk->data || !fdc_sector_offset(fdc, &off) || off + 512 > disk->size) {
    fdc->busy = false;
    fdc->drq = false;
    fdc->status = FDC_NOT_READY;
    fdc->needs_nmi = true;
    return;
  }
  memcpy(fdc->sector_buf, disk->data + off, 512);
  fdc->sector_idx = 0;
  fdc->transfer_len = 512;
  fdc->write_mode = false;
  fdc->busy = true;
  fdc->drq = true;
  fdc->status = FDC_BUSY | FDC_DRQ;
  fdc->needs_nmi = true;
}

static void fdc_begin_write_sector(fdc_impl_t *fdc) {
  size_t off = 0;
  fdc->type1_status = false;
  if (!fdc_drive_ready(fdc) || !fdc_sector_offset(fdc, &off)) {
    fdc->busy = false;
    fdc->drq = false;
    fdc->status = FDC_NOT_READY;
    fdc->needs_nmi = true;
    return;
  }
  fdc->sector_idx = 0;
  fdc->transfer_len = 512;
  fdc->write_mode = true;
  fdc->busy = true;
  fdc->drq = true;
  fdc->status = FDC_BUSY | FDC_DRQ;
  fdc->needs_nmi = true;
}

static void fdc_finish_write_sector(fdc_impl_t *fdc) {
  fdc_disk_t *disk = &fdc->drives[fdc->selected_drive];
  size_t off = 0;
  if (disk->data && fdc_sector_offset(fdc, &off) && off + 512 <= disk->size) {
    memcpy(disk->data + off, fdc->sector_buf, 512);
  }
  fdc->drq = false;
  fdc->busy = false;
  fdc->type1_status = false;
  fdc->status = 0;
  if (!fdc_drive_ready(fdc)) fdc->status |= FDC_NOT_READY;
  fdc->needs_nmi = true;
}

static void fdc_finish_read_sector(fdc_impl_t *fdc) {
  fdc->drq = false;
  fdc->busy = false;
  fdc->type1_status = false;
  fdc->status = 0;
  if (!fdc_drive_ready(fdc)) fdc->status |= FDC_NOT_READY;
  /* WD1793: Read Address copies track ID into the sector register. */
  if ((fdc->command & 0xF0) == 0xC0) {
    fdc->sector = fdc->sector_buf[0];
  }
  fdc->needs_nmi = true;
}

static int fdc_read_status(void *dev, int port) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev;
  (void)port;
  uint8_t st = fdc->status;
  if (fdc->busy) st |= FDC_BUSY;
  if (fdc->drq) st |= FDC_DRQ;
  if (!fdc->busy) {
    if (!fdc_drive_ready(fdc)) st |= FDC_NOT_READY;
    /* Index / Track0 are Type I bits. fstat uses Force Interrupt to sample
     * them; do not expose them after Type II/III completions or Read Address
     * looks like a nonzero error (TRACK0|INDEX == 06h). */
    if (fdc->type1_status) {
      fdc->index_phase++;
      if ((fdc->index_phase & 0x20) != 0) st |= FDC_INDEX;
      if (fdc->track == 0) st |= FDC_TRACK0;
    }
  }
  return st;
}

static int fdc_read_track(void *dev, int port) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev;
  (void)port;
  return fdc->track;
}

static int fdc_read_sector(void *dev, int port) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev;
  (void)port;
  return fdc->sector;
}

static int fdc_read_data(void *dev, int port) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev;
  (void)port;
  if (!fdc->drq || fdc->write_mode || fdc->sector_idx >= fdc->transfer_len) return 0;
  uint8_t v = fdc->sector_buf[fdc->sector_idx++];
  if (fdc->sector_idx >= fdc->transfer_len) {
    fdc_finish_read_sector(fdc);
  } else {
    /* ROM HALTs for an NMI on every DRQ byte. */
    fdc->needs_nmi = true;
    fdc->status = FDC_BUSY | FDC_DRQ;
  }
  return v;
}

static void fdc_write_cmd(void *dev, int port, int value) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev;
  (void)port;
  fdc->command = (uint8_t)value;
  fdc->drq = false;
  fdc->busy = true;
  fdc->status = FDC_BUSY;

  switch (fdc->command & 0xF0) {
  case 0x00: /* Restore */
    fdc->track = 0;
    fdc_finish_type1(fdc);
    break;
  case 0x10: /* Seek */
    fdc_finish_type1(fdc);
    break;
  case 0x20: /* Step */
  case 0x30:
    if (fdc->command & 0x04) {
      if (fdc->track > 0) fdc->track--;
    } else if (fdc->track < 39) {
      fdc->track++;
    }
    fdc_finish_type1(fdc);
    break;
  case 0x40: /* Step in */
  case 0x50:
    if (fdc->track < 39) fdc->track++;
    fdc_finish_type1(fdc);
    break;
  case 0x60: /* Step out */
  case 0x70:
    if (fdc->track > 0) fdc->track--;
    fdc_finish_type1(fdc);
    break;
  case 0x80: /* Read sector */
  case 0x90:
    fdc_begin_read_sector(fdc);
    break;
  case 0xA0: /* Write sector */
  case 0xB0:
    fdc_begin_write_sector(fdc);
    break;
  case 0xC0: /* Read address */
    fdc_begin_read_address(fdc);
    break;
  case 0xD0: /* Force interrupt — presents Type I status (fstat). */
    fdc->busy = false;
    fdc->drq = false;
    fdc->type1_status = true;
    fdc->status = 0;
    if (fdc->track == 0) fdc->status |= FDC_TRACK0;
    if (!fdc_drive_ready(fdc)) fdc->status |= FDC_NOT_READY;
    fdc->needs_nmi = true;
    break;
  default:
    fdc_finish_type1(fdc);
    break;
  }
}

static void fdc_write_track(void *dev, int port, int value) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev;
  (void)port;
  fdc->track = (uint8_t)value;
}

static void fdc_write_sector(void *dev, int port, int value) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev;
  (void)port;
  fdc->sector = (uint8_t)value;
}

static void fdc_write_data(void *dev, int port, int value) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev;
  (void)port;
  if (!fdc->write_mode || !fdc->drq || fdc->sector_idx >= fdc->transfer_len) return;
  fdc->sector_buf[fdc->sector_idx++] = (uint8_t)value;
  if (fdc->sector_idx >= fdc->transfer_len) {
    fdc_finish_write_sector(fdc);
  } else {
    fdc->needs_nmi = true;
    fdc->status = FDC_BUSY | FDC_DRQ;
  }
}

void fdc1793_set_sysport(kaypro_fdc_t *fdc, uint8_t sysport) {
  fdc_impl_t *impl = fdc_impl(fdc);
  impl->sysport = sysport;
  /* Drive selects are active-low on bits 0-1 (see FLPYIO psel). */
  uint8_t sel = sysport & 0x03;
  if (sel == 0x02)
    impl->selected_drive = 0;
  else if (sel == 0x01)
    impl->selected_drive = 1;
  impl->double_density = (sysport & 0x20) != 0;
  impl->motors_on = (sysport & 0x10) != 0; /* bit4 = motor/high speed */
  /* Side select: bit2 set = side 0, clear = side 1 (SELECT.MAC). */
  impl->side = (sysport & 0x04) ? 0 : 1;
}

bool kaypro_fdc_attach_mem(kaypro_fdc_t *fdc, int drive, uint8_t *data, size_t size) {
  unsigned sides;
  if (drive < 0 || drive > 1 || !data || size == 0) return false;
  if (size == KAYPRO_SSDD_SIZE) {
    sides = 1;
  } else if (size == KAYPRO_DSDD_SIZE) {
    sides = 2;
  } else {
    fprintf(stderr,
            "Unsupported disk image size %zu (need %zu SSDD or %zu DSDD)\n",
            size, KAYPRO_SSDD_SIZE, KAYPRO_DSDD_SIZE);
    return false;
  }
  fdc_disk_t *disk = &fdc_impl(fdc)->drives[drive];
  free(disk->data);
  disk->data = data;
  disk->size = size;
  disk->sides = sides;
  return true;
}

bool kaypro_fdc_needs_nmi(const kaypro_fdc_t *fdc) {
  return fdc_impl(fdc)->needs_nmi;
}

void kaypro_fdc_clear_nmi(kaypro_fdc_t *fdc) { fdc_impl(fdc)->needs_nmi = false; }

static void fdc_reset(EmuDevice *dev) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev->ctx;
  fdc->track = fdc->sector = fdc->side = 0;
  fdc->status = 0;
  fdc->drq = fdc->busy = fdc->needs_nmi = false;
  fdc->type1_status = true;
  fdc->sector_idx = 0;
  fdc->transfer_len = 512;
  fdc->write_mode = false;
  fdc->index_phase = 0;
  fdc->selected_drive = 0;
}

static void fdc_poll(EmuDevice *dev) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev->ctx;
  fdc->index_phase++;
}

static void fdc_destroy(EmuDevice *dev) {
  fdc_impl_t *fdc = (fdc_impl_t *)dev->ctx;
  for (int i = 0; i < 2; i++) free(fdc->drives[i].data);
  free(dev->ctx);
}

static int (*fdc_reads[])(void *, int) = {
    fdc_read_status, fdc_read_track, fdc_read_sector, fdc_read_data};
static void (*fdc_writes[])(void *, int, int) = {
    fdc_write_cmd, fdc_write_track, fdc_write_sector, fdc_write_data};

kaypro_fdc_t *kaypro_fdc_create(void) {
  fdc_impl_t *fdc = (fdc_impl_t *)calloc(1, sizeof(fdc_impl_t));
  if (!fdc) return NULL;

  fdc->emu.ctx = fdc;
  fdc->emu.port.port_count = 4;
  fdc->emu.port.read = fdc_reads;
  fdc->emu.port.write = fdc_writes;
  fdc->emu.reset = fdc_reset;
  fdc->emu.poll = fdc_poll;
  fdc->emu.destroy = fdc_destroy;
  return (kaypro_fdc_t *)fdc;
}
