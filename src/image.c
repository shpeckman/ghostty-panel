// src/image.c
#include "image.h"

#include <png.h>
#include <string.h>

#include <ghostty/vt.h>

static bool decode_png(void *userdata, const GhosttyAllocator *allocator, const uint8_t *data, size_t data_len, GhosttySysImage *out)
{
    png_image image;
    memset(&image, 0, sizeof image);
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, data_len)) return false;
    image.format = PNG_FORMAT_RGBA;
    size_t len = PNG_IMAGE_SIZE(image);
    uint8_t *pixels = ghostty_alloc(allocator, len);
    if (!pixels) {
        png_image_free(&image);
        return false;
    }
    if (!png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        ghostty_free(allocator, pixels, len);
        return false;
    }
    out->width = image.width;
    out->height = image.height;
    out->data = pixels;
    out->data_len = len;
    return true;
}

void image_install_png_decoder(void)
{
    ghostty_sys_set(GHOSTTY_SYS_OPT_DECODE_PNG, (const void *)decode_png);
}
