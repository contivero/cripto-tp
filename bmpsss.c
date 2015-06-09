#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#define BMP_HEADER_SIZE        14
#define DIB_HEADER_SIZE_OFFSET 14
#define DIB_HEADER_SIZE        40
#define PALETTE_SIZE           1024
#define PIXEL_ARRAY_OFFSET     BMP_HEADER_SIZE + DIB_HEADER_SIZE + PALETTE_SIZE
#define FILESIZE_OFFSET        2	
#define WIDTH_OFFSET           18
#define HEIGHT_OFFSET          22
#define PRIME_MOD              251
#define ERROR                  1
#define OK                     0
#define MIN_PARAMETERS         6

typedef enum {CIPHER, DECRYPTION} operation_type;
typedef enum {DONE, NOT_DONE} status;

typedef struct {
	uint8_t id[2];     /* magic number to identify the BMP format */
	uint32_t size;     /* size of the BMP file in bytes */
	uint16_t unused1;  /* reserved */
	uint16_t unused2;  /* reserved */
	uint32_t offset;   /* starting address of the pixel array (bitmap data) */
} BMPheader;

/* 40 bytes BITMAPINFOHEADER */
typedef struct {
	uint32_t size;            /* the size of this header (40 bytes) */
	uint32_t width;           /* the bitmap width in pixels */
	uint32_t height;          /* the bitmap height in pixels */
	uint16_t nplanes;         /* number of color planes used; Must set to 1 */
	uint16_t depth;           /* bpp number. Usually: 1, 4, 8, 16, 24 or 32 */
	uint32_t compression;     /* compression method used */
	uint32_t pixelarraysize;  /* size of the raw bitmap (pixel) data */
	uint32_t hres;            /* horizontal resolution (pixel per meter) */
	uint32_t vres;            /* vertical resolution (pixel per meter) */
	uint32_t ncolors;         /* colors in the palette. 0 means 2^n */
	uint32_t nimpcolors;      /* important colors used, usually ignored */
} DIBheader;

typedef struct {
	BMPheader bmpheader;  /* 14 bytes bmp starting header */
	DIBheader dibheader;  /* 40 bytes dib header */
	uint8_t *palette;     /* color palette */
	uint8_t *imgpixels;   /* array of bytes representing each pixel */
} Bitmap;

/* prototypes */ 
static void     die(const char *errstr, ...);
static void     *xmalloc(size_t size);
static FILE     *xfopen(const char *filename, const char *mode);
static void     xfclose(FILE *fp);
static void     xfread(void *ptr, size_t size, size_t nmemb, FILE *stream);
static void     xfwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
static void     usage(void);
static long     randint(long int max);
static double   randnormalize(void);
static int      generatepixel(uint8_t *coeff, int degree, int value);
static uint32_t get32bitsfromheader(FILE *fp, int offset);
static uint32_t bmpfilesize(FILE *fp);
static uint32_t bmpfilewidth(FILE *fp);
static uint32_t bmpfileheight(FILE *fp);
static uint32_t bmpfiledibheadersize(FILE *fp);
static void     freebitmap(Bitmap *bp);
static Bitmap   *bmpfromfile(char *filename);
static void     bmptofile(Bitmap *bp, const char *filename);
static int      bmpimagesize(Bitmap *bp);
static int      bmppalettesize(Bitmap *bp);
static Bitmap   **formshadows(Bitmap *bp, int r, int n);

/* globals */
static char *argv0; /* program name for usage() */

void
die(const char *errstr, ...){
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void *
xmalloc(size_t size){
	void *p = malloc(size);

	if(!p)
		die("Out of memory: couldn't malloc %d bytes\n", size);

	return p;
}

FILE *
xfopen (const char *filename, const char *mode){
	FILE *fp = fopen(filename, mode);

	if(!fp) 
		die("couldn't open %s", filename);

	return fp;
}

void
xfclose (FILE *fp){
	if(fclose(fp) == EOF)
		die("couldn't close file\n");
}

void
xfread(void *ptr, size_t size, size_t nmemb, FILE *stream){
	if (fread(ptr, size, nmemb, stream) < 1)
		die("read error"); /* TODO print errno string! */
}

void 
xfwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream){
	if (fwrite(ptr, size, nmemb, stream) != nmemb)
		die("Error in writing or end of file.\n");
}

void
usage(void){
	die("usage: %s -(d|r) -secret image -k number [-n number|-dir directory]\n"
		"2 <= k <= n; 2 <= n\n", argv0);
}

double 
randnormalize(void){
	return rand()/((double)RAND_MAX+1);
}

long
randint(long int max){
	return (long) (randnormalize()*(max+1)); /*returns num in [0,max]*/
} 

uint32_t
get32bitsfromheader(FILE *fp, int offset){
	uint32_t value;
	uint32_t pos = ftell(fp);

	fseek(fp, offset, SEEK_SET);
	xfread(&value, 4, 1, fp);
	fseek(fp, pos, SEEK_SET);

	return value;
}

uint32_t
bmpfilesize(FILE *fp){
	return get32bitsfromheader(fp, FILESIZE_OFFSET);
}

uint32_t
bmpfilewidth(FILE *fp){
	return get32bitsfromheader(fp, WIDTH_OFFSET);
}

uint32_t
bmpfileheight(FILE *fp){
	return get32bitsfromheader(fp, HEIGHT_OFFSET);
}

uint32_t
bmpfiledibheadersize(FILE *fp){
	uint32_t size = get32bitsfromheader(fp, DIB_HEADER_SIZE_OFFSET);

	if(size != DIB_HEADER_SIZE)
		die("unsupported dib header format\n");

	return size;
}

void
freebitmap(Bitmap *bp){
	free(bp->palette);
	free(bp->imgpixels);
	free(bp);
}

void
bmpheaderdebug(Bitmap *bp){
	printf("ID: %c%-15c size: %-16d r1: %-16d r2: %-16d offset: %-16d\n",
			bp->bmpheader.id[0], bp->bmpheader.id[1], bp->bmpheader.size, 
			bp->bmpheader.unused1, bp->bmpheader.unused2, bp->bmpheader.offset);
}

void
dibheaderdebug(Bitmap *bp){
	printf("dibsize: %-16d width: %-16d height: %-16d\n" 
			"nplanes: %-16d depth: %-16d compression:%-16d\n"
			"pixelarraysize: %-16d hres: %-16d vres:%-16d\n"
			"ncolors: %-16d nimpcolors: %-16d\n", bp->dibheader.size,
			bp->dibheader.width, bp->dibheader.height, bp->dibheader.nplanes,
			bp->dibheader.depth, bp->dibheader.compression,
			bp->dibheader.pixelarraysize, bp->dibheader.hres, bp->dibheader.vres,
			bp->dibheader.ncolors, bp->dibheader.nimpcolors);
}

void
readbmpheader(Bitmap *bp, FILE *fp){
	xfread(&bp->bmpheader.id, 1, sizeof(bp->bmpheader.id), fp);
	xfread(&bp->bmpheader.size, 1, sizeof(bp->bmpheader.size), fp);
	xfread(&bp->bmpheader.unused1, 1, sizeof(bp->bmpheader.unused1), fp);
	xfread(&bp->bmpheader.unused2, 1, sizeof(bp->bmpheader.unused2), fp);
	xfread(&bp->bmpheader.offset, 1, sizeof(bp->bmpheader.offset), fp);
}

void
writebmpheader(Bitmap *bp, FILE *fp){
	xfwrite(&bp->bmpheader.id, 1, sizeof(bp->bmpheader.id), fp);
	xfwrite(&bp->bmpheader.size, 1, sizeof(bp->bmpheader.size), fp);
	xfwrite(&bp->bmpheader.unused1, 1, sizeof(bp->bmpheader.unused1), fp);
	xfwrite(&bp->bmpheader.unused2, 1, sizeof(bp->bmpheader.unused2), fp);
	xfwrite(&bp->bmpheader.offset, 1, sizeof(bp->bmpheader.offset), fp);
}

void
readdibheader(Bitmap *bp, FILE *fp){
	xfread(&bp->dibheader.size, 1, sizeof(bp->dibheader.size), fp);
	xfread(&bp->dibheader.width, 1, sizeof(bp->dibheader.width), fp);
	xfread(&bp->dibheader.height, 1, sizeof(bp->dibheader.height), fp);
	xfread(&bp->dibheader.nplanes, 1, sizeof(bp->dibheader.nplanes), fp);
	xfread(&bp->dibheader.depth, 1, sizeof(bp->dibheader.depth), fp);
	xfread(&bp->dibheader.compression, 1, sizeof(bp->dibheader.compression), fp);
	xfread(&bp->dibheader.pixelarraysize, 1, sizeof(bp->dibheader.pixelarraysize), fp);
	xfread(&bp->dibheader.hres, 1, sizeof(bp->dibheader.hres), fp);
	xfread(&bp->dibheader.vres, 1, sizeof(bp->dibheader.vres), fp);
	xfread(&bp->dibheader.ncolors, 1, sizeof(bp->dibheader.ncolors), fp);
	xfread(&bp->dibheader.nimpcolors, 1, sizeof(bp->dibheader.nimpcolors), fp);
}

void
writedibheader(Bitmap *bp, FILE *fp){
	xfwrite(&bp->dibheader.size, 1, sizeof(bp->dibheader.size), fp);
	xfwrite(&bp->dibheader.width, 1, sizeof(bp->dibheader.width), fp);
	xfwrite(&bp->dibheader.height, 1, sizeof(bp->dibheader.height), fp);
	xfwrite(&bp->dibheader.nplanes, 1, sizeof(bp->dibheader.nplanes), fp);
	xfwrite(&bp->dibheader.depth, 1, sizeof(bp->dibheader.depth), fp);
	xfwrite(&bp->dibheader.compression, 1, sizeof(bp->dibheader.compression), fp);
	xfwrite(&bp->dibheader.pixelarraysize, 1, sizeof(bp->dibheader.pixelarraysize), fp);
	xfwrite(&bp->dibheader.hres, 1, sizeof(bp->dibheader.hres), fp);
	xfwrite(&bp->dibheader.vres, 1, sizeof(bp->dibheader.vres), fp);
	xfwrite(&bp->dibheader.ncolors, 1, sizeof(bp->dibheader.ncolors), fp);
	xfwrite(&bp->dibheader.nimpcolors, 1, sizeof(bp->dibheader.nimpcolors), fp);
}

Bitmap *
bmpfromfile(char *filename){
	FILE *fp = xfopen(filename, "r");
	Bitmap *bp = xmalloc(sizeof(*bp));

	readbmpheader(bp, fp);
	readdibheader(bp, fp);

	/* TODO: debug info, delete later! */
	bmpheaderdebug(bp);
	dibheaderdebug(bp);

	/* read color palette */
	int palettesize = bmppalettesize(bp);
	bp->palette = xmalloc(palettesize);
	xfread(bp->palette, 1, palettesize, fp);

	/* read pixel data */
	int imagesize = bmpimagesize(bp);
	bp->imgpixels = xmalloc(imagesize);
	xfread(bp->imgpixels, 1, imagesize, fp);

	xfclose(fp);
	return bp;
}

int
bmpimagesize(Bitmap *bp){
	return bp->bmpheader.size - bp->bmpheader.offset;
}

int
bmppalettesize(Bitmap *bp){
	return bp->bmpheader.offset - (BMP_HEADER_SIZE + bp->dibheader.size);
}

void
bmptofile(Bitmap *bp, const char *filename){
	FILE *fp = xfopen(filename, "w");

	writebmpheader(bp, fp);
	writedibheader(bp, fp);
	xfwrite(bp->palette, bmppalettesize(bp), 1, fp);
	xfwrite(bp->imgpixels, bmpimagesize(bp), 1, fp);
	xfclose(fp);
}

void
truncategrayscale(Bitmap *bp){
	int palettesize = bmppalettesize(bp);

	for(int i = 0; i < palettesize; i++)
		if(bp->palette[i] > 250)
			bp->palette[i] = 250;
}

/* Durstenfeld algorithm */
void
permutepixels(Bitmap *bp){
	int i, j, temp;
	uint8_t *p = bp->imgpixels;
	srand(10); /* TODO preguntar sobre la "key" de permutación! */

	for(i = bmpimagesize(bp)-1; i > 1; i--){
		j = randint(i);
		temp = p[j];
		p[j] = p[i];
		p[i] = temp;
	}
}

/* uses coeff[0] to coeff[degree] to evaluate the corresponding
 * section polynomial and generate a pixel for a shadow image */
int
generatepixel(uint8_t *coeff, int degree, int value){
	long ret = 0;

	for(int i = 0; i <= degree; i++)
		ret += coeff[i] * powl(value,i);

	return ret % PRIME_MOD;
}

Bitmap **
formshadows(Bitmap *bp, int r, int n){
	uint8_t *coeff;
	int i, j;
	int totalpixels = bmpimagesize(bp);
	Bitmap **shadows = xmalloc(sizeof(*shadows) * n);
	Bitmap *sp;

	/* allocate memory for shadows and copy necessary data */
	for(i = 0; i < n; i++){
		shadows[i] = xmalloc(sizeof(**shadows));
		sp = shadows[i];
		sp->palette = xmalloc(bmppalettesize(bp));
		memcpy(sp->palette, bp->palette, bmppalettesize(bp));
		sp->imgpixels = xmalloc(totalpixels/r);
		memcpy(&sp->bmpheader, &bp->bmpheader, sizeof(bp->bmpheader));
		memcpy(&sp->dibheader, &bp->dibheader, sizeof(bp->dibheader));
		sp->dibheader.height = bp->dibheader.height / r;
		sp->dibheader.pixelarraysize = bp->dibheader.pixelarraysize / r;
		sp->bmpheader.size = bp->dibheader.pixelarraysize/r + PIXEL_ARRAY_OFFSET;
	}

	/* generate shadow image pixels */
	for(j = 0; j*r < totalpixels; j++){
		for(i = 0; i < n; i++){
			coeff = &bp->imgpixels[j*r]; 
			shadows[i]->imgpixels[j] = generatepixel(coeff, r-1, i+1);
		}
	}

	return shadows;
}

Bitmap *
recover(Bitmap **shadows, int r){
	int i, j;
	int npixels = bmpimagesize(*shadows);
	Bitmap *bp = xmalloc(sizeof(*bp));

	for(i = 0; i < npixels; i++){
		for(j = 0; j < r; j++){
			// TODO
		}
	}

	return bp;
}

/* debugging function: TODO delete later! */
void
printmat(double **mat, int rows, int cols){
	int i, j;

	for(i = 0; i < rows; i++){
		printf("|");
		for(j = 0; j < cols; j++){
			printf("%f ", mat[i][j]);
		}
		printf("|\n");
	}
}

uint8_t *
revealpixels(double **mat, int r){
	uint8_t *pixels = xmalloc(sizeof(*pixels) * r);
	int i, j, k;
	double a;

	/* take matrix to echelon form */
	for(j = 0; j < r-1; j++){
		for(i = r-1; i > j; i--){
			a = mat[i][j]/mat[i-1][j];
			for(k = j; k < r+1; k++){
				mat[i][k] -= mat[i-1][k]*a;
			}
		}
	}

	/* take matrix to reduced row echelon form */
	for(i = r-1; i > 0; i--){
		j = i;
		mat[i][r] /= mat[i][j];
		mat[i][j] = 1;
		for(k = i-1; k >= 0; k--){
			mat[k][r] -= mat[i][r] * mat[k][j];
			mat[k][j] = 0;
		}
	}

	for(i = 0; i < r; i++){
		pixels[i] = mat[i][r];
	}

	return pixels;
}

int
countfiles(char * path){
	int file_count = 0;
	DIR * dirp;
	struct dirent * entry;

	dirp = opendir(path); /* There should be error handling after this */
	while ((entry = readdir(dirp)) != NULL) {
	    if (entry->d_type == DT_REG) { /* If the entry is a regular file */
			int len = strlen(entry->d_name);
			const char *last_three = &entry->d_name[len-3];
			if(strncmp(last_three,"bmp", sizeof("bmp"))==0){
	        	file_count++;				
			}
	    }
	}
	closedir(dirp);

	return file_count;
}

int
main(int argc, char *argv[]){
	char *filename;
	if(argc<MIN_PARAMETERS){
		printf("%s\n","error in function call, not enought parameters");
		return ERROR;
	}else{
		char* compulsory_param[] = {"-d", "-r", "-secret", "-k"};
		char* optional_params[] = {"-n", "-dir"};
		char *argv0, *filename, *secret, *dir=NULL;
		operation_type operation;
		int r, n, compulsory=0;
		status operation_status = NOT_DONE;
		argv0 = argv[0]; /* Saving program name for future use */
		for (int i = 1; i < argc; i++){
			 if (i + 1 != argc){
			 	if(strncmp(argv[i], compulsory_param[0], sizeof(compulsory_param[0]))==0 && operation_status!=DONE){
			 		operation=DECRYPTION;
			 		operation_status = DONE;
			 		compulsory++;
			 	}else if(strncmp(argv[i], compulsory_param[1], sizeof(compulsory_param[1]))==0 && operation_status!=DONE){
					operation=CIPHER;
			 		operation_status = DONE;
			 		compulsory++;
			 	}else if(strncmp(argv[i], compulsory_param[2], sizeof(compulsory_param[2]))==0){
			 		filename=argv[i+1];
			 		i++;
			 		compulsory++;
			 	}else if(strncmp(argv[i], compulsory_param[3], sizeof(compulsory_param[3]))==0){
			 		sscanf(argv[i+1], "%d", &r);
			 		i++;
			 		compulsory++;
			 	}else if(strncmp(argv[i], optional_params[0], sizeof(optional_params[0]))==0){
			 		sscanf(argv[i+1], "%d", &n);
			 		i++;
			 	}else if(strncmp(argv[i], optional_params[1], sizeof(optional_params[1]))==0){
			 		dir = argv[i+1];
			 		i++;
			 	}else{
					printf("%s\n","error in function call");
					return ERROR;	
			 	}
			 }
		}

		/* Checking compulsory parameters and default for optionals*/
		int size = sizeof(compulsory_param)/sizeof(compulsory_param[0]);
		if(compulsory!=size-1){
			printf("%s\n","error in function call, incomplete parameters");
		}
		if(dir==NULL){
			dir="./";
		}
		if(n==0){
			int count=countfiles(dir);
			printf("%s %d\n","count", count);
			n=count;
		}


		Bitmap *bp = bmpfromfile(filename);
		truncategrayscale(bp);
		permutepixels(bp);
		Bitmap **shadows = formshadows(bp, r, n);

		/* write shadows to disk */
		for(n -= 1; n >= 0; n--){
			snprintf(filename, 256, "shadow%d.bmp", n);
			printf("%s\n", filename);
			bmptofile(shadows[n], filename);
			freebitmap(shadows[n]); 
		}
		freebitmap(bp);
		free(shadows);
	}

}
