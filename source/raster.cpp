// SPDX-License-Identifier: Zlib OR GPL-3.0-or-later
//
// Copyright (c) 2023 Adrian "asie" Siekierka

#include <map>
#include "raster.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "stb_image.h"
#pragma GCC diagnostic pop

bool IsRasterImageExtensionFilename(const char *filename) {
	if (filename == NULL) return false;
	const char *p = strrchr(filename, '.');
	return p != NULL && (!strcasecmp(p, ".bmp") || !strcasecmp(p, ".gif") || !strcasecmp(p, ".png"));
}

bool IsBmpExtensionFilename(const char *filename) {
	if (filename == NULL) return false;
	const char *p = strrchr(filename, '.');
	return p != NULL && !strcasecmp(p, ".bmp");
}

RasterImage::~RasterImage() {
	if(data && !is_subimage) free(data);
	if(delays && !is_subimage) free(delays);
}

unsigned int RasterImage::get_data(int z, int x, int y) const {
	int bytes = get_component_size();
	int offset = (z * height + y) * width + x;
	unsigned int result = 0;
	for (int i = 0; i < bytes; i++) {
		result |= data[offset*bytes + i] << (i * 8);
	}
	return result;
}

void RasterImage::set_data(int z, int x, int y, unsigned int v) {
	int bytes = get_component_size();
	int offset = (z * height + y) * width + x;
	for (int i = 0; i < bytes; i++) {
		data[offset*bytes + i] = v >> (i * 8);
	}
}

bool RasterImage::loadFile(const char *filename) {
	stbi__context ctx;
	
	data = NULL;
	delays = NULL;

	FILE *f = stbi__fopen(filename, "rb");
	if (f == NULL) {
		fprintf(stderr, "Could not open image file \"%s\".\n", filename);
		return false;
	}
	stbi__start_file(&ctx, f);

	return loadStb(&ctx, filename);
}

bool RasterImage::loadBuffer(const void *_data, size_t length, const char *name) {
	stbi__context ctx;

	data = NULL;
	delays = NULL;

	stbi__start_mem(&ctx, (const stbi_uc*) _data, length);

	return loadStb(&ctx, name);
}

bool RasterImage::loadStb(void *_ctx, const char *filename) {
	stbi__context *ctx = (stbi__context*) _ctx;
	stbi__result_info res;
	
	if (stbi__gif_test(ctx)) {
		// Load GIFs using the dedicated function - this allows loading animation data.
		res.bits_per_channel = 8;
		data = (unsigned char*) stbi__load_gif_main(ctx, &delays, &width, &height, &frames, &components, 4);
	} else {
		frames = 1;
		delays = NULL;
		data = (unsigned char*) stbi__load_main(ctx, &width, &height, &components, 4, &res, 8);
	}
	// The "reqcomp" stbi parameter seems to convert, but still set the old value...
	components = 4;

	if (data == NULL) {
		fprintf(stderr, "Could not read image file \"%s\".\n", filename);
		if (delays) { free(delays); delays = NULL; }
		return false;
	}
	if (frames > RASTER_MAX_FRAME_COUNT) {
		fprintf(stderr, "Could not read image file \"%s\" (too many frames = %d > %d).\n", filename, frames, RASTER_MAX_FRAME_COUNT);
		if (data) { free(data); data = NULL; }
		if (delays) { free(delays); delays = NULL; }
		return false;
	}

	// Convert 16bpc -> 8bpc images, as stbi__load_and_postprocess_8bit would in the high-level API.
	if (res.bits_per_channel == 16) {
		if (!stbi__convert_16_to_8((stbi__uint16*) data, width, height, 4)) {
			fprintf(stderr, "Could not read image file \"%s\" (16->8 bpc conversion failure).\n", filename);
			if (data) { free(data); data = NULL; }
			if (delays) { free(delays); delays = NULL; }
			return false;
		}
	} else if (res.bits_per_channel != 8) {
		fprintf(stderr, "Could not read image file \"%s\" (invalid bpc = %d).\n", filename, res.bits_per_channel);
		if (data) { free(data); data = NULL; }
		if (delays) { free(delays); delays = NULL; }
		return false;
	}

	return true;
}

typedef struct __attribute__((packed)) {
	uint16_t magic = 0x4D42;
	uint32_t size;
	uint16_t reserved[2];
	uint32_t offset;
	uint32_t core_header_size = 12;
	uint16_t width;
	uint16_t height;
	uint16_t planes = 1;
	uint16_t bpp;
} bmp_header_t;

bool RasterImage::saveFile(const char *filename) {
	// TODO: Support saving images which are not unpaletted BMPs.
	// (Probably during the migration to libplum.)

	if (has_palette) return false;
	if (frames > 1) return false;
	if (components != 3) return false;

	FILE *f = fopen(filename, "wb");
	if (!f) return false;

	bmp_header_t header;
	header.size = sizeof(bmp_header_t) + (width * height * 3);
	header.offset = sizeof(bmp_header_t);
	header.width = width;
	header.height = height;
	header.bpp = get_component_size() * 8;
	if (fwrite(&header, sizeof(header), 1, f) <= 0) return false;
	for (int y = height - 1; y >= 0; y--) {
		for (int x = 0; x < width; x++) {
			if (fputc((get_data(0, x, y) >> 16) & 0xFF, f) < 0) return false;
			if (fputc((get_data(0, x, y) >> 8) & 0xFF, f) < 0) return false;
			if (fputc(get_data(0, x, y) & 0xFF, f) < 0) return false;
		}
	}

	fclose(f);
	return true;
}

int RasterImage::max_palette_count(void) const {
	if (!has_palette) return -1;
	unsigned int count = 0;

	for (int fr = 0; fr < frames; fr++) {
		if (palette_count[fr] > count) {
			count = palette_count[fr];
		}
	}

	return (int) count;
}

bool RasterImage::quantize_rgb15(void) {
	if (has_palette) {
		fprintf(stderr, "Could not quantize image to RGB15: already paletted.\n");
		return false;
	}
	if (components == 1 || components == 3) {
		// No alpha - quantization unnecessary.
		return true;
	}
	for (int fr = 0; fr < frames; fr++) {
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				unsigned int pxl = get_data(fr, x, y);
				if (components == 4) {
					// quantize alpha
					if ((pxl >> 24) >= 0x80) {
						pxl |= 0xFF000000;
					} else {
						pxl &= 0x000000FF;
					}
					// quantize color
					pxl &= 0xFFF8F8F8;
				} else {
					if ((pxl >> 8) >= 0x80) {
						pxl |= 0xFF00;
					} else {
						pxl &= 0x00FF;
					}
					// quantize color
					pxl &= 0xFFF8;
				}
				set_data(fr, x, y, pxl);
			}
		}
	}
	return true;
}

bool RasterImage::convert_palette(void) {
	if (has_palette) return true;

	unsigned char *new_frame_data = (unsigned char*) malloc(width * height * frames);
	unsigned char *new_frame_data_ptr = new_frame_data;

	for (int fr = 0; fr < frames; fr++) {
		// build palette for frame
		int palette_idx = 0;
		std::map<unsigned int, unsigned char> palette_map;

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				unsigned int pxl = get_data(fr, x, y);
				auto entry = palette_map.find(pxl);
				if (entry == palette_map.end()) {
					palette_map[pxl] = 0;
					palette_idx++;
				}
			}
		}

		if (palette_idx > RASTER_MAX_PALETTE_SIZE) {
			fprintf(stderr, "Could not convert image to palette (too many colors %d > %d).\n", palette_idx, RASTER_MAX_PALETTE_SIZE);
			free(new_frame_data);
			return false;
		}

		// order palette indices (for make_zero_translucent)
		palette_idx = 0;
		for (auto it = palette_map.begin(); it != palette_map.end(); it++) {
			it->second = palette_idx++;
			palette[fr][it->second] = it->first;
		}

		// convert frame to palette
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				unsigned int pxl = get_data(fr, x, y);
				*(new_frame_data_ptr++) = palette_map.at(pxl);
			}
		}
		palette_count[fr] = palette_idx;
	}

	free(data);
	data = new_frame_data;
	has_palette = true;
	return true;
}

bool RasterImage::make_zero_transparent(void) {
	if (!has_palette) {
		fprintf(stderr, "Could not adjust image palette for transparency: no palette.\n");
		return false;
	}
	if (components != 4) {
		fprintf(stderr, "Could not adjust image palette for transparency: unsupported component format = %d.\n", components);
		return false;
	}

	for (int fr = 0; fr < frames; fr++) {
		// We make use of the fact that alpha is the highest-order byte
		// and that the palette building map is ordered.
		unsigned int translucent_color_count = 0;
		while (translucent_color_count < palette_count[fr] && (palette[fr][translucent_color_count] >> 24) < 0x80) {
			translucent_color_count++;
		}

		// We want one translucent color, at index zero.
		if (translucent_color_count == 1) {
			palette[fr][0] &= 0x00FFFFFF;
			continue;
		}
		if (translucent_color_count == 0 && palette_count[fr] == RASTER_MAX_PALETTE_SIZE) {
			// Resulting palette would be too large.
			fprintf(stderr, "Could not adjust image palette for transparency: too many colors (frame %d).\n", fr);
			return false;
		}
		int offset = 1 - translucent_color_count;

		// Adjust palette.
		memmove(palette[fr] + 1, palette[fr] + translucent_color_count, (palette_count[fr] + offset) * sizeof(unsigned int));
		palette[fr][0] = 0x00FF00FF;
		palette_count[fr] += offset;

		// Adjust palette data.
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				unsigned int pxl = get_data(fr, x, y);
				if (translucent_color_count > 0 && pxl < translucent_color_count) {
					pxl = 0;
				} else {
					pxl += offset;
				}
				set_data(fr, x, y, pxl);
			}
		}
	}

	return true;
}

RasterImage *RasterImage::clone(bool flip_h, bool flip_v) const {
	RasterImage *result = new RasterImage;
	memcpy(result, this, sizeof(RasterImage));

	if (delays != NULL) {
		result->delays = (int*) malloc(frames * sizeof(int));
		memcpy(result->delays, delays, frames * sizeof(int));
	}

	size_t data_size = frames * width * height * get_component_size();
	result->data = (unsigned char*) malloc(data_size);
	if (!flip_h && !flip_v) {
		memcpy(result->data, this->data, data_size);
	} else {
		for (int z = 0; z < frames; z++) {
			for (int y = 0; y < height; y++) {
				for (int x = 0; x < width; x++) {
					int in_x = flip_h ? width - 1 - x : x;
					int in_y = flip_v ? height - 1 - y : y;
					result->set_data(z, x, y, get_data(z, in_x, in_y));
				}
			}
		}
	}
	result->is_subimage = false;

	return result;
}
