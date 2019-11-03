#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h> /* isatty */
#include <getopt.h>
#include "Ppmd8.h"

static void *pmalloc(ISzAllocPtr ip, size_t size)
{
    (void) ip;
    return malloc(size);
}

static void pfree(ISzAllocPtr ip, void *addr)
{
    (void) ip;
    free(addr);
}

static ISzAlloc ialloc = { pmalloc, pfree };

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

static int opt_mem = 256;
static int opt_order = 16;
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
    return fflush(stdout) != 0 || ferror(stdin);
}

static int decompress(void)
{
    if (fread(&hdr, sizeof hdr, 1, stdin) != 1)
	return 1;
    if (hdr.magic != MAGIC)
	return 1;
    if (hdr.info >> 12 != 'I' - 'A')
	return 1;

    char fname[0x1FF];
    size_t fnlen = hdr.fnlen & 0x1FF;
    if (fread(fname, fnlen, 1, stdin) != 1)
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
    int c;
    while (1) {
	c = Ppmd8_DecodeSymbol(&ppmd);
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
    return fflush(stdout) != 0 || c != -1 ||
	   !Ppmd8_RangeDec_IsFinishedOK(&ppmd) ||
	   ferror(stdin) || getchar_unlocked() != EOF;
}

int main(int argc, char **argv)
{
    static const struct option longopts[] = {
	{ "stdout",     0, NULL, 'c' },
	{ "to-stdout",  0, NULL, 'c' },
	{ "decompress", 0, NULL, 'd' },
	{ "uncompress", 0, NULL, 'd' },
	{ "force",	0, NULL, 'f' },
	{ "help",       0, NULL, 'h' },
	{ "keep",       0, NULL, 'k' },
	{ "memory",     1, NULL, 'm' },
	{ "order",      1, NULL, 'o' },
	{  NULL,        0, NULL,  0  },
    };
    bool opt_c = 0;
    bool opt_d = 0;
    bool opt_f = 0;
    bool opt_k = 0;
    int c;
    while ((c = getopt_long(argc, argv, "cdfhkm:o:123456789", longopts, NULL)) != -1) {
	switch (c) {
	case 'c':
	    opt_c = 1;
	    break;
	case 'd':
	    opt_d = 1;
	    break;
	case 'f':
	    opt_f = 1;
	    break;
	case 'k':
	    opt_k = 1;
	    break;
	case 'm':
	    opt_mem = atoi(optarg);
	    break;
	case 'o':
	    opt_order = atoi(optarg);
	    break;
	case '1':
	    opt_mem = 1;
	    opt_order = 2;
	    break;
	case '2':
	    opt_mem = 2;
	    opt_order = 3;
	    break;
	case '3':
	    opt_mem = 4;
	    opt_order = 4;
	    break;
	case '4':
	    opt_mem = 8;
	    opt_order = 6;
	    break;
	case '5':
	    opt_mem = 16;
	    opt_order = 8;
	    break;
	case '6':
	    opt_mem = 32;
	    opt_order = 10;
	    break;
	case '7':
	    opt_mem = 64;
	    opt_order = 12;
	    break;
	case '8':
	    opt_mem = 128;
	    opt_order = 14;
	    break;
	case '9':
	    opt_mem = 256;
	    opt_order = 16;
	    break;
	default:
	    goto usage;
	}
    }
    argc -= optind;
    argv += optind;
    if ( opt_order < 2 || opt_order > 16 || opt_mem < 1 || opt_mem > 256 ) {
	fputs("ppmd-mini: invalid parameters\n", stderr);
	goto usage;
    }
    if (argc > 1) {
	fputs("ppmd-mini: too many arguments\n", stderr);
usage:	fputs("Usage: ppmd-mini [args] [FILE]\n\n", stderr);
	fputs("Arguments\n", stderr);
	fputs("  -1 .. -9          compression level (default 9)\n", stderr);
	fputs("  -c, --stdout      write to stdout\n", stderr);
	fputs("  -d, --decompress  decompression\n", stderr);
	fputs("  -f, --force       force overwrite\n", stderr);
	fputs("  -k, --keep        keep input file\n", stderr);
	fputs("  -m, --memory      memory usage, 1..32\n", stderr);
	fputs("  -o, --order       order, 2..16\n", stderr);
	fputs("  -h, --help        this page\n", stderr);
	return 1;
    }
    char *fname = argc ? argv[0] : NULL;
    if (fname && strcmp(fname, "-") == 0)
	fname = NULL;
    if (fname == NULL)
	opt_c = 1;
    if (fname == NULL && opt_d && isatty(0)) {
	fprintf(stderr, "ppmd-mini: compressed data cannot be read from a terminal\n");
	return 1;
    }
    if (opt_c && !opt_d && isatty(1)) {
	fprintf(stderr, "ppmd-mini: compressed data cannot be written to a terminal\nFor help, type: ppmd-mini -h\n");
	return 1;
    }
    if (fname) {
	stdin = freopen(fname, "r", stdin);
	if (!stdin) {
	    fprintf(stderr, "ppmd-mini: cannot open %s\n", fname);
	    return 1;
	}
    }
    if (opt_d && !opt_c) {
	char *dot = strrchr(fname, '.');
	if (dot == NULL || dot[1] != 'p' || strchr(dot, '/')) {
	    fprintf(stderr, "ppmd-mini: unknown suffix: %s\n", fname);
	    return 1;
	}
	*dot = '\0';
	if( access( fname, F_OK ) != -1 && !opt_f ) {
	    fprintf(stderr, "ppmd-mini: %s file already exists\n", fname);
	    return 1;
	} 
	stdout = freopen(fname, "w", stdout);
	if (!stdout) {
	    fprintf(stderr, "ppmd-mini: cannot open %s\n", fname);
	    return 1;
	}
	*dot = '.';
    }
    if (!opt_d && !opt_c) {
	size_t len = strlen(fname);
	char outname[len + 6];
	memcpy(outname, fname, len);
	memcpy(outname + len, ".ppmd", 6);
	if( access( outname, F_OK ) != -1 && !opt_f ) {
	    fprintf(stderr, "ppmd-mini: %s already exists\n", outname);
	    return 1;
	} 
	stdout = freopen(outname, "w", stdout);
	if (!stdout) {
	    fprintf(stderr, "ppmd-mini: cannot open %s\n", outname);
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
