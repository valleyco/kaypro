#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kaypro.h"
#include "kaypro_host.h"
#include "kaypro_internal.h"
#include "fdc1793.h"

#define KAYPRO_SSDD_SIZE 204800u
#define KAYPRO_DSDD_SIZE 409600u

#define PORT_FDC_STATUS 0x10
#define PORT_FDC_COMMAND 0x10
#define PORT_FDC_TRACK 0x11
#define PORT_FDC_SECTOR 0x12
#define PORT_FDC_DATA 0x13
#define PORT_SYSPORT 0x14

static uint8_t read_port(kaypro_t *m, uint16_t port) {
  return kaypro_bus_io_read(m, port);
}

static void write_port(kaypro_t *m, uint16_t port, uint8_t value) {
  kaypro_bus_io_write(m, port, value);
}

static void expect_and_clear_nmi(kaypro_t *m) {
  assert(kaypro_fdc_needs_nmi(m->fdc));
  kaypro_fdc_clear_nmi(m->fdc);
}

static void read_sector_bytes(kaypro_t *m, uint8_t sector, uint8_t *out, unsigned n,
                              const uint8_t *expect, unsigned expect_n) {
  write_port(m, PORT_FDC_TRACK, 0x00);
  write_port(m, PORT_FDC_SECTOR, sector);
  write_port(m, PORT_FDC_COMMAND, 0x80);
  expect_and_clear_nmi(m);

  for (unsigned i = 0; i < 512; i++) {
    uint8_t status = read_port(m, PORT_FDC_STATUS);
    assert((status & 0x01) != 0);
    assert((status & 0x02) != 0);

    uint8_t value = read_port(m, PORT_FDC_DATA);
    if (i < n) out[i] = value;
    if (expect && i < expect_n) assert(value == expect[i]);
    expect_and_clear_nmi(m);
  }
}

int main(void) {
  /* Sector 0: Universal ROM cold-boot record (valid BOOTSYS checksum). */
  static const uint8_t boot_prefix[8] = {0x18, 0xfe, 0x00, 0xd8, 0x00, 0xee, 0x37, 0x00};
  /* Sector 1: first CP/M host sector after the boot record. */
  static const uint8_t sec1_prefix[32] = {
      0xb7, 0xc4, 0xbd, 0xd8, 0x21, 0x08, 0xd8, 0xcd, 0xac, 0xd8, 0xcd, 0xc2,
      0xd9, 0xca, 0xa7, 0xd9, 0xcd, 0xdd, 0xd9, 0xc3, 0x82, 0xdb, 0xcd, 0xdd,
      0xd9, 0xcd, 0x1a, 0xd9, 0x0e, 0x0a, 0x11, 0x06,
  };

  kaypro_t *m = kaypro_create();
  assert(m);
  assert(kaypro_attach_disk(m, 0, "assets/images/dsdd/kaypro1.dsk"));

  /* Drive A select (active low), side 0, motor on, double density, bank1. */
  write_port(m, PORT_SYSPORT, 0xB6);

  write_port(m, PORT_FDC_COMMAND, 0x00);
  expect_and_clear_nmi(m);

  uint8_t status = read_port(m, PORT_FDC_STATUS);
  assert((status & 0x04) != 0);
  assert((status & 0x80) == 0);
  assert(read_port(m, PORT_FDC_TRACK) == 0x00);

  uint8_t boot[128];
  read_sector_bytes(m, 0x00, boot, sizeof(boot), boot_prefix, sizeof(boot_prefix));
  {
    unsigned sum = 0;
    for (unsigned i = 0; i < 126; i++) sum += boot[i];
    unsigned expect = (unsigned)boot[126] | ((unsigned)boot[127] << 8);
    assert((sum & 0xffffu) == expect);
  }

  status = read_port(m, PORT_FDC_STATUS);
  assert((status & 0x01) == 0);
  assert((status & 0x02) == 0);

  read_sector_bytes(m, 0x01, NULL, 0, sec1_prefix, sizeof(sec1_prefix));

  status = read_port(m, PORT_FDC_STATUS);
  assert((status & 0x01) == 0); /* not busy */
  assert((status & 0x02) == 0); /* no DRQ */
  assert((status & 0x80) == 0); /* ready */
  /* Type II completion status has no Track0 bit; sample via Force Interrupt. */
  write_port(m, PORT_FDC_COMMAND, 0xD0);
  expect_and_clear_nmi(m);
  status = read_port(m, PORT_FDC_STATUS);
  assert((status & 0x04) != 0); /* Track0 in Type I status */

  kaypro_destroy(m);

  /* SSDD: side 0 is readable; side 1 must not alias into later tracks. */
  {
    kaypro_t *ss = kaypro_create();
    assert(ss);
    uint8_t *img = (uint8_t *)calloc(1, KAYPRO_SSDD_SIZE);
    assert(img);
    img[0] = 0xA5;
    img[1] = 0x5A;
    /* Track 1 side 0 starts at 10*512 — must not be read as side 1. */
    img[10 * 512] = 0x11;
    assert(kaypro_attach_disk_mem(ss, 0, img, KAYPRO_SSDD_SIZE));

    write_port(ss, PORT_SYSPORT, 0xB6); /* drive A, side 0 */
    write_port(ss, PORT_FDC_COMMAND, 0x00);
    expect_and_clear_nmi(ss);

    uint8_t got[2];
    read_sector_bytes(ss, 0x00, got, sizeof(got), NULL, 0);
    assert(got[0] == 0xA5 && got[1] == 0x5A);

    /* Side 1 select (sysport bit2 clear) on SSDD → not ready. */
    write_port(ss, PORT_SYSPORT, 0xB2);
    write_port(ss, PORT_FDC_TRACK, 0x00);
    write_port(ss, PORT_FDC_SECTOR, 0x00);
    write_port(ss, PORT_FDC_COMMAND, 0x80);
    expect_and_clear_nmi(ss);
    status = read_port(ss, PORT_FDC_STATUS);
    assert((status & 0x80) != 0); /* not ready */
    assert((status & 0x01) == 0); /* not busy */

    uint8_t *bad = (uint8_t *)malloc(100);
    assert(bad);
    assert(!kaypro_attach_disk_mem(ss, 0, bad, 100));
    free(bad);

    kaypro_destroy(ss);
  }

  /* Reject wrong size; accept DSDD size constant used above via file. */
  {
    kaypro_t *ds = kaypro_create();
    assert(ds);
    uint8_t *img = (uint8_t *)calloc(1, KAYPRO_DSDD_SIZE);
    assert(img);
    img[0] = 0x22;
    assert(kaypro_attach_disk_mem(ds, 0, img, KAYPRO_DSDD_SIZE));
    write_port(ds, PORT_SYSPORT, 0xB6);
    write_port(ds, PORT_FDC_COMMAND, 0x00);
    expect_and_clear_nmi(ds);
    uint8_t got[1];
    read_sector_bytes(ds, 0x00, got, 1, NULL, 0);
    assert(got[0] == 0x22);
    kaypro_destroy(ds);
  }

  printf("kaypro fdc smoke: ok\n");
  return 0;
}