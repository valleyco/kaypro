#ifndef KAYPRO_H
#define KAYPRO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kaypro kaypro_t;

kaypro_t *kaypro_create(void);
void kaypro_destroy(kaypro_t *m);

/* Portable asset loaders. ROM is copied into the machine.
 * Disk: takes ownership of a malloc'd buffer (freed on reattach/destroy). */
bool kaypro_load_rom_bytes(kaypro_t *m, const uint8_t *data, size_t len);
bool kaypro_attach_disk_mem(kaypro_t *m, int drive, uint8_t *data, size_t size);

void kaypro_reset(kaypro_t *m);
void kaypro_step(kaypro_t *m, unsigned m_cycles);
void kaypro_run(kaypro_t *m);

/* Force the next kaypro_step to refresh the host display from CRT cells. */
void kaypro_mark_display_dirty(kaypro_t *m);

void kaypro_set_trace_io(kaypro_t *m, bool enable);

uint8_t kaypro_mem_read(kaypro_t *m, uint16_t addr);
void kaypro_set_bank1(kaypro_t *m, bool bank1);
bool kaypro_halted(const kaypro_t *m);
uint16_t kaypro_pc(const kaypro_t *m);
bool kaypro_fetch_trap_hit(const kaypro_t *m);
uint16_t kaypro_fetch_trap_addr(const kaypro_t *m);
uint8_t kaypro_sysport_state(const kaypro_t *m);

#ifdef __cplusplus
}
#endif

#endif /* KAYPRO_H */
