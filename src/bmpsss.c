#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>

#include "util.h"

#define BMP_HEADER_SIZE      14
#define DIB_HEADER_SIZE      40
#define PALETTE_SIZE         1024
#define PIXEL_ARRAY_OFFSET   (BMP_HEADER_SIZE + DIB_HEADER_SIZE + PALETTE_SIZE)
#define UNUSED2_OFFSET       8
#define WIDTH_OFFSET         18
#define HEIGHT_OFFSET        22
#define BITS_PER_PIXEL       8
#define PRIME                251
#define DEFAULT_SEED         691
#define RIGHTMOST_BIT_ON(x)  ((x) |= 0x01)
#define RIGHTMOST_BIT_OFF(x) ((x) &= 0xFE)
#define DIR_MAX              (PATH_MAX - NAME_MAX)

typedef struct {
    uint8_t  id[2];   /* magic number to identify the BMP format */
    uint32_t size;    /* size of the BMP file in bytes */
    uint16_t unused1; /* key (seed) */
    uint16_t unused2; /* shadow number */
    uint32_t offset;  /* starting address of the pixel array (bitmap data) */
} BMPheader;

/* 40 bytes BITMAPINFOHEADER */
typedef struct {
    uint32_t size;           /* the size of this header (40 bytes) */
    uint32_t width;          /* the bitmap width in pixels */
    int32_t  height;         /* the bitmap height in pixels; can be negative */
    uint16_t nplanes;        /* number of color planes used; Must set to 1 */
    uint16_t depth;          /* bpp number. Usually: 1, 4, 8, 16, 24 or 32 */
    uint32_t compression;    /* compression method used */
    uint32_t pixelarraysize; /* size of the raw bitmap (pixel) data */
    uint32_t hres;           /* horizontal resolution (pixel per meter) */
    uint32_t vres;           /* vertical resolution (pixel per meter) */
    uint32_t ncolors;        /* colors in the palette. 0 means 2^n */
    uint32_t nimpcolors;     /* important colors used, usually ignored */
} DIBheader;

typedef struct {
    BMPheader bmpheader;             /* 14 bytes BMP starting header */
    DIBheader dibheader;             /* 40 bytes DIB header */
    uint8_t   palette[PALETTE_SIZE]; /* color palette; mandatory for depth <= 8 */
    uint8_t   *imgpixels;            /* array of bytes representing each pixel */
} Bitmap;

typedef bool (*fn)(FILE *, uint16_t, uint32_t);
/* prototypes */
static long     randint(long max);
static void     swap(uint8_t *s, uint8_t *t);
static int      countfiles(const char *dirname);
static void     usage(void);
static uint32_t get32bitsfromheader(FILE *fp, int offset);
static uint32_t bmpfilewidth(FILE *fp);
static uint32_t bmpfileheight(FILE *fp);
static uint32_t bmpimagesize(const Bitmap *bp);
static void     initpalette(uint8_t palette[static PALETTE_SIZE]);
static Bitmap   *newbitmap(uint32_t width, int32_t height, uint16_t seed);
static void     freebitmap(Bitmap *bp);
static Bitmap   *newbitmaphelper(uint32_t width, int32_t height, uint16_t seed, uint16_t shadnum, uint32_t pixelarraysize);
static void     changeheaderendianness(BMPheader *h);
static void     changedibendianness(DIBheader *h);
static void     readbmpheader(Bitmap *bp, FILE *fp);
static void     writebmpheader(const Bitmap *bp, FILE *fp);
static void     readdibheader(Bitmap *bp, FILE *fp);
static void     writedibheader(const Bitmap *bp, FILE *fp);
static Bitmap   *bmpfromfile(const char *filename);
static bool     isvalidbmpsize(FILE *fp, uint16_t k, uint32_t secretsize);
static bool     kdivisiblesize(FILE *fp, uint16_t k);
static void     bmptofile(const Bitmap *bp, const char *filename);
static void     findclosestpair(uint32_t x, uint32_t *width, int32_t *height);
static Bitmap   *newshadow(uint32_t width, int32_t height, uint16_t seed, uint16_t shadownumber);
static Bitmap   **formshadows(const Bitmap *bp, uint16_t k, uint16_t n, uint16_t seed);
static void     findcoefficients(int **mat, uint16_t k);
static Bitmap   *revealsecret(Bitmap **shadows, uint32_t width, int32_t height, uint16_t k);
static void     hideshadow(Bitmap *bp, const Bitmap *shadow);
static Bitmap   *retrieveshadow(const Bitmap *bp, uint32_t width, int32_t height, uint16_t k);
static bool     isbmp(FILE *fp);
static bool     isvalidshadow(FILE *fp, uint16_t k, uint32_t secretsize);
static bool     isvalidbmp(FILE *fp, uint16_t k, uint32_t ignoredparameter);
static char     **getvalidfilenames(const char *dir, uint16_t k, uint16_t n, fn isvalid, uint32_t size);
static char     **getbmpfilenames(const char *dir, uint16_t k, uint16_t n, uint32_t size);
static char     **getshadowfilenames(const char *dir, uint16_t k, uint32_t size);
static void     distributeimage(const char *dir, const char *imgpath, uint16_t k, uint16_t n, uint16_t seed);
static void     recoverimage(const char *dir, const char *filename, uint32_t width, int32_t height, uint16_t k);
static uint32_t calculatepixelarraysize(uint32_t width, int32_t height);
static void     truncategrayscale(Bitmap *bp);
static void     permutepixels(Bitmap *bp, uint16_t seed);
static void     unpermutepixels(Bitmap *bp, uint16_t seed);
static uint8_t  generatepixel(const uint8_t *coeff, uint16_t degree, uint16_t value);

/* globals */
static const char    *argv0;           /* program name for usage() */
static const uint8_t modinv[PRIME] = { /* modular multiplicative inverse */
    0, 1, 126, 84, 63, 201, 42, 36, 157, 28, 226, 137, 21, 58, 18, 67, 204,
    192, 14, 185, 113, 12, 194, 131, 136, 241, 29, 93, 9, 26, 159, 81, 102,
    213, 96, 208, 7, 95, 218, 103, 182, 49, 6, 216, 97, 106, 191, 235, 68, 41,
    246, 64, 140, 90, 172, 178, 130, 229, 13, 234, 205, 107, 166, 4, 51, 112,
    232, 15, 48, 211, 104, 99, 129, 196, 173, 164, 109, 163, 177, 197, 91, 31,
    150, 124, 3, 189, 108, 176, 174, 110, 53, 80, 221, 27, 243, 37, 34, 44,
    146, 71, 123, 169, 32, 39, 70, 153, 45, 61, 86, 76, 89, 199, 65, 20, 240,
    227, 132, 118, 117, 135, 228, 195, 179, 100, 83, 249, 2, 168, 151, 72, 56,
    23, 116, 134, 133, 119, 24, 11, 231, 186, 52, 162, 175, 165, 190, 206, 98,
    181, 212, 219, 82, 128, 180, 105, 207, 217, 214, 8, 224, 30, 171, 198, 141,
    77, 75, 143, 62, 248, 127, 101, 220, 160, 54, 74, 88, 142, 87, 78, 55, 122,
    152, 147, 40, 203, 236, 19, 139, 200, 247, 85, 144, 46, 17, 238, 22, 121,
    73, 79, 161, 111, 187, 5, 210, 183, 16, 60, 145, 154, 35, 245, 202, 69,
    148, 33, 156, 244, 43, 155, 38, 149, 170, 92, 225, 242, 158, 222, 10, 115,
    120, 57, 239, 138, 66, 237, 59, 47, 184, 233, 193, 230, 114, 25, 223, 94,
    215, 209, 50, 188, 167, 125, 250
};

int
countfiles(const char *dirname) {
    struct dirent *d;
    int filecount = 0;
    DIR *dp = xopendir(dirname);

    while ((d = readdir(dp))) {
        if (d->d_type == DT_REG) /* If the entry is a regular file */
            filecount++;
    }
    xclosedir(dp);

    return filecount;
}

long
randint(long int max) {
    double normalizedrand = rand()/(RAND_MAX + 1.0);

    return (long) (normalizedrand * (max + 1)); /* returns num in [0, max] */
}

void
swap(uint8_t *s, uint8_t *t) {
    uint8_t temp;

    temp = *s;
    *s = *t;
    *t = temp;
}

void
usage(void) {
    die("usage: %s -(d|r) --secret image -k number -w width -h height -s seed"
            "[-n number] [--dir directory]\n", argv0);
}

/* Calculates needed pixelarraysize, accounting for padding.
 * See: https://en.wikipedia.org/wiki/BMP_file_format#Pixel_storage */
inline uint32_t
calculatepixelarraysize(uint32_t width, int32_t height) {
    return ((BITS_PER_PIXEL * width + 31)/32) * 4 * height;
}

uint32_t
get32bitsfromheader(FILE *fp, int offset) {
    uint32_t value;
    long pos = ftell(fp);

    xfseek(fp, offset, SEEK_SET);
    xfread(&value, sizeof(value), 1, fp);
    xfseek(fp, pos, SEEK_SET);

    return value;
}

uint32_t
bmpfilewidth(FILE *fp) {
    return get32bitsfromheader(fp, WIDTH_OFFSET);
}

uint32_t
bmpfileheight(FILE *fp) {
    return get32bitsfromheader(fp, HEIGHT_OFFSET);
}

/* initialize palette with default 8-bit greyscale values */
void
initpalette(uint8_t palette[static PALETTE_SIZE]) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t j = i * 4;

        palette[j++] = i;
        palette[j++] = i;
        palette[j++] = i;
        palette[j]   = 0;
    }
}

/* If no seed is needed, just pass 0 */
Bitmap *
newbitmap(uint32_t width, int32_t height, uint16_t seed) {
    uint32_t pixelarraysize = calculatepixelarraysize(width, height);
    return newbitmaphelper(width, height, seed, 0, pixelarraysize);
}

/* Helper function to build a BMP, used by newbitmap() and newshadow() */
Bitmap*
newbitmaphelper(uint32_t width, int32_t height, uint16_t seed, uint16_t shadnum, uint32_t pixelarraysize) {
    Bitmap *bmp = xmalloc(sizeof(*bmp));

    bmp->imgpixels = xmalloc(pixelarraysize);
    initpalette(bmp->palette);

    bmp->bmpheader = (BMPheader)
        { .id[0]   = 'B'
        , .id[1]   = 'M'
        , .size    = PIXEL_ARRAY_OFFSET + pixelarraysize
        , .unused1 = seed
        , .unused2 = shadnum
        , .offset  = PIXEL_ARRAY_OFFSET
        };

    bmp->dibheader = (DIBheader)
        { .size           = DIB_HEADER_SIZE
        , .width          = width
        , .height         = height
        , .nplanes        = 1
        , .depth          = BITS_PER_PIXEL
        , .compression    = 0
        , .pixelarraysize = pixelarraysize
        , .hres           = 0
        , .vres           = 0
        , .ncolors        = 0
        , .nimpcolors     = 0
        };

    return bmp;

}

void
freebitmap(Bitmap *bp) {
    free(bp->imgpixels);
    free(bp);
}

void
changeheaderendianness(BMPheader *h) {
    uint32swap(&h->size);
    uint16swap(&h->unused1);
    uint16swap(&h->unused2);
    uint32swap(&h->offset);
}

void
changedibendianness(DIBheader *h) {
    uint32swap(&h->size);
    uint32swap(&h->width);
    int32swap(&h->height);
    uint16swap(&h->nplanes);
    uint16swap(&h->depth);
    uint32swap(&h->compression);
    uint32swap(&h->pixelarraysize);
    uint32swap(&h->hres);
    uint32swap(&h->vres);
    uint32swap(&h->ncolors);
    uint32swap(&h->nimpcolors);
}

void
readbmpheader(Bitmap *bp, FILE *fp) {
    BMPheader *h = &bp->bmpheader;

    xfread(h->id, sizeof(h->id), 1, fp);
    xfread(&h->size, sizeof(h->size), 1, fp);
    xfread(&h->unused1, sizeof(h->unused1), 1, fp);
    xfread(&h->unused2, sizeof(h->unused2), 1, fp);
    xfread(&h->offset, sizeof(h->offset), 1, fp);

    if (isbigendian())
        changeheaderendianness(&bp->bmpheader);
}

void
writebmpheader(const Bitmap *bp, FILE *fp) {
    BMPheader h = bp->bmpheader;

    if (isbigendian())
        changeheaderendianness(&h);

    xfwrite(h.id, sizeof(h.id), 1, fp);
    xfwrite(&(h.size), sizeof(h.size), 1, fp);
    xfwrite(&(h.unused1), sizeof(h.unused1), 1, fp);
    xfwrite(&(h.unused2), sizeof(h.unused2), 1, fp);
    xfwrite(&(h.offset), sizeof(h.offset), 1, fp);
}

void
readdibheader(Bitmap *bp, FILE *fp) {
    DIBheader *h = &bp->dibheader;

    xfread(&h->size, sizeof(h->size), 1, fp);
    xfread(&h->width, sizeof(h->width), 1, fp);
    xfread(&h->height, sizeof(h->height), 1, fp);
    xfread(&h->nplanes, sizeof(h->nplanes), 1, fp);
    xfread(&h->depth, sizeof(h->depth), 1, fp);
    xfread(&h->compression, sizeof(h->compression), 1, fp);
    xfread(&h->pixelarraysize, sizeof(h->pixelarraysize), 1, fp);
    xfread(&h->hres, sizeof(h->hres), 1, fp);
    xfread(&h->vres, sizeof(h->vres), 1, fp);
    xfread(&h->ncolors, sizeof(h->ncolors), 1, fp);
    xfread(&h->nimpcolors, sizeof(h->nimpcolors), 1, fp);

    if (isbigendian())
        changedibendianness(&bp->dibheader);
}

void
writedibheader(const Bitmap *bp, FILE *fp) {
    DIBheader h = bp->dibheader;

    if (isbigendian())
        changedibendianness(&h);

    xfwrite(&(h.size), sizeof(h.size), 1, fp);
    xfwrite(&(h.width), sizeof(h.width), 1, fp);
    xfwrite(&(h.height), sizeof(h.height), 1, fp);
    xfwrite(&(h.nplanes), sizeof(h.nplanes), 1, fp);
    xfwrite(&(h.depth), sizeof(h.depth), 1, fp);
    xfwrite(&(h.compression), sizeof(h.compression), 1, fp);
    xfwrite(&(h.pixelarraysize), sizeof(h.pixelarraysize), 1, fp);
    xfwrite(&(h.hres), sizeof(h.hres), 1, fp);
    xfwrite(&(h.vres), sizeof(h.vres), 1, fp);
    xfwrite(&(h.ncolors), sizeof(h.ncolors), 1, fp);
    xfwrite(&(h.nimpcolors), sizeof(h.nimpcolors), 1, fp);
}

Bitmap *
bmpfromfile(const char *filename) {
    FILE *fp = xfopen(filename, "r");
    Bitmap *bp = xmalloc(sizeof(*bp));

    readbmpheader(bp, fp);
    readdibheader(bp, fp);
    xfread(bp->palette, sizeof(bp->palette), 1, fp);

    /* read pixel data */
    uint32_t imagesize = bmpimagesize(bp);
    bp->imgpixels = xmalloc(imagesize);
    xfread(bp->imgpixels, sizeof(bp->imgpixels[0]), imagesize, fp);
    xfclose(fp);

    return bp;
}

bool
isvalidbmpsize(FILE *fp, uint16_t k, uint32_t secretsize) {
    uint32_t shadowsize = (secretsize * 8)/k;
    uint32_t imgsize    = bmpfilewidth(fp) * bmpfileheight(fp);

    return imgsize >= shadowsize;
}

bool
kdivisiblesize(FILE *fp, uint16_t k) {
    int pixels = bmpfilewidth(fp) * bmpfileheight(fp);
    int aux    = pixels / k;

    return pixels == aux * k;
}

uint32_t
bmpimagesize(const Bitmap *bp) {
    uint32_t sz = bp->bmpheader.size;

    if (sz > 0)
        return bp->bmpheader.size - bp->bmpheader.offset;

    return bp->dibheader.pixelarraysize;
}

void
bmptofile(const Bitmap *bp, const char *filename) {
    FILE *fp = xfopen(filename, "w");

    writebmpheader(bp, fp);
    writedibheader(bp, fp);
    xfwrite(bp->palette, PALETTE_SIZE, 1, fp);
    xfwrite(bp->imgpixels, bmpimagesize(bp), 1, fp);
    xfclose(fp);
}

/* find closest pair of values that when multiplied, give x.
 * Used to make the shadows as 'squared' as possible */
void
findclosestpair(uint32_t x, uint32_t *width, int32_t *height) {
    unsigned int y = floor(sqrt(x));

    for (; y > 2; y--)
        if (x % y == 0) {
            *width  = y;
            *height = x / y;
            break;
        }
}

Bitmap *
newshadow(uint32_t width, int32_t height, uint16_t seed, uint16_t shadownumber) {
    return newbitmaphelper(width, height, seed, shadownumber, width * height);
}

Bitmap **
formshadows(const Bitmap *bp, uint16_t k, uint16_t n, uint16_t seed) {
    uint32_t width;
    int32_t height;
    uint32_t pixelarraysize = bmpimagesize(bp);
    Bitmap **shadows = xmalloc(sizeof(*shadows) * n);

    findclosestpair(pixelarraysize/k, &width, &height);

    /* allocate shadows */
    for (size_t i = 0; i < n; i++)
        shadows[i] = newshadow(width, height, seed, i+1);

    /* generate shadow image pixels */
    for (size_t j = 0; j*k < pixelarraysize; j++) {
        uint8_t *coeff = &bp->imgpixels[j*k];
        for (size_t i = 0; i < n; i++)
            shadows[i]->imgpixels[j] = generatepixel(coeff, k-1, i+1);
    }

    return shadows;
}

/* Gauss-Jordan elimination under modular arithmetic */
void
findcoefficients(int **mat, uint16_t k) {

    /* take matrix to echelon form */
    for (size_t j = 0; j < k-1; j++) {
        for (size_t i = k-1; i > j; i--) {
            int a = mat[i][j] * modinv[mat[i-1][j]];
            for (size_t t = j; t < k+1; t++) {
                int temp = mat[i][t] - ((mat[i-1][t] * a) % PRIME);
                mat[i][t] = mod(temp, PRIME);
            }
        }
    }

    /* take matrix to reduced row echelon form */
    for (size_t i = k-1; i > 0; i--) {
        mat[i][k] = (mat[i][k] * modinv[mat[i][i]]) % PRIME;
        mat[i][i] = (mat[i][i] * modinv[mat[i][i]]) % PRIME;
        for (int t = i-1; t >= 0; t--) {
            int temp = mat[t][k] - ((mat[i][k] * mat[t][i]) % PRIME);
            mat[t][k] = mod(temp, PRIME);
            mat[t][i] = 0;
        }
    }
}

Bitmap *
revealsecret(Bitmap **shadows, uint32_t width, int32_t height, uint16_t k) {
    uint32_t pixels = (*shadows)->dibheader.pixelarraysize;
    Bitmap *bmp = newbitmap(width, height, (*shadows)->bmpheader.unused1);

    int **mat = xmalloc(sizeof(*mat) * k);
    for (size_t i = 0; i < k; i++)
        mat[i] = xmalloc(sizeof(**mat) * (k+1));

    for (size_t i = 0; i < pixels; i++) {
        for (size_t j = 0; j < k; j++) {
            Bitmap *sp = shadows[j];
            int value = sp->bmpheader.unused2;
            mat[j][0] = 1;
            for (size_t t = 1; t < k; t++) {
                mat[j][t] = value;
                value *= sp->bmpheader.unused2;
            }
            mat[j][k] = sp->imgpixels[i];
        }
        findcoefficients(mat, k);
        for (size_t j = i * k; j < (i+1) * k; j++) {
            bmp->imgpixels[j] = mat[j % k][k];
        }
    }

    //unpermutepixels(bmp, sp->bmpheader.unused1);

    for (size_t i = 0; i < k; i++)
        free(mat[i]);
    free(mat);

    return bmp;
}

void
hideshadow(Bitmap *bp, const Bitmap *shadow) {
    char shadowfilename[20] = {0};
    uint32_t pixels = bmpimagesize(shadow);

    bp->bmpheader.unused1 = shadow->bmpheader.unused1;
    bp->bmpheader.unused2 = shadow->bmpheader.unused2;
    xsnprintf(shadowfilename, 20, "shadow%d.bmp", shadow->bmpheader.unused2);

    for (size_t i = 0; i < pixels; i++) {
        uint8_t byte = shadow->imgpixels[i];
        for (size_t j = i*8; j < 8*(i+1); j++) {
            if (byte & 0x80) /* 1000 0000 */
                RIGHTMOST_BIT_ON(bp->imgpixels[j]);
            else
                RIGHTMOST_BIT_OFF(bp->imgpixels[j]);
            byte <<= 1;
        }
    }
    bmptofile(bp, shadowfilename);
}

/* width and height parameters needed because the image hiding the shadow could
 * be bigger than necessary */
Bitmap *
retrieveshadow(const Bitmap *bp, uint32_t width, int32_t height, uint16_t k) {
    uint16_t key          = bp->bmpheader.unused1;
    uint16_t shadownumber = bp->bmpheader.unused2;

    findclosestpair(calculatepixelarraysize(width, height)/k, &width, &height);
    Bitmap *shadow = newshadow(width, height, key, shadownumber);
    uint32_t shadowpixels = shadow->dibheader.pixelarraysize;

    for (uint32_t i = 0; i < shadowpixels; i++) {
        uint8_t byte = 0;
        uint8_t mask = 0x80; /* 1000 0000 */
        for (uint32_t j = i*8; j < 8*(i+1); j++) {
            if (bp->imgpixels[j] & 0x01)
                byte |= mask;
            mask >>= 1;
        }
        shadow->imgpixels[i] = byte;
    }

    return shadow;
}

bool
isbmp(FILE *fp) {
    char magicnumber[2];
    long pos = ftell(fp);

    xfseek(fp, 0, SEEK_SET);
    xfread(magicnumber, sizeof(magicnumber), 1, fp);
    xfseek(fp, pos, SEEK_SET);

    return magicnumber[0] == 'B' && magicnumber[1] == 'M';
}

bool
isvalidshadow(FILE *fp, uint16_t k, uint32_t secretsize) {
    uint16_t shadownumber;
    long pos = ftell(fp);

    xfseek(fp, UNUSED2_OFFSET, SEEK_SET);
    xfread(&shadownumber, sizeof(shadownumber), 1, fp);
    xfseek(fp, pos, SEEK_SET);

    return shadownumber && isbmp(fp) && isvalidbmpsize(fp, k, secretsize);
}

/* the last parameter is ignored, and is only present so that the function
 * prototype can be used with getvalidfilenames() */
bool
isvalidbmp(FILE *fp, uint16_t k, uint32_t ignoredparameter) {
    return isbmp(fp) && kdivisiblesize(fp, k);
}

char **
getvalidfilenames(const char *dir, uint16_t k, uint16_t n, fn isvalid, uint32_t size) {
    struct dirent *d;
    FILE *fp;
    DIR *dp = xopendir(dir);
    size_t i = 0;
    char filepath[PATH_MAX] = {0};
    char **filenames = xmalloc(sizeof(*filenames) * n);

    while ((d = readdir(dp)) && i < n) {
        if (d->d_type == DT_REG) {
            size_t len = xsnprintf(filepath, PATH_MAX, "%.*s/%.*s", DIR_MAX, dir, NAME_MAX, d->d_name);
            fp = xfopen(filepath, "r");
            if (isvalid(fp, k, size)) {
                filenames[i] = xmalloc(len + 1UL);
                strncpy(filenames[i], filepath, len);
                filenames[i][len] = '\0'; /* NULL terminate string */
                i++;
            }
            xfclose(fp);
        }
    }
    xclosedir(dp);

    if (i < n)
        die("not enough valid bmps for a (%d,%d) threshold scheme in dir %s\n", k, n, dir);

    return filenames;
}

char **
getbmpfilenames(const char *dir, uint16_t k, uint16_t n, uint32_t size) {
    return getvalidfilenames(dir, k, n, isvalidbmp, size);
}

char **
getshadowfilenames(const char *dir, uint16_t k, uint32_t size) {
    return getvalidfilenames(dir, k, k, isvalidshadow, size);
}

void
distributeimage(const char *dir, const char *imgpath, uint16_t k, uint16_t n, uint16_t seed) {
    Bitmap *bmp, **shadows;

    bmp = bmpfromfile(imgpath);
    char ** filepaths = getbmpfilenames(dir, k, n, bmpimagesize(bmp));
    truncategrayscale(bmp);
    //permutepixels(bmp, seed);
    shadows = formshadows(bmp, k, n, seed);
    freebitmap(bmp);

    for (size_t i = 0; i < n; i++) {
        bmp = bmpfromfile(filepaths[i]);
        hideshadow(bmp, shadows[i]);
        freebitmap(bmp);
    }

    for (size_t i = 0; i < n; i++) {
        free(filepaths[i]);
        freebitmap(shadows[i]);
    }
    free(filepaths);
    free(shadows);
}

void
recoverimage(const char *dir, const char *filename, uint32_t width, int32_t height, uint16_t k) {
    Bitmap **shadows = xmalloc(sizeof(*shadows) * k);

    char **filepaths = getshadowfilenames(dir, k, width * height);
    for (size_t i = 0; i < k; i++) {
        Bitmap *bp = bmpfromfile(filepaths[i]);
        shadows[i] = retrieveshadow(bp, width, height, k);
        freebitmap(bp);
    }

    Bitmap *bmp = revealsecret(shadows, width, height, k);
    bmptofile(bmp, filename);
    freebitmap(bmp);

    for (size_t i = 0; i < k; i++) {
        free(filepaths[i]);
        freebitmap(shadows[i]);
    }
    free(filepaths);
    free(shadows);
}


void
truncategrayscale(Bitmap *bp) {
    uint32_t imgsize = bmpimagesize(bp);

    for (size_t i = 0; i < imgsize; i++)
        if (bp->imgpixels[i] > 250)
            bp->imgpixels[i] = 250;
}

void
permutepixels(Bitmap *bp, uint16_t seed) {
    uint32_t imgsize = bmpimagesize(bp);
    uint8_t *p  = bp->imgpixels;

    srand(seed);
    for (uint32_t i = imgsize - 1; i > 1; i--) {
        long j = randint(i);
        swap(&p[j], &p[i]);
    }
}

void
unpermutepixels(Bitmap *bp, uint16_t seed) {
    uint32_t imgsize = bmpimagesize(bp);
    long *permseq    = xmalloc(sizeof(*permseq) * imgsize);
    uint8_t *p       = bp->imgpixels;

    srand(seed);
    for (size_t i = imgsize - 1; i > 0; i--)
        permseq[i] = randint(i);

    for (size_t i = 1 ; i < imgsize - 1; i++) {
        long j = permseq[i];
        swap(&p[j], &p[i]);
    }
    free(permseq);
}

/* uses coeff[0] to coeff[degree] to evaluate the corresponding
 * section polynomial and generate a pixel for a shadow image */
uint8_t
generatepixel(const uint8_t *coeff, uint16_t degree, uint16_t value) {
    intmax_t ret = 0;

    for (size_t i = 0; i <= degree; i++)
        ret += coeff[i] * pow(value, i);

    return ret % PRIME;
}

int
main(int argc, char *argv[argc + 1]) {
    bool dflag      = 0;
    bool rflag      = 0;
    bool kflag      = 0;
    bool wflag      = 0;
    bool hflag      = 0;
    bool nflag      = 0;
    bool secretflag = 0;
    uint16_t seed   = DEFAULT_SEED;
    uint16_t k      = 0;
    uint16_t n      = 0;
    uint32_t width  = 0;
    int32_t height  = 0;
    char *filename  = 0;
    char *dir       = "./";
    char *endptr;

    argv0 = argv[0]; /* save program name for usage() */

    for (size_t i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            dflag = 1;
        } else if (strcmp(argv[i], "-r") == 0) {
            rflag = 1;
        } else if (strcmp(argv[i], "--secret") == 0) {
            secretflag = 1;
            if (i + 1 < argc) {
                filename = argv[++i];
            } else {
                usage();
            }
        } else if (strcmp(argv[i], "-k") == 0) {
            kflag = 1;
            if (i + 1 < argc) {
                long int l = xstrtol(argv[++i], &endptr, 10);
                if (l <= UINT16_MAX)
                    k = l;
                else
                    die("k must be 2 <= k <= %d; was %d", UINT16_MAX, l);
            } else {
                usage();
            }
        } else if (strcmp(argv[i], "-w") == 0) {
            wflag = 1;
            if (i + 1 < argc) {
                long int l = xstrtol(argv[++i], &endptr, 10);
                if (l <= UINT32_MAX)
                    width = l;
                else
                    die("width must be less or equal to %d; was %d", UINT32_MAX, l);
            } else {
                usage();
            }
        } else if (strcmp(argv[i], "-h") == 0) {
            hflag = 1;
            if (i + 1 < argc) {
                long int l = xstrtol(argv[++i], &endptr, 10);
                if (INT32_MIN <= l && l <= INT32_MAX)
                    height = l;
                else
                    die("height must be %d <= height <= %d; was %d", INT32_MIN, INT32_MAX, l);
            } else {
                usage();
            }
        } else if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 < argc) {
                long int l = xstrtol(argv[++i], &endptr, 10);
                if (l <= UINT16_MAX)
                    seed = l;
                else
                    die("seed must be less or equal to %d; was %d", UINT16_MAX, l);
            } else {
                usage();
            }
        } else if (strcmp(argv[i], "-n") == 0) {
            nflag = 1;
            if (i + 1 < argc) {
                long int l = xstrtol(argv[++i], &endptr, 10);
                if (l <= UINT16_MAX)
                    n = l;
                else
                    die("n must be 2 <= n <= 65535; was %d", l);
            } else {
                usage();
            }
        } else if (strcmp(argv[i], "--dir") == 0) {
            if (i + 1 < argc) {
                dir = argv[++i];
            } else{
                usage();
            }
        } else {
            die("invalid %s parameter \n", argv[i]);
        }
    }

    if (!(dflag || rflag) || !secretflag || !kflag)
        usage();
    if ((rflag && !(wflag && hflag)) || !width || !height)
        die("specify a positive width and height with -w -h for the revealed image\n");

    if (!nflag)
        n = countfiles(dir);

    if (k > n || k < 2 || n < 2)
        die("k and n must be: 2 <= k <= n\n");
    if (dflag && rflag)
        die("can't use -d and -r flags simultaneously\n");

    if (dflag)
        distributeimage(dir, filename, k, n, seed);
    else if (rflag)
        recoverimage(dir, filename, width, height, k);

    return EXIT_SUCCESS;
}
