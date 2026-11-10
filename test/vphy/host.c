// SPDX-License-Identifier: GPL-2.0-only
/*
 * The user side of the harness: the chip behind a pipe, the trace, the
 * options, and the allocator the driver's kmalloc lands on.
 *
 *     b43phy [--trace FILE] -- CHIP COMMAND...
 *
 * CHIP COMMAND is vchipd.py with the board's options. The trace is one line
 * per access, in the op classes of b43-tools' interpreter/README.md: PHY and
 * radio accesses folded from their address and data ports, the rest as REG
 * (eight digits for a 32-bit access), WAIT for the time the driver lets pass
 * and a # line at each checkpoint. FN.IN and FN.OUT name the PHY function
 * entered and left, from the executable's own symbol table.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MMIO_RADIO_CONTROL	0x3F6
#define MMIO_RADIO_DATA_LOW	0x3FA
#define MMIO_PHY_CONTROL	0x3FC
#define MMIO_PHY_DATA		0x3FE

int drive(void);

static FILE *to_chip, *from_chip, *trace;
static unsigned int phy_addr, radio_addr;

static void die(const char *what)
{
	fprintf(stderr, "b43phy: %s\n", what);
	exit(2);
}

static void spawn(char **command)
{
	int down[2], up[2];

	if (pipe(down) || pipe(up))
		die("pipe");
	switch (fork()) {
	case -1:
		die("fork");
	case 0:
		dup2(down[0], 0);
		dup2(up[1], 1);
		close(down[1]);
		close(up[0]);
		execvp(command[0], command);
		die("cannot run the chip");
	}
	close(down[0]);
	close(up[1]);
	to_chip = fdopen(down[1], "w");
	from_chip = fdopen(up[0], "r");
}

static char *ask(const char *fmt, ...)
{
	static char reply[1 << 16];
	va_list args;

	va_start(args, fmt);
	vfprintf(to_chip, fmt, args);
	va_end(args);
	fflush(to_chip);
	if (!fgets(reply, sizeof(reply), from_chip))
		die("the chip went away");
	return reply;
}

static void traced(const char *class, const char *key, unsigned int at, unsigned int val, int bits)
{
	if (trace)
		fprintf(trace, "cpu0 %-8s %s=0x%04x val=0x%0*x\n", class, key, at, bits / 4, val);
}

unsigned int chip_read(unsigned short off, int bits)
{
	unsigned int val = strtoul(ask("R %x %x\n", off, bits) + 2, NULL, 16);

	if (off == MMIO_PHY_DATA)
		traced("PHY.RD", "addr", phy_addr, val, bits);
	else if (off == MMIO_RADIO_DATA_LOW)
		traced("RAD.RD", "addr", radio_addr, val, bits);
	else
		traced("REG.RD", "off", off, val, bits);
	return val;
}

void chip_write(unsigned short off, unsigned int val, int bits)
{
	ask("W %x %x %x\n", off, val, bits);
	if (off == MMIO_PHY_CONTROL)
		phy_addr = val;
	else if (off == MMIO_RADIO_CONTROL)
		radio_addr = val;
	else if (off == MMIO_PHY_DATA)
		traced("PHY.WR", "addr", phy_addr, val, bits);
	else if (off == MMIO_RADIO_DATA_LOW)
		traced("RAD.WR", "addr", radio_addr, val, bits);
	else
		traced("REG.WR", "off", off, val, bits);
}

void chip_time(unsigned long us)
{
	ask("T %lx\n", us);
	if (trace)
		fprintf(trace, "cpu0 WAIT     us=0x%lx\n", us);
}

void chip_state(const char *checkpoint)
{
	printf("== %s\n", checkpoint);
	if (trace)
		fprintf(trace, "# %s\n", checkpoint);
}

struct sym {
	unsigned long at;
	char name[64];
};

static struct sym *syms;
static size_t nsyms;

static int sym_cmp(const void *a, const void *b)
{
	unsigned long x = ((const struct sym *)a)->at, y = ((const struct sym *)b)->at;

	return x < y ? -1 : x > y;
}

static void load_syms(void)
{
	char exe[4096], line[4352], type;
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	size_t cap = 0;
	struct sym s;
	FILE *nm;

	if (n < 0)
		die("cannot find the executable");
	exe[n] = 0;
	snprintf(line, sizeof(line), "nm --defined-only '%s'", exe);
	nm = popen(line, "r");
	if (!nm)
		die("cannot run nm");
	while (fgets(line, sizeof(line), nm)) {
		if (sscanf(line, "%lx %c %63s", &s.at, &type, s.name) != 3 ||
		    (type != 't' && type != 'T'))
			continue;
		if (nsyms == cap) {
			cap = cap ? 2 * cap : 1024;
			syms = realloc(syms, cap * sizeof(*syms));
			if (!syms)
				die("out of memory");
		}
		syms[nsyms++] = s;
	}
	pclose(nm);
	qsort(syms, nsyms, sizeof(*syms), sym_cmp);
}

static const char *sym_name(void *fn)
{
	struct sym key = { .at = (unsigned long)fn };
	struct sym *s = bsearch(&key, syms, nsyms, sizeof(*syms), sym_cmp);

	return s ? s->name : "?";
}

void __cyg_profile_func_enter(void *fn, void *site)
{
	if (trace)
		fprintf(trace, "cpu0 FN.IN    fn=%s\n", sym_name(fn));
}

void __cyg_profile_func_exit(void *fn, void *site)
{
	if (trace)
		fprintf(trace, "cpu0 FN.OUT   fn=%s\n", sym_name(fn));
}

void vsay(const char *prefix, const char *fmt, va_list args)
{
	fputs(prefix, stdout);
	vprintf(fmt, args);
}

void say(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

long option(const char *name, long otherwise)
{
	const char *val = getenv(name);

	return val ? strtol(val, NULL, 0) : otherwise;
}

int step_asked(const char *name)
{
	const char *steps = getenv("B43_STEPS");
	const char *at = steps ? strstr(steps, name) : NULL;
	size_t n = strlen(name);

	return at && (at == steps || at[-1] == ',') && (at[n] == ',' || !at[n]);
}

void *kmalloc_caches[32][32];
unsigned long random_kmalloc_seed;

void *__kmalloc_cache_noprof(void *cache, unsigned int flags, size_t size)
{
	return calloc(1, size);
}

void *__kmalloc_noprof(size_t size, unsigned int flags)
{
	return calloc(1, size);
}

void kfree(const void *p)
{
	free((void *)p);
}

int main(int argc, char **argv)
{
	int at = 1, err;

	if (at + 1 < argc && !strcmp(argv[at], "--trace")) {
		trace = fopen(argv[at + 1], "w");
		if (!trace)
			die("cannot write the trace");
		load_syms();
		at += 2;
	}
	if (at + 1 >= argc || strcmp(argv[at], "--"))
		die("usage: b43phy [--trace FILE] -- CHIP COMMAND...");
	spawn(argv + at + 1);
	err = drive();
	ask("Q\n");
	return err ? 1 : 0;
}
