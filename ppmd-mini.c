#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h> /* isatty */
#include <getopt.h>
#include "Ppmd8.h"

static void *pmalloc(void *p, size_t size)
{
    (void) p;
    return malloc(size);
}

static void pfree(void *p, void *addr)
{
    (void) p;
    free(addr);
}

ISzAlloc ialloc = { pmalloc, pfree };

struct CharWriter {
    /* Inherits from IByteOut */
    void (*Write)(void *p, Byte b);
    FILE *fp;
};

struct CharReader {
    /* Inherits from IByteIn */
    Byte (*Read)(void *p);
    FILE *fp;
    bool eof;
};

static void Write(void *p, Byte b)
{
    struct CharWriter *cw = p;
    putc_unlocked(b, cw->fp);
}

static Byte Read(void *p)
{
    struct CharReader *cr = p;
    if (cr->eof)
	return 0;
    int c = getc_unlocked(cr->fp);
    if (c == EOF) {
	cr->eof = 1;
	return 0;
    }
    return c;
}

static int opt_mem = 8;
static int opt_order = 6;
static int opt_restore = 0;

struct header {
    unsigned magic, attr;
    unsigned short info, fnlen;
    unsigned short date, time;
} hdr = {
#define MAGIC 0x84ACAF8F
    MAGIC, /* FILE_ATTRIBUTE_NORMAL */ 0x80,
    0, 1, 0, 0,
};

static int compress(void)
{
    hdr.info = (opt_order - 1) | ((opt_mem - 1) << 4) | (('I' - 'A') << 12);
    fwrite(&hdr, sizeof hdr, 1, stdout);
    putchar('a');

    struct CharWriter cw = { Write, stdout };
    CPpmd8 ppmd = { .Stream.Out = (IByteOut *) &cw };
    Ppmd8_Construct(&ppmd);
    Ppmd8_Alloc(&ppmd, opt_mem << 20, &ialloc);
    Ppmd8_RangeEnc_Init(&ppmd);
    Ppmd8_Init(&ppmd, opt_order, 0);

    unsigned char buf[BUFSIZ];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, stdin))) {
	for (size_t i = 0; i < n; i++)
	    Ppmd8_EncodeSymbol(&ppmd, buf[i]);
    }
    Ppmd8_EncodeSymbol(&ppmd, -1); /* EndMark */
    Ppmd8_RangeEnc_FlushData(&ppmd);
    if (ferror(stdin) || fflush(stdout) != 0)
	return 1;
    return 0;
}

static int decompress(void)
{
    if (fread(&hdr, sizeof hdr, 1, stdin) != 1)
	return 1;
    if (hdr.magic != MAGIC)
	return 1;
    if (hdr.info >> 12 != 'I' - 'A')
	return 1;
    if (fseek(stdin, hdr.fnlen & 0x1FF, SEEK_CUR) != 0)
	return 1;

    opt_restore = hdr.fnlen >> 14;
    opt_order = (hdr.info & 0xf) + 1;
    opt_mem = ((hdr.info >> 4) & 0xff) + 1;

    struct CharReader cr = { Read, stdin, 0 };
    CPpmd8 ppmd = { .Stream.In = (IByteIn *) &cr };
    Ppmd8_Construct(&ppmd);
    Ppmd8_Alloc(&ppmd, opt_mem << 20, &ialloc);
    Ppmd8_RangeDec_Init(&ppmd);
    Ppmd8_Init(&ppmd, opt_order, opt_restore);

    unsigned char buf[BUFSIZ];
    size_t n = 0;
    while (1) {
	int c = Ppmd8_DecodeSymbol(&ppmd);
	if (cr.eof || c < 0)
	    break;
	buf[n++] = c;
	if (n == sizeof buf) {
	    fwrite(buf, 1, sizeof buf, stdout);
	    n = 0;
	}
    }
    if (n)
	fwrite(buf, 1, n, stdout);
    if (ferror(stdin) || !feof(stdin) || fflush(stdout) != 0)
	return 1;
    return 0;
}

int main(int argc, char **argv)
{
    static const struct option longopts[] = {
	{ "decompress", 0, NULL, 'd' },
	{ "uncompress", 0, NULL, 'd' },
	{ "keep",       0, NULL, 'k' },
	{ "stdout",     0, NULL, 'c' },
	{ "to-stdout",  0, NULL, 'c' },
	{ "memory",     1, NULL, 'm' },
	{ "order",      1, NULL, 'o' },
	{ "help",       0, NULL, 'h' },
	{  NULL,        0, NULL,  0  },
    };
    bool opt_d = 0;
    bool opt_k = 0;
    bool opt_c = 0;
    int c;
    while ((c = getopt_long(argc, argv, "dkcm:o:36h", longopts, NULL)) != -1) {
	switch (c) {
	case 'd':
	    opt_d = 1;
	    break;
	case 'k':
	    opt_k = 1;
	    break;
	case 'c':
	    opt_c = 1;
	    break;
	case 'm':
	    opt_mem = atoi(optarg);
	    break;
	case 'o':
	    opt_order = atoi(optarg);
	    break;
	case '3':
	    opt_mem = 1;
	    opt_order = 5;
	    break;
	case '6':
	    opt_mem = 8;
	    opt_order = 6;
	    break;
	default:
	    goto usage;
	}
    }
    argc -= optind;
    argv += optind;
    if (argc > 1) {
	fputs("ppmid-mini: too many arguments\n", stderr);
usage:	fputs("Usage: ppmid-mini [-d] [-k] [-c] [FILE]\n", stderr);
	return 1;
    }
    char *fname = argc ? argv[0] : NULL;
    if (fname && strcmp(fname, "-") == 0)
	fname = NULL;
    if (fname == NULL)
	opt_c = 1;
    if (fname == NULL && opt_d && isatty(0)) {
	fprintf(stderr, "ppmid-mini: compressed data cannot be read from a terminal\n");
	return 1;
    }
    if (opt_c && !opt_d && isatty(1)) {
	fprintf(stderr, "ppmid-mini: compressed data cannot be written to a terminal\n");
	return 1;
    }
    if (fname) {
	stdin = freopen(fname, "r", stdin);
	if (!stdin) {
	    fprintf(stderr, "ppmid-mini: cannot open %s\n", fname);
	    return 1;
	}
    }
    if (opt_d && !opt_c) {
	char *dot = strrchr(fname, '.');
	if (dot == NULL || dot[1] != 'p' || strchr(dot, '/')) {
	    fprintf(stderr, "ppmid-mini: unknown suffix: %s\n", fname);
	    return 1;
	}
	*dot = '\0';
	stdout = freopen(fname, "w", stdout);
	if (!stdout) {
	    fprintf(stderr, "ppmid-mini: cannot open %s\n", fname);
	    return 1;
	}
	*dot = '.';
    }
    if (!opt_d && !opt_c) {
	size_t len = strlen(fname);
	char outname[len + 6];
	memcpy(outname, fname, len);
	memcpy(outname + len, ".ppmd", 6);
	stdout = freopen(outname, "w", stdout);
	if (!stdout) {
	    fprintf(stderr, "ppmid-mini: cannot open %s\n", outname);
	    return 1;
	}
    }
    int rc = opt_d ? decompress() : compress();
    if (rc == 0 && !opt_k && !opt_c) {
	fclose(stdin);
	remove(fname);
    }
    return rc;
}

/* ex: set ts=8 sts=4 sw=4 noet: */
