/* SPDX-License-Identifier: GPL-2.0 */
/*
 * What the driver side (flows.c, built against the kernel headers) and the
 * user side (trace.c, built against libc) share: plain C types only.
 */
#ifndef HT_TRACE_H
#define HT_TRACE_H

unsigned short ht_mmio_read16(unsigned short offset);
void ht_mmio_write16(unsigned short offset, unsigned short value);
unsigned int ht_mmio_read32(unsigned short offset);
void ht_mmio_write32(unsigned short offset, unsigned int value);
unsigned int ht_wrap_read32(unsigned short offset);
void ht_wrap_write32(unsigned short offset, unsigned int value);
void ht_log(const char *what);
/* An nvram variable of the board as a number, or dflt if it has none. */
long ht_nvram(const char *name, long dflt);
/* An environment variable as a number, or dflt. */
long ht_env(const char *name, long dflt);
/* A call into b43's core, which the harness does not compile. */
void ht_core(const char *fn);

/* Driver side: run one flow; nonzero if the name is unknown. */
int ht_run_flow(const char *name);

#endif
