#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#include <libgen.h>
#include "zlib.h"
#include "png.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

// CONSTANTS
#define COMPRESSED 1
#define TRANSPARENT_INDEX 254
#define BGF_VERSION 10

#define ATLAS_PAD 1
#define ATLAS_MAX_DIM 4096

// BGF VARIABLES TO BE FILLED
uint32_t version = 0;
char bitmap_name[32] = { 0 };
uint32_t bitmap_count = 0;
uint32_t group_count = 0;
uint32_t max_group_bitmaps = 0;
uint32_t shrink_factor = 1;
struct bitmap *bitmaps = NULL;
uint32_t *bitmap_groups = NULL;
uint32_t *bitmap_indexes = NULL;
FILE *bgf_file = NULL;

// MERIDIAN 59 COLOR PALETTE
static uint32_t hex_palette[256] = {
	0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080,
	0xC0C0C0, 0x800000, 0x008000, 0x800000, 0x008000, 0x800000, 0x008000,
	0x800000, 0x008000, 0xC20101, 0xB40101, 0xAB0202, 0xA60101, 0x9A0202,
	0x910200, 0x890200, 0x7F0000, 0x780200, 0x6D0100, 0x560000, 0x4C0000,
	0x400000, 0x380000, 0x260000, 0x110000, 0xFEC294, 0xEBB892, 0xDBA983,
	0xCB9D7C, 0xC69475, 0xB58769, 0xB18866, 0xA88060, 0x9D7356, 0x916B51,
	0x886048, 0x7A5844, 0x755440, 0x684D3B, 0x604631, 0x4A3B2D, 0xFFB580,
	0xF3A872, 0xDC9968, 0xCA8D61, 0xC48257, 0xB97A51, 0xAB7347, 0xA56E44,
	0x935C36, 0x855231, 0x7B4626, 0x6B3D22, 0x63381C, 0x552F18, 0x4B280D,
	0x321C0B, 0xB95F2B, 0x91461A, 0x833F18, 0x793B16, 0x773412, 0x722F10,
	0x69300C, 0x662D0C, 0x5E250C, 0x54220C, 0x4B1B0B, 0x41190B, 0x3C170B,
	0x33140B, 0x2A140B, 0x1B0F0A, 0xFFB233, 0xFFA91B, 0xFFA511, 0xFA9C00,
	0xEE9400, 0xD88700, 0xCC7F00, 0xC27900, 0xAA6A00, 0xA06400, 0x885500,
	0x7E4F00, 0x684100, 0x5C3900, 0x442A00, 0x301E00, 0x89B174, 0x82A96E,
	0x78A164, 0x70955C, 0x678B53, 0x5F814C, 0x587C49, 0x507042, 0x476537,
	0x3E5A31, 0x304F26, 0x29441F, 0x253E16, 0x1C3010, 0x101E08, 0x070E03,
	0x00C432, 0x00B82F, 0x00AA2B, 0x009E27, 0x009A27, 0x008C24, 0x008A23,
	0x007E20, 0x00721D, 0x006219, 0x005014, 0x004511, 0x003E10, 0x00300C,
	0x001A07, 0x000E04, 0xABD5DE, 0xA5CED7, 0x89BCC5, 0x7FACB3, 0x709AA3,
	0x6A919A, 0x4E8189, 0x48757D, 0x345F67, 0x2E555D, 0x1B464E, 0x173D46,
	0x0A343D, 0x062930, 0x031B21, 0x00090B, 0x344EDE, 0x324AD3, 0x2B3EC7,
	0x2A3ABC, 0x2434AB, 0x2230A1, 0x1B2C92, 0x172684, 0x0A1B78, 0x08186B,
	0x021256, 0x010F4B, 0x000A46, 0x00073B, 0x000329, 0x000018, 0xA042C2,
	0x993FB9, 0x9438B2, 0x862EA2, 0x7A2CA1, 0x6E2893, 0x66248B, 0x5E2081,
	0x56186F, 0x4E1263, 0x3F0355, 0x36004C, 0x2D003E, 0x21002F, 0x170020,
	0x0A0010, 0xF4F0CE, 0xEDE7B0, 0xEBE4A3, 0xE5DC89, 0xD8D7F6, 0xBBBAF0,
	0xAFADED, 0x9491E7, 0x9CE99C, 0x84E484, 0x5AD75A, 0x28B828, 0xF2C5C5,
	0xE89898, 0xE17777, 0xDC6262, 0xFFEA6E, 0xFADE37, 0xF7D51B, 0xF0D019,
	0xEECA1A, 0xDEBD19, 0xDCC413, 0xCFB910, 0xC5B40A, 0xB9A708, 0x9A8902,
	0x877A00, 0x807300, 0x777100, 0x706A00, 0x555100, 0xE7E7E7, 0xD5D5D5,
	0xCDCDCD, 0xBCBCBC, 0xB4B4B4, 0xA3A3A3, 0x9A9A9A, 0x929292, 0x818181,
	0x787878, 0x676767, 0x5F5F5F, 0x4E4E4E, 0x464646, 0x343434, 0x242424,
	0x7CBFFF, 0x67ABEF, 0x5FA3E7, 0x5F9AD5, 0x4E89C5, 0x4678AB, 0x3D70A3,
	0x3C6B9A, 0x345F89, 0x2C5277, 0x1B4167, 0x112F4D, 0x0A243D, 0x05182B,
	0x010E1B, 0x000B16, 0xE0B494, 0xD0B084, 0xCCA87C, 0xC4A074, 0x800000,
	0x008000, 0x800000, 0x008000, 0x808080, 0xFF0000, 0x00FF00, 0xFFFF00,
	0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF
};

// STRUCTS FOR INDIVIDUAL IMAGES IN BGF
struct hotspot {
	int8_t number;
	int32_t x, y;
};

struct bitmap {
	int x_pos, y_pos;
	int32_t width, height;
	int32_t x_offset, y_offset;
	uint8_t hotspot_count;
	struct hotspot *hotspots;
	uint8_t format;
	uint32_t compressed_size;
	uint8_t *image_bytes;
};

// frees all necessary variables and files
void cleanup()
{
	if (bgf_file)
		fclose(bgf_file);

	if (bitmaps) {
		for (int i = 0; i < bitmap_count; i++) {
			if (bitmaps[i].hotspots)
				free(bitmaps[i].hotspots);
			if (bitmaps[i].image_bytes)
				free(bitmaps[i].image_bytes);
		}
		free(bitmaps);
	}

	if (bitmap_groups)
		free(bitmap_groups);

	if (bitmap_indexes)
		free(bitmap_indexes);
}

// loads byte_count number of bytes into dest address
// return 0 on success, exits program on error
int load_bgf_bytes(void *dest, size_t byte_count)
{
	int items_read = fread(dest, byte_count, 1, bgf_file);

	if (items_read < 1) {
		fprintf(stderr, "Fatal Error: Failed to read from bgf file\n");
		cleanup();
		exit(EXIT_FAILURE);
	}

	return 0;
}

void load_bitmap(struct bitmap *bitmap)
{
	bitmap->x_pos = 0;
	bitmap->y_pos = 0;

	// load header information
	load_bgf_bytes(&bitmap->width, sizeof(bitmap->width));
	load_bgf_bytes(&bitmap->height, sizeof(bitmap->height));
	load_bgf_bytes(&bitmap->x_offset, sizeof(bitmap->x_offset));
	load_bgf_bytes(&bitmap->y_offset, sizeof(bitmap->y_offset));
	load_bgf_bytes(&bitmap->hotspot_count, sizeof(bitmap->hotspot_count));

	bitmap->hotspots =
		malloc(sizeof(*bitmap->hotspots) * bitmap->hotspot_count);

	// load hotspots
	for (int j = 0; j < bitmap->hotspot_count; ++j) {
		struct hotspot *hotspot = bitmap->hotspots + j;
		load_bgf_bytes(&hotspot->number, sizeof(hotspot->number));
		load_bgf_bytes(&hotspot->x, sizeof(hotspot->x));
		load_bgf_bytes(&hotspot->y, sizeof(hotspot->y));
	}

	// load the image bytes
	load_bgf_bytes(&bitmap->format, sizeof(bitmap->format));
	load_bgf_bytes(&bitmap->compressed_size,
		       sizeof(bitmap->compressed_size));

	unsigned long uncomp_size = bitmap->width * bitmap->height;
	bitmap->image_bytes = malloc(uncomp_size);

	if (bitmap->format == COMPRESSED) {
		uint8_t *source = malloc(bitmap->compressed_size);
		load_bgf_bytes(source, bitmap->compressed_size);
		int result = uncompress(bitmap->image_bytes, &uncomp_size,
					source, bitmap->compressed_size);
		free(source);
		if (result != Z_OK) {
			fprintf(stderr,
				"Error: Failed to uncompress bitmap image data\n");
			cleanup();
			exit(EXIT_FAILURE);
		}
	} else {
		load_bgf_bytes(bitmap->image_bytes, uncomp_size);
	}
}

void load_bgf()
{
	printf("Loading BGF header...\n");

	// load bgf header information
	int magic[4] = { 0x42, 0x47, 0x46, 0x11 };
	uint8_t byte;
	for (int i = 0; i < 4; i++) {
		load_bgf_bytes(&byte, 1);
		if (byte != magic[i]) {
			fprintf(stderr, "Error: Invalid BGF\n");
			cleanup();
			exit(EXIT_FAILURE);
		}
	}

	load_bgf_bytes(&version, sizeof(version));

	if (version != BGF_VERSION) {
		fprintf(stderr, "Error: Bad BGF version\n");
		cleanup();
		exit(EXIT_FAILURE);
	}

	load_bgf_bytes(bitmap_name, sizeof(bitmap_name));
	load_bgf_bytes(&bitmap_count, sizeof(bitmap_count));
	load_bgf_bytes(&group_count, sizeof(group_count));
	load_bgf_bytes(&max_group_bitmaps, sizeof(max_group_bitmaps));
	load_bgf_bytes(&shrink_factor, sizeof(shrink_factor));

	// calloc to ensure pointers are 0 (for cleanup check)
	bitmaps = calloc(bitmap_count, sizeof(*bitmaps));

	printf("Loading bitmaps...\n");

	// start loading bitmaps
	for (int i = 0; i < bitmap_count; i++) {
		load_bitmap(bitmaps + i);
	}

	printf("Loading groups and indexes...\n");

	bitmap_groups = malloc(sizeof(*bitmap_groups) * group_count);
	bitmap_indexes = malloc(sizeof(*bitmap_indexes) * max_group_bitmaps *
				group_count);

	int indexes_offset = 0;
	for (int i = 0; i < group_count; i++) {
		load_bgf_bytes(bitmap_groups + i, sizeof(*bitmap_groups));
		uint32_t index_count = bitmap_groups[i];

		for (int j = 0; j < index_count; ++j) {
			load_bgf_bytes(bitmap_indexes + indexes_offset + j,
				       sizeof(*bitmap_indexes));
		}

		indexes_offset += index_count;
	}
}

// return 0 on success, -1 on error
int write_png(char *file_name, struct bitmap *bitmap)
{
	FILE *fp;
	png_structp png_ptr;
	png_infop info_ptr;
	png_color palette[256];
	png_byte trans_alpha[256];

	fp = fopen(file_name, "wb");

	if (!fp) {
		fprintf(stderr, "Error: Failed to create png %s: %s\n",
			file_name, strerror(errno));
		return -1;
	}

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL,
					  NULL);

	if (!png_ptr) {
		fprintf(stderr, "Error: Failed to initialize libpng struct\n");
		fclose(fp);
		return -1;
	}

	info_ptr = png_create_info_struct(png_ptr);

	if (!info_ptr) {
		png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
		fprintf(stderr,
			"Error: Failed to initialize libpng info struct\n");
		fclose(fp);
		return -1;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);
		return -1;
	}

	png_init_io(png_ptr, fp);

	png_set_IHDR(png_ptr, info_ptr, bitmap->width, bitmap->height, 8,
		     PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
		     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	for (int i = 0; i < 256; i++) {
		palette[i].red = (hex_palette[i] & 0xFF0000) >> 16;
		palette[i].green = (hex_palette[i] & 0x00FF00) >> 8;
		palette[i].blue = hex_palette[i] & 0x0000FF;
		trans_alpha[i] = 255;
	}

	trans_alpha[TRANSPARENT_INDEX] = 0;

	png_set_tRNS(png_ptr, info_ptr, trans_alpha, 256, NULL);

	png_set_PLTE(png_ptr, info_ptr, palette, 256);

	uint8_t **row_pointers = malloc(sizeof(uint8_t *) * bitmap->height);

	for (int i = 0; i < bitmap->height; i++) {
		row_pointers[i] = bitmap->image_bytes + bitmap->width * i;
	}

	png_set_rows(png_ptr, info_ptr, (png_bytepp)row_pointers);

	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	png_write_end(png_ptr, NULL);

	png_destroy_write_struct(&png_ptr, &info_ptr);
	free(row_pointers);
	fclose(fp);
	return 0;
}

int pack_rects()
{
	struct stbrp_rect *rects = malloc(sizeof(*rects) * bitmap_count);
	for (int i = 0; i < bitmap_count; i++) {
		struct stbrp_rect *r = &rects[i];
		r->w = bitmaps[i].width + ATLAS_PAD * 2;
		r->h = bitmaps[i].height + ATLAS_PAD * 2;
	}

	int dim = 256;
	int result = 0;
	while (dim < ATLAS_MAX_DIM) {
		struct stbrp_context ctx;
		struct stbrp_node *nodes = malloc(sizeof(*nodes) * dim);
		stbrp_init_target(&ctx, dim, dim, nodes, dim);
		result = stbrp_pack_rects(&ctx, rects, bitmap_count);
		free(nodes);
		if (result) {
			break;
		}
		dim *= 2;
	}

	if (!result) {
		free(rects);
		return -1;
	}

	for (int i = 0; i < bitmap_count; i++) {
		struct stbrp_rect *r = &rects[i];
		bitmaps[i].x_pos = r->x + ATLAS_PAD;
		bitmaps[i].y_pos = r->y + ATLAS_PAD;
	}

	free(rects);
	return 0;
}

int pack_bitmaps(struct bitmap *b)
{
	if (pack_rects() == -1) {
		return -1;
	}
	int max_width = 0;
	int max_height = 0;
	for (int i = 0; i < bitmap_count; i++) {
		struct bitmap *bm = &bitmaps[i];
		int width = bm->x_pos + bm->width + ATLAS_PAD;
		int height = bm->y_pos + bm->height + ATLAS_PAD;
		if (width > max_width)
			max_width = width;
		if (height > max_height)
			max_height = height;
	}
	b->width = max_width;
	b->height = max_height;
	b->image_bytes = malloc(max_width * max_height);
	memset(b->image_bytes, TRANSPARENT_INDEX, max_width * max_height);

	for (int i = 0; i < bitmap_count; i++) {
		struct bitmap *bm = &bitmaps[i];

		int x = bm->x_pos;
		int y = bm->y_pos;
		int w = bm->width;
		int h = bm->height;

		uint8_t *bp = bm->image_bytes;
		uint8_t *ap = b->image_bytes;
		for (int r = 0; r < h; r++) {
			uint8_t *src = &bp[r * w];
			uint8_t *dst = &ap[x + (y + r) * b->width];
			memcpy(dst, src, w);
		}
	}
	return 0;
}

// return 0 on success, -1 on failure
int export_metadata(char *json_file_name, char *png_file_name)
{
	FILE *fp = fopen(json_file_name, "w");

	if (!fp) {
		fprintf(stderr, "Error: Failed to create json %s: %s\n",
			json_file_name, strerror(errno));
		return -1;
	}

	fprintf(fp, "{\n");
	fprintf(fp, "\t\"name\": \"%s\",\n", bitmap_name);
	fprintf(fp, "\t\"version\": %d,\n", version);
	fprintf(fp, "\t\"sprite_count\": %d,\n", bitmap_count);
	fprintf(fp, "\t\"group_count\": %d,\n", group_count);
	fprintf(fp, "\t\"shrink_factor\": %d,\n", shrink_factor);
	fprintf(fp, "\t\"image_file\": \"%s\",\n", png_file_name);
	fprintf(fp, "\t\"sprites\": [\n");

	for (int i = 0; i < bitmap_count; i++) {
		struct bitmap *bitmap = bitmaps + i;
		fprintf(fp, "\t\t{\n");
		fprintf(fp, "\t\t\t\"x_pos\": %d,\n", bitmap->x_pos);
		fprintf(fp, "\t\t\t\"y_pos\": %d,\n", bitmap->y_pos);
		fprintf(fp, "\t\t\t\"width\": %d,\n", bitmap->width);
		fprintf(fp, "\t\t\t\"height\": %d,\n", bitmap->height);
		fprintf(fp, "\t\t\t\"x_offset\": %d,\n", bitmap->x_offset);
		fprintf(fp, "\t\t\t\"y_offset\": %d,\n", bitmap->y_offset);
		fprintf(fp, "\t\t\t\"hotspot_count\": %d,\n",
			bitmap->hotspot_count);
		fprintf(fp, "\t\t\t\"hotspots\": [\n");
		for (int j = 0; j < bitmap->hotspot_count; j++) {
			struct hotspot *hotspot = bitmap->hotspots + j;
			fprintf(fp, "\t\t\t\t{\n");
			fprintf(fp, "\t\t\t\t\t\"number\": %d,\n",
				hotspot->number);
			fprintf(fp, "\t\t\t\t\t\"x\": %d,\n", hotspot->x);
			fprintf(fp, "\t\t\t\t\t\"y\": %d\n", hotspot->y);
			if (j == bitmap->hotspot_count - 1) {
				fprintf(fp, "\t\t\t\t}\n");
			} else {
				fprintf(fp, "\t\t\t\t},\n");
			}
		}
		fprintf(fp, "\t\t\t]\n");
		if (i == bitmap_count - 1) {
			fprintf(fp, "\t\t}\n");
		} else {
			fprintf(fp, "\t\t},\n");
		}
	}
	fprintf(fp, "\t],\n");
	fprintf(fp, "\t\"groups\": [\n");
	int indexes_offset = 0;
	for (int i = 0; i < group_count; i++) {
		fprintf(fp, "\t\t{\n");
		fprintf(fp, "\t\t\t\"index_count\": %d,\n", bitmap_groups[i]);
		fprintf(fp, "\t\t\t\"indexes\": [");
		for (int j = 0; j < bitmap_groups[i]; ++j) {
			if (j == bitmap_groups[i] - 1) {
				fprintf(fp, "%d]\n",
					bitmap_indexes[j + indexes_offset]);
			} else {
				fprintf(fp, "%d, ",
					bitmap_indexes[j + indexes_offset]);
			}
		}
		indexes_offset += bitmap_groups[i];
		if (i == group_count - 1) {
			fprintf(fp, "\t\t}\n");
		} else {
			fprintf(fp, "\t\t},\n");
		}
	}
	fprintf(fp, "\t]\n");
	fprintf(fp, "}\n");
	fclose(fp);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("Usage: %s <bgf file>\n", argv[0]);
		return EXIT_SUCCESS;
	}

	// open bgf file
	bgf_file = fopen(argv[1], "r");

	if (bgf_file == NULL) {
		fprintf(stderr, "Error: Failed to open %s: %s\n", argv[1],
			strerror(errno));
		return EXIT_FAILURE;
	}

	printf("Unpacking %s\n", argv[1]);

	load_bgf();

	struct bitmap b = { 0 };
	if (bitmap_count > 1) {
		printf("Converting bitmaps to PNG atlas...\n");
		if (pack_bitmaps(&b) == -1) {
			fprintf(stderr, "%s%s",
				"Error: Failed to pack bitmaps,",
				" try increasing ATLAS_MAX_DIM\n");
			cleanup();
			return EXIT_FAILURE;
		}
	} else {
		printf("Converting bitmap to PNG...\n");
		b = bitmaps[0];
	}

	size_t name_length = strlen(basename(argv[1])) + 5;
	char *png_name = malloc(name_length * sizeof(char));
	strcpy(png_name, basename(argv[1]));
	char *dot_loc = strrchr(png_name, '.');
	if (dot_loc) {
		sprintf(dot_loc, ".png");
	} else {
		strcat(png_name, ".png");
	}
	write_png(png_name, &b);

	// b is the image atlas in this case, needs manual free
	if (bitmap_count != 1) {
		free(b.image_bytes);
	}

	// manually export meta data to json file
	printf("Exporting metadata to json file...\n");

	char *json_name = malloc(strlen(basename(argv[1])) + 6);
	strcpy(json_name, basename(argv[1]));
	dot_loc = strrchr(json_name, '.');
	if (dot_loc) {
		sprintf(dot_loc, ".json");
	} else {
		strcat(json_name, ".json");
	}
	export_metadata(json_name, png_name);

	free(json_name);
	free(png_name);
	cleanup();
	printf("%s successfully unpacked\n", argv[1]);
	return EXIT_SUCCESS;
}