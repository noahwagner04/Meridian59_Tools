#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include "jansson.h"

// use getopt later for more input options
// e.g. -b for binary output, -e to embed textures, -l to disable "KHR_materials_unlit"

#define ROO_VERSION 10

// unit types
#define FINENESS 1024
#define KOD_FINENESS 64
#define BLAK_FACTOR 16
#define BITMAP_WIDTH 64

// wall flags
#define WF_BACKWARDS 0x00000001 // Draw bitmap right/left reversed
// #define WF_TRANSPARENT    0x00000002      // normal wall has some transparency
// #define WF_PASSABLE       0x00000004      // wall can be walked through
// #define WF_MAP_NEVER      0x00000008      // Don't show wall on map
// #define WF_MAP_ALWAYS     0x00000010      // Always show wall on map
// #define WF_NOLOOKTHROUGH  0x00000020      // bitmap can't be seen through even though it's transparent
#define WF_ABOVE_BOTTOMUP 0x00000040 // Draw upper texture bottom-up
#define WF_BELOW_TOPDOWN 0x00000080 // Draw lower texture top-down
#define WF_NORMAL_TOPDOWN 0x00000100 // Draw normal texture top-down
#define WF_NO_VTILE 0x00000200 // Don't tile texture vertically
// #define WF_HAS_ANIMATED	  0x00000400      // has animated once and hence is dynamic geometry, required for new client
// #define WF_NO_HTILE       0x00020000      // Don't tile texture horizontally (must be transparent)

// sector flags
#define SF_SLOPED_FLOOR 0x00000400 // Sector has sloped floor
#define SF_SLOPED_CEILING 0x00000800 // Sector has sloped ceiling

// JSON error types
#define ET_NO_ERROR 0
#define ET_LOAD 1
#define ET_SYNTAX 2
#define ET_INVALID_DATA 3

// BOWTIE FLAGS
#define BT_NONE 0
#define BT_POS 1 // positive starts above, ends below
#define BT_NEG 2 // negative starts above, ends below

// FACE FLAGS
#define FC_BELOW 0
#define FC_ABOVE 1
#define FC_NORMAL 2

// SIDE FLAGS
#define SD_POS 0
#define SD_NEG 1

#define NUM_DEGREES 4096

// utility data structures
struct dynamic_array {
	void *data;
	size_t data_size;
	size_t length;
	size_t capacity;
};

struct material {
	// if texture file can't be found, set to 0, otherwise 1
	int is_valid;
	char *texture_file_path;
	uint32_t tex_width, tex_height;
	uint32_t shrink_factor;
};

// NOTE: Draw ascii diagram of wall here for reference
// intermediate form of wall before going into mesh_object
struct wall_3d {
	// dereferenced sector & sidedef indexes
	struct sector *pos_sector, *neg_sector;
	struct sidedef *pos_sidedef, *neg_sidedef;

	// texture offsets (same as wall struct)
	int16_t pos_x_offset, pos_y_offset;
	int16_t neg_x_offset, neg_y_offset;

	// start and end coordinates, 0 and 1 respectively
	float x0, y0;
	float x1, y1;

	// height values for start coordinate (bottom = 00 ... top = 03)
	float z00, z01, z02, z03;
	// height values for end coordinate (bottom = 10 ... top = 13)
	float z10, z11, z12, z13;

	int above_bowtie_flags;
	int below_bowtie_flags;

	int pos_below_is_visible;
	int neg_below_is_visible;
	int pos_above_is_visible;
	int neg_above_is_visible;
	int pos_normal_is_visible;
	int neg_normal_is_visible;
};

/*
 * Mesh information for a quadrilateral: 2 triangles and 4 verticies.
 * For bowtied faces, only one triangle is used. ignore_triangle is set when
 * the face is a bowtie, causing one of the triangles to have an area of 0.
 */
struct mesh_face {
	struct mesh_object *mesh_obj;

	// -1 = ignore none, 0 = ignore first, 1 means ignore second
	int ignore_triangle;

	uint32_t indices[6];
	float positions[12];
	float tex_coords[8];
	float normal[3];
};

/* Mesh information for a generic polygon that lies on a plane. Normal array 
 * only needs to contain 1 normal vector (3 floats).
 */
struct mesh_poly {
	struct mesh_object *mesh_obj;

	uint32_t triangle_count;
	uint32_t vertex_count;
	uint32_t *indices;
	float *positions;
	float *tex_coords;
	float *normal;
};

/* The room is temporarily transformed into an array of mesh objects before
 * being outputted as an obj file. Each mesh object represents all the 
 * calculated vertices from walls, sector floors, and ceilings that share the
 * same texture. The z axis is treated as the vertical, so the meshes must be 
 * rotated to account for this. Texture coordinates are transposed to account 
 * for png files also being transposed.
 */
struct mesh_object {
	// texture number is used for unique identification
	uint16_t id;
	struct material material;
	// array of uint32_t
	struct dynamic_array indices;

	// arrays of floats
	struct dynamic_array positions;
	struct dynamic_array tex_coords;
	struct dynamic_array normals;
};

struct point {
	float x;
	float y;
};

struct thing {
	int16_t type;
	int16_t x_pos, y_pos;
	int16_t angle;
	int16_t when;
	int16_t x_exit_pos, y_exit_pos;
	int16_t flags;
	int16_t id;
	char comment[64];
};

struct subsector {
	uint16_t sector_number;
	uint16_t point_count;
	struct point *points;
};

struct wall {
	uint16_t pos_sidedef_num, neg_sidedef_num;
	int16_t pos_x_tex_offset, pos_y_tex_offset;
	int16_t neg_x_tex_offset, neg_y_tex_offset;
	int16_t pos_sector_num, neg_sector_num;
	int16_t x0, x1, y0, y1;
};

struct sidedef {
	uint16_t id;
	uint16_t normal_bitmap_num, above_bitmap_num, below_bitmap_num;
	uint32_t wall_flags;
	uint8_t animation_speed;
};

struct slope_data {
	float a, b, c, d;
	struct point tex_origin;
	int32_t tex_angle;
};

struct sector {
	uint16_t id;
	uint16_t floor_bitmap_num, ceiling_bitmap_num;
	uint16_t x_tex_offset, y_tex_offset;
	int32_t floor_height, ceiling_height;
	uint8_t light_level;
	uint32_t sector_flags;
	uint8_t animation_speed;
	struct slope_data floor_slope, ceiling_slope;
};

// room file information
FILE *roo_file;
int32_t room_version;
int32_t width, height; // 1024 fineness units per 1 unit

// array of struct subsector
struct dynamic_array subsectors;
uint16_t wall_count;
struct wall *walls;
uint16_t sidedef_count;
struct sidedef *sidedefs;
uint16_t sector_count;
struct sector *sectors;
uint16_t thing_count;
struct thing *things;

int16_t map_max_x = -32767;
int16_t map_max_y = -32767;
int16_t map_min_x = 32767;
int16_t map_min_y = 32767;

/*
 * Directory that holds all texture info (both png and json outputed from 
 * bgf2png), subdirectories are not searched. Wall textures must be transposed.
 * All .json files must be named grd#####.json, where "#####" is a number in 
 * the range [1 - 65535]. Every texture must have a .json & .png file.
 */
char *texture_dir;

// array of struct mesh_object
struct dynamic_array mesh_objects;

void set_material_info(char *json_file_path, struct material *mat)
{
	json_error_t error;
	int error_type = ET_NO_ERROR;
	json_t *root = NULL;
	json_t *shrink_factor = NULL;
	json_t *sprites = NULL;
	json_t *texture = NULL;
	json_t *file_name = NULL;
	json_t *width = NULL;
	json_t *height = NULL;

	root = json_load_file(json_file_path, 0, &error);
	if (root == NULL) {
		if (error.line == -1)
			error_type = ET_LOAD;
		else
			error_type = ET_SYNTAX;
		goto invalid;
	}

	if (!json_is_object(root)) {
		error_type = ET_INVALID_DATA;
		goto invalid;
	}

	shrink_factor = json_object_get(root, "shrink_factor");
	file_name = json_object_get(root, "image_file");
	sprites = json_object_get(root, "sprites");

	if (!json_is_integer(shrink_factor) || !json_is_array(sprites)) {
		error_type = ET_INVALID_DATA;
		goto invalid;
	}

	if (json_array_size(sprites) < 1) {
		error_type = ET_INVALID_DATA;
		goto invalid;
	}

	texture = json_array_get(sprites, 0);

	if (!json_is_object(texture)) {
		error_type = ET_INVALID_DATA;
		goto invalid;
	}

	width = json_object_get(texture, "width");
	height = json_object_get(texture, "height");

	if (!json_is_string(file_name) || !json_is_integer(width) ||
	    !json_is_integer(height)) {
		error_type = ET_INVALID_DATA;
		goto invalid;
	}

	int temp_shrink_factor = json_integer_value(shrink_factor);
	int temp_width = json_integer_value(width);
	int temp_height = json_integer_value(height);

	if (temp_width <= 0 || temp_height <= 0 || temp_shrink_factor <= 0) {
		error_type = ET_INVALID_DATA;
		goto invalid;
	}

	mat->shrink_factor = temp_shrink_factor;
	mat->tex_width = temp_width;
	mat->tex_height = temp_height;

	const char *temp_str = json_string_value(file_name);
	mat->texture_file_path = malloc(strlen(temp_str) + 1);
	strcpy(mat->texture_file_path, temp_str);

	mat->is_valid = 1;
	json_decref(root);
	return;

invalid:
	fprintf(stderr, "Failed to load JSON file: %s\n", json_file_path);
	switch (error_type) {
	case ET_LOAD:
		fprintf(stderr, "Error: %s\n", error.text);
		break;
	case ET_SYNTAX:
		fprintf(stderr, "Syntax Error on line: %d: %s\n", error.line,
			error.text);
		break;
	case ET_INVALID_DATA:
		fprintf(stderr, "Data Error: Invalid texture data\n");
		break;
	default:
		break;
	}
	mat->is_valid = 0;
	json_decref(root);
}

// NOTE: not sure what to do when there is more than 1 bitmap for texture_number
char *get_json_file_path(uint16_t texture_number)
{
	// allocate enough space for <texture_dir>/grd#####.json
	char *file_path = malloc(strlen(texture_dir) + 15);
	sprintf(file_path, "%s/grd%05u.json", texture_dir, texture_number);
	return file_path;
}

void dynamic_array_init(struct dynamic_array *arr, size_t capacity, size_t size)
{
	arr->data = malloc(size * capacity);
	arr->data_size = size;
	arr->length = 0;
	arr->capacity = capacity;
}

void dynamic_array_grow(struct dynamic_array *arr)
{
	arr->capacity *= 2;
	arr->data = realloc(arr->data, arr->capacity * arr->data_size);
}

void *dynamic_array_get(struct dynamic_array *arr, int index)
{
	return (uint8_t *)(arr->data) + arr->data_size * index;
}

void *dynamic_array_get_next(struct dynamic_array *arr)
{
	if (arr->length + 1 > arr->capacity)
		dynamic_array_grow(arr);

	void *next = dynamic_array_get(arr, arr->length);
	arr->length++;
	return next;
}

void mesh_object_init(struct mesh_object *mesh_obj, uint16_t texture_number)
{
	mesh_obj->id = texture_number;

	char *json_file_path = get_json_file_path(texture_number);
	set_material_info(json_file_path, &mesh_obj->material);
	free(json_file_path);

	// initial capacities allow for 1 face (2 triangles, 4 verticies)
	dynamic_array_init(&mesh_obj->indices, 6, sizeof(uint32_t));
	dynamic_array_init(&mesh_obj->positions, 12 * 3, sizeof(float));
	dynamic_array_init(&mesh_obj->tex_coords, 12 * 2, sizeof(float));
	dynamic_array_init(&mesh_obj->normals, 12 * 3, sizeof(float));
}

struct mesh_object *get_mesh_object(uint16_t texture_number)
{
	for (int i = 0; i < mesh_objects.length; i++) {
		struct mesh_object *mesh_obj;
		mesh_obj = dynamic_array_get(&mesh_objects, i);
		if (mesh_obj->id == texture_number)
			return mesh_obj;
	}

	struct mesh_object *mesh_obj = dynamic_array_get_next(&mesh_objects);
	mesh_object_init(mesh_obj, texture_number);
	return mesh_obj;
}

void mesh_object_add_poly(struct mesh_poly *mesh_poly)
{
	struct mesh_object *mesh_obj = mesh_poly->mesh_obj;

	int next_vertex = mesh_obj->positions.length / 3;

	for (int i = 0; i < mesh_poly->triangle_count * 3; i++) {
		// add indices
		uint32_t *index = dynamic_array_get_next(&mesh_obj->indices);
		*index = mesh_poly->indices[i] + next_vertex;
	}

	for (int i = 0; i < mesh_poly->vertex_count; i++) {
		// add positions
		float *pos_x = dynamic_array_get_next(&mesh_obj->positions);
		*pos_x = mesh_poly->positions[i * 3 + 0];
		float *pos_y = dynamic_array_get_next(&mesh_obj->positions);
		*pos_y = mesh_poly->positions[i * 3 + 1];
		float *pos_z = dynamic_array_get_next(&mesh_obj->positions);
		*pos_z = mesh_poly->positions[i * 3 + 2];

		// add tex coords
		float *tex_x = dynamic_array_get_next(&mesh_obj->tex_coords);
		*tex_x = mesh_poly->tex_coords[i * 2 + 0];
		float *tex_y = dynamic_array_get_next(&mesh_obj->tex_coords);
		*tex_y = mesh_poly->tex_coords[i * 2 + 1];

		// add normals
		float *norm_x = dynamic_array_get_next(&mesh_obj->normals);
		*norm_x = mesh_poly->normal[0];
		float *norm_y = dynamic_array_get_next(&mesh_obj->normals);
		*norm_y = mesh_poly->normal[1];
		float *norm_z = dynamic_array_get_next(&mesh_obj->normals);
		*norm_z = mesh_poly->normal[2];
	}
}

void mesh_object_add_face(struct mesh_face *mesh_face)
{
	struct mesh_object *mesh_obj = mesh_face->mesh_obj;
	int next_vertex = mesh_obj->positions.length / 3;

	int ignore_vertex = -1;

	if (mesh_face->ignore_triangle == 0)
		ignore_vertex = 1;

	if (mesh_face->ignore_triangle == 1)
		ignore_vertex = 3;

	// add indices
	if (mesh_face->ignore_triangle != -1) {
		uint32_t *index = dynamic_array_get_next(&mesh_obj->indices);
		*index = 0 + next_vertex;
		index = dynamic_array_get_next(&mesh_obj->indices);
		*index = 1 + next_vertex;
		index = dynamic_array_get_next(&mesh_obj->indices);
		*index = 2 + next_vertex;
	} else {
		for (int i = 0; i < 6; i++) {
			uint32_t *index;
			index = dynamic_array_get_next(&mesh_obj->indices);
			*index = mesh_face->indices[i] + next_vertex;
		}
	}

	for (int i = 0; i < 4; i++) {
		if (i == ignore_vertex)
			continue;
		// add positions
		float *pos_x = dynamic_array_get_next(&mesh_obj->positions);
		*pos_x = mesh_face->positions[i * 3 + 0];
		float *pos_y = dynamic_array_get_next(&mesh_obj->positions);
		*pos_y = mesh_face->positions[i * 3 + 1];
		float *pos_z = dynamic_array_get_next(&mesh_obj->positions);
		*pos_z = mesh_face->positions[i * 3 + 2];

		// add tex coords
		float *tex_x = dynamic_array_get_next(&mesh_obj->tex_coords);
		*tex_x = mesh_face->tex_coords[i * 2 + 0];
		float *tex_y = dynamic_array_get_next(&mesh_obj->tex_coords);
		*tex_y = mesh_face->tex_coords[i * 2 + 1];

		// add normals
		float *norm_x = dynamic_array_get_next(&mesh_obj->normals);
		*norm_x = mesh_face->normal[0];
		float *norm_y = dynamic_array_get_next(&mesh_obj->normals);
		*norm_y = mesh_face->normal[1];
		float *norm_z = dynamic_array_get_next(&mesh_obj->normals);
		*norm_z = mesh_face->normal[2];
	}
}

void set_room_bounds()
{
	if (thing_count >= 2) {
		int min_thing_x = things[0].x_pos > things[1].x_pos;
		int min_thing_y = things[0].y_pos > things[1].y_pos;
		int max_thing_x = things[0].x_pos < things[1].x_pos;
		int max_thing_y = things[0].y_pos < things[1].y_pos;

		map_min_x = things[min_thing_x].x_pos;
		map_min_y = things[min_thing_y].y_pos;
		map_max_x = things[max_thing_x].x_pos;
		map_max_y = things[max_thing_y].y_pos;
		return;
	}

	for (int i = 0; i < wall_count; i++) {
		struct wall *wall = walls + i;

		if (wall->x0 < map_min_x)
			map_min_x = wall->x0;
		if (wall->y0 < map_min_y)
			map_min_y = wall->y0;

		if (wall->x1 < map_min_x)
			map_min_x = wall->x1;
		if (wall->y1 < map_min_y)
			map_min_y = wall->y1;

		if (wall->x0 > map_max_x)
			map_max_x = wall->x0;
		if (wall->y0 > map_max_y)
			map_max_y = wall->y0;

		if (wall->x1 > map_max_x)
			map_max_x = wall->x1;
		if (wall->y1 > map_max_y)
			map_max_y = wall->y1;
	}
}

// Interpret the 4-byte buffer as a float or int, depending on room_version
float read_value(int32_t buf)
{
	if (room_version < 13)
		return (float)buf;
	return *((float *)&buf);
}

int load_things(FILE *roo_file)
{
	int32_t temp;
	if (fread(&thing_count, 2, 1, roo_file) < 1)
		return -1;

	things = calloc(thing_count, sizeof(struct thing));

	if (thing_count <= 2) {
		for (int i = 0; i < thing_count; i++) {
			struct thing *thing = things + i;
			if (fread(&temp, 4, 1, roo_file) < 1)
				return -1;
			thing->x_pos = temp;

			if (fread(&temp, 4, 1, roo_file) < 1)
				return -1;
			thing->y_pos = temp;

			// printf("Thing: %d\n", i);
			// printf("pos: (%d, %d)\n", thing->x_pos, thing->y_pos);
		}
		return 0;
	}

	for (int i = 0; i < thing_count; i++) {
		struct thing *thing = things + i;
		if (fread(&temp, 4, 1, roo_file) < 1)
			return -1;
		thing->type = temp;
		if (fread(&temp, 4, 1, roo_file) < 1)
			return -1;
		thing->angle = temp;
		if (fread(&temp, 4, 1, roo_file) < 1)
			return -1;
		thing->x_pos = temp;
		if (fread(&temp, 4, 1, roo_file) < 1)
			return -1;
		thing->y_pos = temp;
		if (fread(&temp, 4, 1, roo_file) < 1)
			return -1;
		thing->when = temp;
		if (fread(&temp, 4, 1, roo_file) < 1)
			return -1;
		thing->x_exit_pos = temp;
		if (fread(&temp, 4, 1, roo_file) < 1)
			return -1;
		thing->y_exit_pos = temp;
		if (fread(&temp, 4, 1, roo_file) < 1)
			return -1;
		thing->flags = temp;
		if (fread(&thing->comment, 64, 1, roo_file) < 1)
			return -1;

		// printf("Thing: %d\n", i);
		// printf("type: %d\n", thing->type);
		// printf("type: %d\n", thing->angle);
		// printf("pos: (%d, %d)\n", thing->x_pos, thing->y_pos);
		// printf("type: %d\n", thing->when);
		// printf("pos: (%d, %d)\n", thing->x_exit_pos, thing->y_exit_pos);
		// printf("type: %d\n", thing->flags);
	}
	return 0;
}

// return 0 on success, -1 on error
int load_subsector_points(FILE *roo_file)
{
	uint16_t node_count;
	if (fread(&node_count, 2, 1, roo_file) < 1)
		return -1;

	dynamic_array_init(&subsectors, node_count / 2,
			   sizeof(struct subsector));

	for (int i = 0; i < node_count; i++) {
		uint8_t type;
		if (fread(&type, 1, 1, roo_file) < 1)
			return -1;

		if (type == 1) {
			// skip box (16 bytes) and internal node (18 bytes)
			if (fseek(roo_file, 34, SEEK_CUR))
				return -1;
			continue;
		}

		if (type != 2) {
			return -1;
		}

		// skip box info
		fseek(roo_file, 16, SEEK_CUR);

		struct subsector *s;
		s = dynamic_array_get_next(&subsectors);
		if (fread(&s->sector_number, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&s->point_count, 2, 1, roo_file) < 1)
			return -1;
		s->points = malloc(sizeof(struct point) * s->point_count);

		for (int j = 0; j < s->point_count; j++) {
			int32_t buf;
			if (fread(&buf, 4, 1, roo_file) < 1)
				return -1;
			s->points[j].x = read_value(buf);
			if (fread(&buf, 4, 1, roo_file) < 1)
				return -1;
			s->points[j].y = read_value(buf);

			// if (s->sector_number == 70)
			// 	printf("%f %f\n", s->points[j].x, s->points[j].y);
		}
	}
	return 0;
}

// return 0 on success, -1 on error
int load_walls(FILE *roo_file)
{
	if (fread(&wall_count, 2, 1, roo_file) < 1)
		return -1;
	walls = malloc(sizeof(struct wall) * wall_count);

	for (int i = 0; i < wall_count; i++) {
		struct wall *wall = walls + i;

		if (fread(&wall->pos_sidedef_num, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&wall->neg_sidedef_num, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&wall->pos_x_tex_offset, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&wall->neg_x_tex_offset, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&wall->pos_y_tex_offset, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&wall->neg_y_tex_offset, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&wall->pos_sector_num, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&wall->neg_sector_num, 2, 1, roo_file) < 1)
			return -1;

		int32_t buf;
		if (fread(&buf, 4, 1, roo_file) < 1)
			return -1;
		wall->x0 = buf;
		if (fread(&buf, 4, 1, roo_file) < 1)
			return -1;
		wall->y0 = buf;

		if (fread(&buf, 4, 1, roo_file) < 1)
			return -1;
		wall->x1 = buf;
		if (fread(&buf, 4, 1, roo_file) < 1)
			return -1;
		wall->y1 = buf;

		// printf("WALL %d\n", i);
		// printf("pos_sidedef_num: %d\n", wall->pos_sidedef_num);
		// printf("neg_sidedef_num: %d\n", wall->neg_sidedef_num);
		// printf("pos_x_tex_offset: %d\n", wall->pos_x_tex_offset);
		// printf("neg_x_tex_offset: %d\n", wall->neg_x_tex_offset);
		// printf("pos_y_tex_offset: %d\n", wall->pos_y_tex_offset);
		// printf("neg_y_tex_offset: %d\n", wall->neg_y_tex_offset);
		// printf("pos_sector_num: %d\n", wall->pos_sector_num);
		// printf("neg_sector_num: %d\n", wall->neg_sector_num);
		// printf("start: (%d, %d)\n", wall->x0, wall->y0);
		// printf("end: (%d, %d)\n", wall->x1, wall->y1);
	}
	return 0;
}

// return 0 on success, -1 on error
int load_sidedefs(FILE *roo_file)
{
	if (fread(&sidedef_count, 2, 1, roo_file) < 1)
		return -1;
	sidedefs = malloc(sizeof(struct sidedef) * sidedef_count);

	for (int i = 0; i < sidedef_count; i++) {
		struct sidedef *sidedef = sidedefs + i;

		if (fread(&sidedef->id, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&sidedef->normal_bitmap_num, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&sidedef->above_bitmap_num, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&sidedef->below_bitmap_num, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&sidedef->wall_flags, 4, 1, roo_file) < 1)
			return -1;
		if (fread(&sidedef->animation_speed, 1, 1, roo_file) < 1)
			return -1;

		// printf("id: %d\n", sidedef->id);
		// printf("normal_bitmap_num: %d\n", sidedef->normal_bitmap_num);
		// printf("above_bitmap_num: %d\n", sidedef->above_bitmap_num);
		// printf("below_bitmap_num: %d\n", sidedef->below_bitmap_num);
		// printf("wall_flags: %d\n", sidedef->wall_flags);
		// printf("animation_speed: %d\n", sidedef->animation_speed);
	}
	return 0;
}

int load_slope_data(FILE *roo_file, struct slope_data *slope)
{
	int32_t buf;

	if (fread(&buf, 4, 1, roo_file) < 1)
		return -1;
	slope->a = read_value(buf);

	if (fread(&buf, 4, 1, roo_file) < 1)
		return -1;
	slope->b = read_value(buf);

	if (fread(&buf, 4, 1, roo_file) < 1)
		return -1;
	slope->c = read_value(buf);

	if (fread(&buf, 4, 1, roo_file) < 1)
		return -1;
	slope->d = read_value(buf);

	if (fread(&buf, 4, 1, roo_file) < 1)
		return -1;
	slope->tex_origin.x = read_value(buf);

	if (fread(&buf, 4, 1, roo_file) < 1)
		return -1;
	slope->tex_origin.y = read_value(buf);

	if (fread(&slope->tex_angle, 4, 1, roo_file) < 1)
		return -1;

	// skip vertex numbers (3 pairs of (x, y, z), 2 bytes per number)
	if (fseek(roo_file, 18, SEEK_CUR))
		return -1;

	// printf("a: %f\n", slope->a);
	// printf("b: %f\n", slope->b);
	// printf("c: %f\n", slope->c);
	// printf("d: %f\n", slope->d);
	// printf("tex_origin: (%f, %f)\n", slope->tex_origin.x, slope->tex_origin.y);
	// printf("tex_angle: %d\n", slope->tex_angle);

	return 0;
}

// return 0 on success, -1 on error
int load_sectors(FILE *roo_file)
{
	uint16_t temp;
	if (fread(&sector_count, 2, 1, roo_file) < 1)
		return -1;
	sectors = malloc(sizeof(struct sector) * sector_count);

	for (int i = 0; i < sector_count; i++) {
		struct sector *sector = sectors + i;

		if (fread(&sector->id, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&sector->floor_bitmap_num, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&sector->ceiling_bitmap_num, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&sector->x_tex_offset, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&sector->y_tex_offset, 2, 1, roo_file) < 1)
			return -1;
		if (fread(&temp, 2, 1, roo_file) < 1)
			return -1;
		sector->floor_height = temp * BLAK_FACTOR;
		if (fread(&temp, 2, 1, roo_file) < 1)
			return -1;
		sector->ceiling_height = temp * BLAK_FACTOR;
		if (fread(&sector->light_level, 1, 1, roo_file) < 1)
			return -1;
		if (fread(&sector->sector_flags, 4, 1, roo_file) < 1)
			return -1;

		if (room_version >= 10) {
			if (fread(&sector->animation_speed, 1, 1, roo_file) < 1)
				return -1;
		} else {
			sector->animation_speed = 0;
		}

		// printf("Sector: %d\n", i);
		// printf("id: %d\n", sector->id);
		// printf("floor_bitmap_num: %d\n", sector->floor_bitmap_num);
		// printf("ceiling_bitmap_num: %d\n", sector->ceiling_bitmap_num);
		// printf("x_tex_offset: %d\n", sector->x_tex_offset);
		// printf("y_tex_offset: %d\n", sector->y_tex_offset);
		// printf("floor_height: %d\n", sector->floor_height);
		// printf("ceiling_height: %d\n", sector->ceiling_height);
		// printf("light_level: %d\n", sector->light_level);
		// printf("sector_flags: %d\n", sector->sector_flags);
		// printf("animation_speed: %d\n", sector->animation_speed);

		if (sector->sector_flags & SF_SLOPED_FLOOR) {
			if (load_slope_data(roo_file, &sector->floor_slope))
				return -1;
		}

		if (sector->sector_flags & SF_SLOPED_CEILING) {
			if (load_slope_data(roo_file, &sector->ceiling_slope))
				return -1;
		}
	}
	return 0;
}

// return 0 on success, -1 on error
int load_room(FILE *roo_file)
{
	int32_t temp;
	int32_t node_pos, wall_pos, sidedef_pos, sector_pos, things_pos;

	// check magic bytes
	char magic[4] = { 0x52, 0x4f, 0x4f, 0xb1 };
	for (int i = 0; i < 4; i++) {
		char byte;
		int read_error = fread(&byte, 1, 1, roo_file) < 1;
		int magic_error = byte != magic[i];
		if (read_error || magic_error)
			return -1;
	}

	// check room version
	int read_error = fread(&room_version, 4, 1, roo_file) < 1;
	int version_error = room_version < ROO_VERSION;
	if (read_error || version_error)
		return -1;

	// skip security number
	if (fread(&temp, 4, 1, roo_file) < 1)
		return -1;

	// load main info subsection location
	if (fread(&temp, 4, 1, roo_file) < 1)
		return -1;

	// jump to main info subsection
	if (fseek(roo_file, temp, SEEK_SET))
		return -1;

	if (fread(&width, 4, 1, roo_file) < 1 ||
	    fread(&height, 4, 1, roo_file) < 1)
		return -1;

	// load subsection locations
	if (fread(&node_pos, 4, 1, roo_file) < 1 ||
	    fread(&temp, 4, 1, roo_file) < 1 ||
	    fread(&wall_pos, 4, 1, roo_file) < 1 ||
	    fread(&sidedef_pos, 4, 1, roo_file) < 1 ||
	    fread(&sector_pos, 4, 1, roo_file) < 1 ||
	    fread(&things_pos, 4, 1, roo_file) < 1)
		return -1;

	// load leaf subsector points
	if (fseek(roo_file, node_pos, SEEK_SET) ||
	    load_subsector_points(roo_file))
		return -1;

	// load walls
	if (fseek(roo_file, wall_pos, SEEK_SET) || load_walls(roo_file))
		return -1;

	// load sidedefs
	if (fseek(roo_file, sidedef_pos, SEEK_SET) || load_sidedefs(roo_file))
		return -1;

	// load sectors
	if (fseek(roo_file, sector_pos, SEEK_SET) || load_sectors(roo_file))
		return -1;

	// load things
	if (fseek(roo_file, things_pos, SEEK_SET) || load_things(roo_file))
		return -1;

	set_room_bounds();

	// printf("Room Version: %d\n", room_version);
	// printf("Room width: %d, height: %d\n", width, height);
	return 0;
}

float get_floor_height(struct sector *sector, float x, float y)
{
	if (!(sector->sector_flags & SF_SLOPED_FLOOR))
		return sector->floor_height;

	struct slope_data *slope = &sector->floor_slope;
	return roundf((-slope->a * x - slope->b * y - slope->d) / slope->c);
}

float get_ceiling_height(struct sector *sector, float x, float y)
{
	if (!(sector->sector_flags & SF_SLOPED_CEILING))
		return sector->ceiling_height;

	struct slope_data *slope = &sector->ceiling_slope;
	return roundf((-slope->a * x - slope->b * y - slope->d) / slope->c);
}

// sets wall heights and bowtie flags
void set_wall_heights(struct wall_3d *wall_3d)
{
	// get local copy for readability
	float x0 = wall_3d->x0;
	float y0 = wall_3d->y0;
	float x1 = wall_3d->x1;
	float y1 = wall_3d->y1;

	// if no sectors, give defaults
	if (!wall_3d->pos_sector && !wall_3d->neg_sector) {
		wall_3d->z00 = wall_3d->z01 = 0;
		wall_3d->z02 = wall_3d->z03 = FINENESS;
		wall_3d->z10 = wall_3d->z11 = 0;
		wall_3d->z12 = wall_3d->z13 = FINENESS;
		return;
	}

	// if there is only one sector, use the other to calculate heights
	if (!wall_3d->pos_sector || !wall_3d->neg_sector) {
		int index = wall_3d->pos_sector == NULL ? 1 : 0;
		struct sector *pos_neg[2] = { wall_3d->pos_sector,
					      wall_3d->neg_sector };
		float bottom_start = get_floor_height(pos_neg[index], x0, y0);
		float top_start = get_ceiling_height(pos_neg[index], x0, y0);
		float bottom_end = get_floor_height(pos_neg[index], x1, y1);
		float top_end = get_ceiling_height(pos_neg[index], x1, y1);
		wall_3d->z00 = wall_3d->z01 = bottom_start;
		wall_3d->z02 = wall_3d->z03 = top_start;
		wall_3d->z10 = wall_3d->z11 = bottom_end;
		wall_3d->z12 = wall_3d->z13 = top_end;
		return;
	}

	// get heights for bottom part of wall
	float pos_z0 = get_floor_height(wall_3d->pos_sector, x0, y0);
	float pos_z1 = get_floor_height(wall_3d->pos_sector, x1, y1);
	float neg_z0 = get_floor_height(wall_3d->neg_sector, x0, y0);
	float neg_z1 = get_floor_height(wall_3d->neg_sector, x1, y1);

	// positive sector floor is above negative at start
	if (pos_z0 > neg_z0) {
		if (pos_z1 >= neg_z1) {
			// normal wall: positive is higher at both ends
			wall_3d->below_bowtie_flags = BT_NONE;

			wall_3d->z00 = neg_z0;
			wall_3d->z10 = neg_z1;
			wall_3d->z01 = pos_z0;
			wall_3d->z11 = pos_z1;
		} else {
			// bowtie: positive starts above, ends below
			wall_3d->below_bowtie_flags = BT_POS;

			wall_3d->z00 = neg_z0;
			wall_3d->z10 = pos_z1;
			wall_3d->z01 = pos_z0;
			wall_3d->z11 = neg_z1;
		}
	} else {
		// negative sector floor is above positive at start
		if (neg_z1 >= pos_z1) {
			// normal wall: negative is higher at both ends
			wall_3d->below_bowtie_flags = BT_NONE;

			wall_3d->z00 = pos_z0;
			wall_3d->z10 = pos_z1;
			wall_3d->z01 = neg_z0;
			wall_3d->z11 = neg_z1;
		} else {
			// bowtie: negative starts above, ends below
			wall_3d->below_bowtie_flags = BT_NEG;

			wall_3d->z00 = pos_z0;
			wall_3d->z10 = neg_z1;
			wall_3d->z01 = neg_z0;
			wall_3d->z11 = pos_z1;
		}
	}

	// get heights for top part of wall
	pos_z0 = get_ceiling_height(wall_3d->pos_sector, x0, y0);
	pos_z1 = get_ceiling_height(wall_3d->pos_sector, x1, y1);
	neg_z0 = get_ceiling_height(wall_3d->neg_sector, x0, y0);
	neg_z1 = get_ceiling_height(wall_3d->neg_sector, x1, y1);

	// positive sector ceiling is above negative at start
	if (pos_z0 > neg_z0) {
		if (pos_z1 >= neg_z1) {
			// normal wall: positive is higher at both ends
			wall_3d->above_bowtie_flags = BT_NONE;

			wall_3d->z02 = neg_z0;
			wall_3d->z12 = neg_z1;
			wall_3d->z03 = pos_z0;
			wall_3d->z13 = pos_z1;
		} else {
			// bowtie: positive starts above, ends below
			wall_3d->above_bowtie_flags = BT_POS;

			wall_3d->z02 = neg_z0;
			wall_3d->z12 = pos_z1;
			wall_3d->z03 = pos_z0;
			wall_3d->z13 = neg_z1;
		}
	} else {
		// negative sector ceiling is above positive at start
		if (neg_z1 >= pos_z1) {
			// normal wall: negative is higher at both ends
			wall_3d->above_bowtie_flags = BT_NONE;

			wall_3d->z02 = pos_z0;
			wall_3d->z12 = pos_z1;
			wall_3d->z03 = neg_z0;
			wall_3d->z13 = neg_z1;
		} else {
			// bowtie: negative starts above, ends below
			wall_3d->above_bowtie_flags = BT_NEG;

			wall_3d->z02 = pos_z0;
			wall_3d->z12 = neg_z1;
			wall_3d->z03 = neg_z0;
			wall_3d->z13 = pos_z1;
		}
	}
}

// converts a wall to wall_3d
void transform_wall(struct wall *wall, struct wall_3d *wall_3d)
{
	struct sidedef *pos_sidedef;
	struct sidedef *neg_sidedef;

	struct sector *pos_sector;
	struct sector *neg_sector;

	// get local reference to sidedefs
	if (wall->pos_sidedef_num == 0)
		pos_sidedef = NULL;
	else
		pos_sidedef = &sidedefs[wall->pos_sidedef_num - 1];

	if (wall->neg_sidedef_num == 0)
		neg_sidedef = NULL;
	else
		neg_sidedef = &sidedefs[wall->neg_sidedef_num - 1];

	// get local references to sectors
	if (wall->pos_sector_num == -1)
		pos_sector = NULL;
	else
		pos_sector = &sectors[wall->pos_sector_num];

	if (wall->neg_sector_num == -1)
		neg_sector = NULL;
	else
		neg_sector = &sectors[wall->neg_sector_num];

	// convert wall start and end coordinates to client units
	wall_3d->x0 = (wall->x0 - map_min_x) * BLAK_FACTOR;
	wall_3d->y0 = (map_max_y - wall->y0) * BLAK_FACTOR;
	wall_3d->x1 = (wall->x1 - map_min_x) * BLAK_FACTOR;
	wall_3d->y1 = (map_max_y - wall->y1) * BLAK_FACTOR;

	wall_3d->pos_sector = pos_sector;
	wall_3d->neg_sector = neg_sector;
	wall_3d->pos_sidedef = pos_sidedef;
	wall_3d->neg_sidedef = neg_sidedef;

	wall_3d->pos_x_offset = wall->pos_x_tex_offset;
	wall_3d->pos_y_offset = wall->pos_y_tex_offset;
	wall_3d->neg_x_offset = wall->neg_x_tex_offset;
	wall_3d->neg_y_offset = wall->neg_y_tex_offset;

	set_wall_heights(wall_3d);

	// get local references for better readability
	float z00 = wall_3d->z00;
	float z01 = wall_3d->z01;
	float z02 = wall_3d->z02;
	float z03 = wall_3d->z03;
	float z10 = wall_3d->z10;
	float z11 = wall_3d->z11;
	float z12 = wall_3d->z12;
	float z13 = wall_3d->z13;

	// assume faces aren't visible
	wall_3d->pos_below_is_visible = 0;
	wall_3d->pos_above_is_visible = 0;
	wall_3d->pos_normal_is_visible = 0;
	wall_3d->neg_below_is_visible = 0;
	wall_3d->neg_above_is_visible = 0;
	wall_3d->neg_normal_is_visible = 0;

	// determine visible faces (bitmap exists & face height != 0)
	if (pos_sidedef) {
		int below_bitmap_exists = pos_sidedef->below_bitmap_num != 0;
		int above_bitmap_exists = pos_sidedef->above_bitmap_num != 0;
		int normal_bitmap_exists = pos_sidedef->normal_bitmap_num != 0;

		if (below_bitmap_exists && (z00 != z01 || z10 != z11))
			wall_3d->pos_below_is_visible = 1;

		if (above_bitmap_exists && (z02 != z03 || z12 != z13))
			wall_3d->pos_above_is_visible = 1;

		if (normal_bitmap_exists && (z01 != z02 || z11 != z12))
			wall_3d->pos_normal_is_visible = 1;
	}

	if (neg_sidedef) {
		int below_bitmap_exists = neg_sidedef->below_bitmap_num != 0;
		int above_bitmap_exists = neg_sidedef->above_bitmap_num != 0;
		int normal_bitmap_exists = neg_sidedef->normal_bitmap_num != 0;

		if (below_bitmap_exists && (z00 != z01 || z10 != z11))
			wall_3d->neg_below_is_visible = 1;

		if (above_bitmap_exists && (z02 != z03 || z12 != z13))
			wall_3d->neg_above_is_visible = 1;

		if (normal_bitmap_exists && (z01 != z02 || z11 != z12))
			wall_3d->neg_normal_is_visible = 1;
	}
}

// face int is face type, side is pos or neg, face struct holds mesh info
void meshify_wall_face(struct wall *wall, struct wall_3d *wall_3d, int side,
		       int face, struct mesh_face *out)
{
	struct sidedef *sidedef;
	uint16_t bitmap_num = 0;

	if (side == SD_POS)
		sidedef = wall_3d->pos_sidedef;
	else
		sidedef = wall_3d->neg_sidedef;

	if (face == FC_BELOW)
		bitmap_num = sidedef->below_bitmap_num;
	else if (face == FC_ABOVE)
		bitmap_num = sidedef->above_bitmap_num;
	else
		bitmap_num = sidedef->normal_bitmap_num;

	// mesh object that will be set for face
	struct mesh_object *mesh_obj = get_mesh_object(bitmap_num);

	out->mesh_obj = mesh_obj;

	struct material *mat = &mesh_obj->material;

	// currently meshifying positive below face
	uint32_t *indices = out->indices;
	float *positions = out->positions;
	float *tex_coords = out->tex_coords;
	float *normal = out->normal;

	// texture mapping flags
	int flip_h = sidedef->wall_flags & WF_BACKWARDS;
	int top_down = 0;
	int no_v_tile = sidedef->wall_flags & WF_NO_VTILE;

	if ((face == FC_BELOW && (sidedef->wall_flags & WF_BELOW_TOPDOWN)) ||
	    (face == FC_NORMAL && (sidedef->wall_flags & WF_NORMAL_TOPDOWN)) ||
	    (face == FC_ABOVE && !(sidedef->wall_flags & WF_ABOVE_BOTTOMUP))) {
		top_down = 1;
	}

	// default to below wall
	float z00 = wall_3d->z00;
	float z01 = wall_3d->z01;
	float z10 = wall_3d->z10;
	float z11 = wall_3d->z11;

	if (face == FC_NORMAL) {
		z00 = wall_3d->z01;
		z01 = wall_3d->z02;
		z10 = wall_3d->z11;
		z11 = wall_3d->z12;
	} else if (face == FC_ABOVE) {
		z00 = wall_3d->z02;
		z01 = wall_3d->z03;
		z10 = wall_3d->z12;
		z11 = wall_3d->z13;
	}

	// default to no bowtie
	out->ignore_triangle = -1;

	if (face == FC_BELOW) {
		// below bowtie correction
		if (wall_3d->below_bowtie_flags == BT_POS) {
			if (side == SD_POS) {
				z01 = z00;
				out->ignore_triangle = 1;
			} else {
				z11 = z10;
				out->ignore_triangle = 0;
			}
		} else if (wall_3d->below_bowtie_flags == BT_NEG) {
			if (side == SD_POS) {
				z11 = z10;
				out->ignore_triangle = 0;
			} else {
				z01 = z00;
				out->ignore_triangle = 1;
			}
		}
	} else if (face == FC_ABOVE) {
		// above bowtie correction
		if (wall_3d->above_bowtie_flags == BT_POS) {
			if (side == SD_POS) {
				z10 = z11;
				out->ignore_triangle = 0;
			} else {
				z00 = z01;
				out->ignore_triangle = 1;
			}
		} else if (wall_3d->above_bowtie_flags == BT_NEG) {
			if (side == SD_POS) {
				z00 = z01;
				out->ignore_triangle = 1;
			} else {
				z10 = z11;
				out->ignore_triangle = 0;
			}
		}
	} else {
		// normal bowtie adjustments
		if (wall_3d->below_bowtie_flags == BT_POS) {
			if (side == SD_POS) {
				z00 = wall_3d->z00;
			} else {
				z10 = wall_3d->z10;
			}
		} else if (wall_3d->below_bowtie_flags == BT_NEG) {
			if (side == SD_POS) {
				z10 = wall_3d->z10;
			} else {
				z00 = wall_3d->z00;
			}
		}

		if (wall_3d->above_bowtie_flags == BT_POS) {
			if (side == SD_POS) {
				z11 = wall_3d->z13;
			} else {
				z01 = wall_3d->z03;
			}
		} else if (wall_3d->above_bowtie_flags == BT_NEG) {
			if (side == SD_POS) {
				z01 = wall_3d->z03;
			} else {
				z11 = wall_3d->z13;
			}
		}
	}

	if (side == SD_POS) {
		indices[0] = 0;
		indices[1] = 2;
		indices[2] = 1;
		indices[3] = 0;
		indices[4] = 3;
		indices[5] = 2;
	} else {
		indices[0] = 0;
		indices[1] = 2;
		indices[2] = 3;
		indices[3] = 0;
		indices[4] = 1;
		indices[5] = 2;
	}

	// x and y texture offsets, y_offset is needed to clip wall height in no_v_tile case
	float x_offset = 0;
	float y_offset = 0;

	if (side == SD_POS) {
		x_offset = wall_3d->pos_x_offset;
		y_offset = wall_3d->pos_y_offset;
		// reverse flipping for positive facing walls (i guess?)
		flip_h = !flip_h;
	} else {
		x_offset = wall_3d->neg_x_offset;
		y_offset = wall_3d->neg_y_offset;
	}

	// textures are transposed
	float tex_width = (float)(mat->tex_height) / mat->shrink_factor;
	float tex_height = (float)(mat->tex_width) / mat->shrink_factor;

	// clamp verticies depending on no vertical tile flag (only for normal walls)
	if (no_v_tile && face == FC_NORMAL) {
		float max_height =
			(tex_height - y_offset) / BITMAP_WIDTH * FINENESS;
		if (top_down) {
			// clamp bottom verticies
			if (z01 - z00 > max_height)
				z00 = z01 - max_height;

			if (z11 - z10 > max_height)
				z10 = z11 - max_height;
		} else {
			// clamp top verticies
			if (z01 - z00 > max_height)
				z01 = z00 + max_height;

			if (z11 - z10 > max_height)
				z11 = z10 + max_height;
		}
	}

	// top left
	positions[0] = wall_3d->x0;
	positions[1] = wall_3d->y0;
	positions[2] = z01;

	// top right
	positions[3] = wall_3d->x1;
	positions[4] = wall_3d->y1;
	positions[5] = z11;

	// bottom right
	positions[6] = wall_3d->x1;
	positions[7] = wall_3d->y1;
	positions[8] = z10;

	// bottom left
	positions[9] = wall_3d->x0;
	positions[10] = wall_3d->y0;
	positions[11] = z00;

	float btw_x = (wall_3d->x1 - wall_3d->x0);
	float btw_y = (wall_3d->y1 - wall_3d->y0);
	float btw_length = sqrt(btw_x * btw_x + btw_y * btw_y);

	normal[0] = btw_y / btw_length;
	normal[1] = -btw_x / btw_length;
	normal[2] = 0;

	if (side == SD_POS) {
		normal[0] *= -1;
		normal[1] *= -1;
	}

	if (!mat->is_valid) {
		return;
	}

	// TEXTURE MAPPING VARIABLES
	// - textures are stored transposed (90 degree rotated and flipped)
	// - textures have offsets (increasing y moves texture down, x to the left)
	// - textures can be mapped "bottom up" or "top down"
	// bottom up: texture origin (before offset) at SW of texture and wall, texture is sampled bottom-up
	// top down: texture origin (before offset) at NW of texture and wall, texture is sampled top-down
	// - above texture defaults to draw top down, normal and below default to bottom up
	// - textures can disable vertical tiling (must truncate position verticies accordingly)
	// - unit conversion is (BITMAP_WIDTH pixels = FINENESS world size)
	// - shrink factor also has to be taken into account
	// - sloped wall origins are "arbitrary"

	// fineness coordinates of texture origin (relative to left side of wall, and world floor)
	float x_origin = 0;
	float z_origin = top_down ? z01 : z00;

	// choose "arbitrary" z origin for sloped walls
	if (fabsf(z00 - z10) > 1e-5 && !top_down) {
		// bottom is sloped
		z_origin = fmin(z00, z10);
		z_origin = ceilf(z_origin / FINENESS) * FINENESS;
	}

	if (fabsf(z01 - z11) > 1e-5 && top_down) {
		// top is sloped
		z_origin = fmin(z01, z11);
		z_origin = ceilf(z_origin / FINENESS) * FINENESS;
	}

	x_origin -= x_offset / BITMAP_WIDTH * FINENESS;
	z_origin -= y_offset / BITMAP_WIDTH * FINENESS;

	float u0 = (0 - x_origin) / FINENESS * BITMAP_WIDTH / tex_width;
	float u1 =
		(btw_length - x_origin) / FINENESS * BITMAP_WIDTH / tex_width;

	// shift x coordinates around the half way point
	float test = 0.5 - (u1 + u0) / 2;
	u0 += 2 * test;
	u1 += 2 * test;

	// add 1 since bottom of texture starts at v = 1
	float v00 = (z_origin - z00) / FINENESS * BITMAP_WIDTH / tex_height + 1;
	float v01 = (z_origin - z01) / FINENESS * BITMAP_WIDTH / tex_height + 1;
	float v10 = (z_origin - z10) / FINENESS * BITMAP_WIDTH / tex_height + 1;
	float v11 = (z_origin - z11) / FINENESS * BITMAP_WIDTH / tex_height + 1;

	// uv coordinates are swapped to transpose the texture
	// top left
	tex_coords[0] = v01;
	tex_coords[1] = u0;

	// top right
	tex_coords[2] = v11;
	tex_coords[3] = u1;

	// bottom right
	tex_coords[4] = v10;
	tex_coords[5] = u1;

	// bottom left
	tex_coords[6] = v00;
	tex_coords[7] = u0;

	// flip horizontally if specified
	if (flip_h) {
		float u_temp = tex_coords[1];

		tex_coords[1] = tex_coords[3];
		tex_coords[3] = u_temp;

		u_temp = tex_coords[5];

		tex_coords[5] = tex_coords[7];
		tex_coords[7] = u_temp;
	}
}

void meshify_wall(struct wall *wall)
{
	struct wall_3d wall_3d = { 0 };
	transform_wall(wall, &wall_3d);

	struct mesh_face mesh_face;

	if (wall_3d.pos_below_is_visible) {
		meshify_wall_face(wall, &wall_3d, SD_POS, FC_BELOW, &mesh_face);
		mesh_object_add_face(&mesh_face);
	}

	if (wall_3d.pos_above_is_visible) {
		meshify_wall_face(wall, &wall_3d, SD_POS, FC_ABOVE, &mesh_face);
		mesh_object_add_face(&mesh_face);
	}

	if (wall_3d.pos_normal_is_visible) {
		meshify_wall_face(wall, &wall_3d, SD_POS, FC_NORMAL,
				  &mesh_face);
		mesh_object_add_face(&mesh_face);
	}

	if (wall_3d.neg_below_is_visible) {
		meshify_wall_face(wall, &wall_3d, SD_NEG, FC_BELOW, &mesh_face);
		mesh_object_add_face(&mesh_face);
	}

	if (wall_3d.neg_above_is_visible) {
		meshify_wall_face(wall, &wall_3d, SD_NEG, FC_ABOVE, &mesh_face);
		mesh_object_add_face(&mesh_face);
	}

	if (wall_3d.neg_normal_is_visible) {
		meshify_wall_face(wall, &wall_3d, SD_NEG, FC_NORMAL,
				  &mesh_face);
		mesh_object_add_face(&mesh_face);
	}
}

void meshify_subsector_plane(struct subsector *subsector, int is_floor,
			     struct mesh_poly *out)
{
	// set output empty by default (assume no output)
	out->mesh_obj = NULL;
	out->triangle_count = 0;
	out->vertex_count = 0;
	out->indices = NULL;
	out->positions = NULL;
	out->tex_coords = NULL;
	out->normal = NULL;

	struct sector *sector;
	uint16_t bitmap_num;

	if (subsector->sector_number == 0)
		return;

	sector = &sectors[subsector->sector_number - 1];

	if (is_floor)
		bitmap_num = sector->floor_bitmap_num;
	else
		bitmap_num = sector->ceiling_bitmap_num;

	if (bitmap_num == 0)
		return;

	struct mesh_object *mesh_obj = get_mesh_object(bitmap_num);

	out->mesh_obj = mesh_obj;
	out->vertex_count = subsector->point_count;
	out->triangle_count = subsector->point_count - 2;
	out->indices = malloc(sizeof(uint32_t) * out->triangle_count * 3);
	out->positions = malloc(sizeof(float) * out->vertex_count * 3);
	out->tex_coords = malloc(sizeof(float) * out->vertex_count * 2);
	out->normal = malloc(sizeof(float) * 3);

	// set vertex positions (convert to client units)
	for (int i = 0; i < out->vertex_count; i++) {
		struct point *sub_p = subsector->points + i;
		float *x = out->positions + (i * 3 + 0);
		float *y = out->positions + (i * 3 + 1);
		float *z = out->positions + (i * 3 + 2);
		// NOTE: not sure why i don't need to multiply by BLAK_FACTOR or offset by map_min_x/y (like i did for wall positions)
		*x = sub_p->x;
		*y = sub_p->y;
		if (is_floor)
			*z = get_floor_height(sector, *x, *y);
		else
			*z = get_ceiling_height(sector, *x, *y);
	}

	// set triangles
	for (int i = 0; i < out->triangle_count; i++) {
		if (is_floor) {
			out->indices[i * 3 + 0] = 0;
			out->indices[i * 3 + 1] = i + 2;
			out->indices[i * 3 + 2] = i + 1;
		} else {
			out->indices[i * 3 + 0] = 0;
			out->indices[i * 3 + 1] = i + 1;
			out->indices[i * 3 + 2] = i + 2;
		}
	}

	// check for slope
	struct slope_data *slope = NULL;

	if (is_floor && (sector->sector_flags & SF_SLOPED_FLOOR))
		slope = &sector->floor_slope;
	else if (!is_floor && (sector->sector_flags & SF_SLOPED_CEILING))
		slope = &sector->ceiling_slope;

	// set normal vector
	out->normal[0] = slope ? slope->a : 0;
	out->normal[1] = slope ? slope->b : 0;
	out->normal[2] = slope ? slope->c : 1;

	if (!is_floor) {
		out->normal[0] *= -1;
		out->normal[1] *= -1;
		out->normal[2] *= -1;
	}

	// SET VERTEX TEXTURE COORDINATES (only if valid texture)
	if (!mesh_obj->material.is_valid)
		return;

	float tex_width = mesh_obj->material.tex_width;
	float tex_height = mesh_obj->material.tex_height;

	// get texture pixel offset
	float u_offset = sector->x_tex_offset / tex_width;
	float v_offset = sector->y_tex_offset / tex_height;

	// set default texture orientation (no slope)
	float x_origin = slope ? slope->tex_origin.x : 0;
	float y_origin = slope ? slope->tex_origin.y : 0;
	float z_origin;

	if (is_floor)
		z_origin = get_floor_height(sector, x_origin, y_origin);
	else
		z_origin = get_ceiling_height(sector, x_origin, y_origin);

	x_origin /= FINENESS;
	y_origin /= FINENESS;
	z_origin /= FINENESS;

	// NOTE: i'm not sure if floor/ceiling textures are transposed

	// texture x axis
	float u_x = 1;
	float u_y = 0;
	float u_z = 0;

	// texture y axis
	float v_x = 0;
	float v_y = 1;
	float v_z = 0;

	// normal vector to plane
	float n_x = out->normal[0];
	float n_y = out->normal[1];
	float n_z = out->normal[2];

	if (slope) {
		double angle = slope->tex_angle;

		float t_x = cos(angle / NUM_DEGREES * M_PI * 2);
		float t_y = sin(angle / NUM_DEGREES * M_PI * 2);
		float t_z = 0;

		// v = n x t
		v_x = (n_y * t_z) - (n_z * t_y);
		v_y = (n_z * t_x) - (n_x * t_z);
		v_z = (n_x * t_y) - (n_y * t_x);

		// u = v x n
		u_x = (v_y * n_z) - (v_z * n_y);
		u_y = (v_z * n_x) - (v_x * n_z);
		u_z = (v_x * n_y) - (v_y * n_x);

		// normalize n, v, and u
		float v_length = sqrt(v_x * v_x + v_y * v_y + v_z * v_z);
		float u_length = sqrt(u_x * u_x + u_y * u_y + u_z * u_z);
		float n_length = sqrt(n_x * n_x + n_y * n_y + n_z * n_z);

		out->normal[0] = n_x / n_length;
		out->normal[1] = n_y / n_length;
		out->normal[2] = n_z / n_length;

		v_x /= v_length;
		v_y /= v_length;
		v_z /= v_length;
		u_x /= u_length;
		u_y /= u_length;
		u_z /= u_length;
	}

	// calculate in uv coordinates (inverse of [u,v,n] matrix * (x, y, z))
	for (int i = 0; i < out->vertex_count; i++) {
		float x = out->positions[i * 3 + 0] / FINENESS;
		float y = out->positions[i * 3 + 1] / FINENESS;
		float z = out->positions[i * 3 + 2] / FINENESS;

		x -= x_origin;
		y -= y_origin;
		z -= z_origin;

		float *tex_x = &out->tex_coords[i * 2 + 0];
		float *tex_y = &out->tex_coords[i * 2 + 1];
		*tex_x = u_x * x + u_y * y + u_z * z;
		*tex_y = v_x * x + v_y * y + v_z * z;

		*tex_x -= u_offset;
		*tex_y -= v_offset;
	}
}

void meshify_subsectors()
{
	for (int i = 0; i < subsectors.length; i++) {
		struct subsector *subsector;
		subsector = dynamic_array_get(&subsectors, i);

		struct sector *sector = &sectors[subsector->sector_number - 1];

		struct mesh_poly mesh_poly;

		if (sector->floor_bitmap_num) {
			meshify_subsector_plane(subsector, 1, &mesh_poly);
			mesh_object_add_poly(&mesh_poly);
			free(mesh_poly.indices);
			free(mesh_poly.positions);
			free(mesh_poly.tex_coords);
			free(mesh_poly.normal);
		}

		if (sector->ceiling_bitmap_num) {
			meshify_subsector_plane(subsector, 0, &mesh_poly);
			mesh_object_add_poly(&mesh_poly);
			free(mesh_poly.indices);
			free(mesh_poly.positions);
			free(mesh_poly.tex_coords);
			free(mesh_poly.normal);
		}
	}
}

void meshify_walls()
{
	for (int i = 0; i < wall_count; i++) {
		meshify_wall(walls + i);
	}
}

char *change_ext(const char *filename, const char *new_ext)
{
	const char *dot = strrchr(filename, '.');
	size_t base_len;

	if (dot) {
		base_len = dot - filename;
	} else {
		base_len = strlen(filename);
	}

	// add 2 for null terminator and "."
	char *out = malloc(sizeof(char) * (base_len + strlen(new_ext) + 2));
	sprintf(out, "%.*s.%s", (int)base_len, filename, new_ext);
	return out;
}

char *cat_dir_base(char *dir, char *base)
{
	// add 2 for null terminator and "/"
	char *out = malloc(sizeof(char) * (strlen(dir) + strlen(base) + 2));
	sprintf(out, "%s/%s", dir, base);
	return out;
}

// output a obj and mtl file
/*
 * Positions and normals are transformed to make the forward axis -Z and 
 * upward axis Y. The model is also flipped to match the in-game geometry.
 */
void to_obj(char **argv)
{
	char *roo_name = basename(argv[1]);
	char *tex_dir = argv[2];

	char *obj_name = change_ext(roo_name, "obj");
	FILE *obj_file = fopen(obj_name, "w");

	char *mtl_name = change_ext(roo_name, "mtl");
	FILE *mtl_file = fopen(mtl_name, "w");

	fprintf(obj_file, "mtllib %s\n", mtl_name);

	for (int m = 0; m < mesh_objects.length; m++) {
		struct mesh_object *mesh = dynamic_array_get(&mesh_objects, m);

		for (int p = 0; p < mesh->positions.length; p += 3) {
			float *x = dynamic_array_get(&mesh->positions, p + 0);
			float *y = dynamic_array_get(&mesh->positions, p + 1);
			float *z = dynamic_array_get(&mesh->positions, p + 2);
			fprintf(obj_file, "v %f %f %f\n", *x / FINENESS * -1,
				*z / FINENESS, *y / FINENESS * -1);
		}
	}

	for (int m = 0; m < mesh_objects.length; m++) {
		struct mesh_object *mesh = dynamic_array_get(&mesh_objects, m);

		for (int t = 0; t < mesh->tex_coords.length; t += 2) {
			float *u = dynamic_array_get(&mesh->tex_coords, t + 0);
			float *v = dynamic_array_get(&mesh->tex_coords, t + 1);
			fprintf(obj_file, "vt %f %f\n", *u, *v);
		}
	}

	for (int m = 0; m < mesh_objects.length; m++) {
		struct mesh_object *mesh = dynamic_array_get(&mesh_objects, m);

		for (int n = 0; n < mesh->normals.length; n += 3) {
			float *x = dynamic_array_get(&mesh->normals, n + 0);
			float *y = dynamic_array_get(&mesh->normals, n + 1);
			float *z = dynamic_array_get(&mesh->normals, n + 2);
			fprintf(obj_file, "vn %f %f %f\n", *x * -1, *z,
				*y * -1);
		}
	}

	int i_offset = 1;
	for (int m = 0; m < mesh_objects.length; m++) {
		struct mesh_object *mesh = dynamic_array_get(&mesh_objects, m);

		// length of 10 = strlen("mat_") + strlen("65535") + 1 (null terminator)
		char mat_name[10];
		char *tex_path =
			cat_dir_base(tex_dir, mesh->material.texture_file_path);
		snprintf(mat_name, 10, "mat_%d", mesh->id);

		fprintf(mtl_file, "newmtl %s\n", mat_name);
		fprintf(mtl_file, "Ka 1.000000 1.000000 1.000000\n");
		fprintf(mtl_file, "Kd 1.000000 1.000000 1.000000\n");
		fprintf(mtl_file, "Ks 0.000000 0.000000 0.000000\n");
		fprintf(mtl_file, "Tr 1.000000\n");
		fprintf(mtl_file, "illum 1\n");
		fprintf(mtl_file, "Ns 0.000000\n");
		fprintf(mtl_file, "map_Kd %s\n\n", tex_path);
		free(tex_path);

		fprintf(obj_file, "usemtl %s\n", mat_name);

		for (int i = 0; i < mesh->indices.length; i += 3) {
			uint32_t *i1 = dynamic_array_get(&mesh->indices, i + 0);
			uint32_t *i2 = dynamic_array_get(&mesh->indices, i + 1);
			uint32_t *i3 = dynamic_array_get(&mesh->indices, i + 2);
			fprintf(obj_file, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
				*i1 + i_offset, *i1 + i_offset, *i1 + i_offset,
				*i2 + i_offset, *i2 + i_offset, *i2 + i_offset,
				*i3 + i_offset, *i3 + i_offset, *i3 + i_offset);
		}
		i_offset += mesh->positions.length / 3;
	}

	fclose(obj_file);
	fclose(mtl_file);

	free(obj_name);
	free(mtl_name);
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		printf("Usage: %s <.roo file path> <texture directory path>\n",
		       argv[0]);
		return EXIT_SUCCESS;
	}

	roo_file = fopen(argv[1], "r");

	if (!roo_file) {
		fprintf(stderr, "Error: Failed to open %s: %s\n", argv[1],
			strerror(errno));
		return EXIT_FAILURE;
	}

	DIR *dir = opendir(argv[2]);

	if (dir) {
		closedir(dir);
	} else {
		fprintf(stderr,
			"Error: Failed to open texture directory %s: %s\n",
			argv[2], strerror(errno));
		return EXIT_FAILURE;
	}

	texture_dir = malloc(strlen(argv[2]) + 1);
	strcpy(texture_dir, argv[2]);

	// initialize the mesh object array with an initial arbitrary capacity
	dynamic_array_init(&mesh_objects, 8, sizeof(struct mesh_object));

	if (load_room(roo_file)) {
		fprintf(stderr, "Error: Failed to load %s\n", argv[1]);
		return EXIT_FAILURE;
	}

	meshify_walls();
	meshify_subsectors();
	to_obj(argv);

	free(texture_dir);
	free(walls);
	free(sidedefs);
	free(sectors);
	free(things);
	for (int i = 0; i < subsectors.length; i++) {
		struct subsector *s = dynamic_array_get(&subsectors, i);
		free(s->points);
	}
	free(subsectors.data);

	for (int i = 0; i < mesh_objects.length; i++) {
		struct mesh_object *m = dynamic_array_get(&mesh_objects, i);
		free(m->material.texture_file_path);
		free(m->indices.data);
		free(m->positions.data);
		free(m->tex_coords.data);
		free(m->normals.data);
	}
	free(mesh_objects.data);
	fclose(roo_file);
	return EXIT_SUCCESS;
}