#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "kitty-graphics.h"

#if defined(HAVE_LIBPNG)
#include <png.h>
#endif

#define LOG_MODULE "kitty-graphics"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "base64.h"
#include "sixel.h"
#include "xmalloc.h"
#include "util.h"
#include "macros.h"

static void kitty_graphics_reset_pending(struct terminal *term)
{
    free(term->kitty_graphics.data);
    term->kitty_graphics.data = NULL;
    term->kitty_graphics.size = 0;
    term->kitty_graphics.cap = 0;
    term->kitty_graphics.active = false;
}

static bool append_pending(struct terminal *term, const char *src, size_t n)
{
    if (n == 0)
        return true;

    if (term->kitty_graphics.size + n + 1 > term->kitty_graphics.cap) {
        size_t new_cap = term->kitty_graphics.cap * 2;
        if (new_cap == 0)
            new_cap = 1024;
        while (term->kitty_graphics.size + n + 1 > new_cap)
            new_cap *= 2;
        char *p = realloc(term->kitty_graphics.data, new_cap);
        if (p == NULL)
            return false;
        term->kitty_graphics.data = p;
        term->kitty_graphics.cap = new_cap;
    }

    memcpy(&term->kitty_graphics.data[term->kitty_graphics.size], src, n);
    term->kitty_graphics.size += n;
    term->kitty_graphics.data[term->kitty_graphics.size] = '\0';
    return true;
}

#if defined(HAVE_LIBPNG)
static bool decode_png(const char *encoded, size_t encoded_len,
                       int *out_w, int *out_h, uint32_t **out_pixels)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&image, encoded, encoded_len))
        return false;

    image.format = PNG_FORMAT_RGBA;
    size_t stride = PNG_IMAGE_ROW_STRIDE(image);
    size_t size = PNG_IMAGE_SIZE(image);
    unsigned char *buf = malloc(size);
    if (buf == NULL) {
        png_image_free(&image);
        return false;
    }

    if (!png_image_finish_read(&image, NULL, buf, stride, NULL)) {
        free(buf);
        png_image_free(&image);
        return false;
    }

    int w = image.width;
    int h = image.height;
    uint32_t *px = calloc((size_t)w * h, sizeof(uint32_t));
    if (px == NULL) {
        free(buf);
        png_image_free(&image);
        return false;
    }

    for (int y = 0; y < h; y++) {
        const unsigned char *row = &buf[(size_t)y * stride];
        for (int x = 0; x < w; x++) {
            unsigned char r = row[x * 4 + 0];
            unsigned char g = row[x * 4 + 1];
            unsigned char b = row[x * 4 + 2];
            unsigned char a = row[x * 4 + 3];
            // PIXMAN_a8r8g8b8 little-endian bytes: B,G,R,A
            px[(size_t)y * w + x] =
                ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    free(buf);
    png_image_free(&image);

    *out_w = w;
    *out_h = h;
    *out_pixels = px;
    return true;
}
#endif

static char *read_file_bytes(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }

    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[sz] = '\0';
    *out_len = (size_t)sz;
    return buf;
}
static char *get_value(const char *ctrl, size_t ctrl_len, char key)
 {
    // returns malloc'd string or NULL
    size_t i = 0;
    while (i < ctrl_len) {
        if (ctrl[i] == key && i + 1 < ctrl_len && ctrl[i + 1] == '=') {
            size_t start = i + 2;
            size_t end = start;
            while (end < ctrl_len && ctrl[end] != ',')
                end++;
            size_t vlen = end - start;
            char *v = xmalloc(vlen + 1);
            memcpy(v, &ctrl[start], vlen);
            v[vlen] = '\0';
            return v;
        }
        while (i < ctrl_len && ctrl[i] != ',')
            i++;
        if (i < ctrl_len)
            i++;
    }
    (void)0;
    return NULL;
}

void kitty_graphics(struct terminal *term, const char *data, size_t len)
{
    if (len == 0 || data == NULL || data[0] != 'G')
        return;

    const char *ctrl = data + 1;
    size_t ctrl_len = 0;
    const char *payload = NULL;
    size_t payload_len = 0;

    /* find ';' */
    for (size_t i = 1; i < len; i++) {
        if (data[i] == ';') {
            ctrl_len = i - 1;
            payload = &data[i + 1];
            payload_len = len - (i + 1);
            break;
        }
    }
    if (payload == NULL) {
        ctrl_len = len - 1;
        payload = "";
        payload_len = 0;
    }

    char *a = get_value(ctrl, ctrl_len, 'a');
    char *f = get_value(ctrl, ctrl_len, 'f');
    char *s = get_value(ctrl, ctrl_len, 's');
    char *v = get_value(ctrl, ctrl_len, 'v');
    char *mstr = get_value(ctrl, ctrl_len, 'm');
    char *tstr = get_value(ctrl, ctrl_len, 't');
    char *cstr = get_value(ctrl, ctrl_len, 'c');
    char *rstr = get_value(ctrl, ctrl_len, 'r');

    int format = 32;
    if (f != NULL)
        format = atoi(f);

    int width = 0;
    int height = 0;
    if (s != NULL)
        width = atoi(s);
    if (v != NULL)
        height = atoi(v);

    char action = 'T';
    if (a != NULL && a[0] != '\0')
        action = a[0];

    int more = 0;
    if (mstr != NULL)
        more = atoi(mstr);

    char medium = 'd';
    if (tstr != NULL && tstr[0] != '\0')
        medium = tstr[0];

    int cols_req = 0;
    int rows_req = 0;
    if (cstr != NULL)
        cols_req = atoi(cstr);
    if (rstr != NULL)
        rows_req = atoi(rstr);

    LOG_DBG("kitty: a=%c f=%d t=%c s=%d v=%d c=%d r=%d more=%d payload=%zu",
            action, format, medium, width, height, cols_req, rows_req, more,
            payload_len);

    /* Delete action */
    if (action == 'd') {
        tll_foreach(term->grid->sixel_images, it) {
            sixel_destroy(&it->item);
            tll_remove(term->grid->sixel_images, it);
        }
        term->bits_affecting_ascii_printer.sixels = false;
        term_update_ascii_printer(term);
        kitty_graphics_reset_pending(term);
        return;
    }

    /* Chunk handling */
    if (term->kitty_graphics.active) {
        if (!append_pending(term, payload, payload_len))
            return;
        /* store format/size if provided (only first likely) */
        if (width > 0)
            term->kitty_graphics.width = width;
        if (height > 0)
            term->kitty_graphics.height = height;
        if (format != 0)
            term->kitty_graphics.format = format;

        if (more == 1)
            return;

        /* final: decode from pending */
        width = term->kitty_graphics.width;
        height = term->kitty_graphics.height;
        format = term->kitty_graphics.format;
        payload = term->kitty_graphics.data;
        payload_len = term->kitty_graphics.size;
        // decode below then reset pending
    } else {
        if (more == 1) {
            /* start pending */
            kitty_graphics_reset_pending(term);
            term->kitty_graphics.active = true;
            term->kitty_graphics.width = width;
            term->kitty_graphics.height = height;
            term->kitty_graphics.format = format;
            append_pending(term, payload, payload_len);
            return;
        }
        /* single-shot: decode payload directly */
        term->kitty_graphics.width = width;
        term->kitty_graphics.height = height;
        term->kitty_graphics.format = format;
    }

    /* base64 decode / file loading */
    size_t decoded_len = 0;
    char *decoded = NULL;

    if (medium == 'f') {
        size_t fname_len = 0;
        char *fname = base64_decode(payload, &fname_len);
        if (fname == NULL) {
            if (term->kitty_graphics.active)
                kitty_graphics_reset_pending(term);
            return;
        }
        char *path = xmalloc(fname_len + 1);
        memcpy(path, fname, fname_len);
        path[fname_len] = '\0';
        free(fname);
        decoded = read_file_bytes(path, &decoded_len);
        free(path);
    } else {
        decoded = base64_decode(payload, &decoded_len);
    }

    if (decoded == NULL) {
        /* Stash things? just return */
        if (term->kitty_graphics.active) {
            kitty_graphics_reset_pending(term);
        }
        return;
    }

    int img_w = 0, img_h = 0;
    uint32_t *pixels = NULL;
    bool ok = false;

    if (format == 100) {
#if defined(HAVE_LIBPNG)
        ok = decode_png(decoded, decoded_len, &img_w, &img_h, &pixels);
#else
        ok = false;
#endif
    } else {
        int ffmt = format;
        if (ffmt == 0)
            ffmt = 32;
        if (ffmt == 32) {
            if (width > 0 && height > 0 &&
                (size_t)width * height * 4 <= decoded_len) {
                img_w = width;
                img_h = height;
                pixels = malloc((size_t)img_w * img_h * sizeof(uint32_t));
                if (pixels != NULL) {
                    const unsigned char *src = (const unsigned char *)decoded;
                    for (int i = 0; i < img_w * img_h; i++) {
                        unsigned char r = src[i * 4 + 0];
                        unsigned char g = src[i * 4 + 1];
                        unsigned char b = src[i * 4 + 2];
                        unsigned char a = src[i * 4 + 3];
                        pixels[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                                    ((uint32_t)g << 8) | (uint32_t)b;
                    }
                    ok = true;
                }
            }
        } else if (ffmt == 24) {
            if (width > 0 && height > 0 &&
                (size_t)width * height * 3 <= decoded_len) {
                img_w = width;
                img_h = height;
                pixels = malloc((size_t)img_w * img_h * sizeof(uint32_t));
                if (pixels != NULL) {
                    const unsigned char *src = (const unsigned char *)decoded;
                    for (int i = 0; i < img_w * img_h; i++) {
                        unsigned char r = src[i * 3 + 0];
                        unsigned char g = src[i * 3 + 1];
                        unsigned char b = src[i * 3 + 2];
                        pixels[i] = (0xffu << 24) | ((uint32_t)r << 16) |
                                    ((uint32_t)g << 8) | (uint32_t)b;
                    }
                    ok = true;
                }
            }
        }
    }

    free(decoded);

    if (ok && pixels != NULL) {
        bool opaque = (format != 32);
        if (format == 0)
            opaque = false;
        if (format == 24)
            opaque = true;
        if (format == 32 || format == 100)
            opaque = false;
        if (cols_req > 0 || rows_req > 0) {
            int cell_w = term->cell_width;
            int cell_h = term->cell_height;
            int new_w = img_w;
            int new_h = img_h;

            if (cols_req > 0 && rows_req > 0) {
                new_w = cols_req * cell_w;
                new_h = rows_req * cell_h;
            } else if (cols_req > 0) {
                new_w = cols_req * cell_w;
                double scale = (double)new_w / (double)img_w;
                new_h = (int)((double)img_h * scale + 0.5);
            } else {
                new_h = rows_req * cell_h;
                double scale = (double)new_h / (double)img_h;
                new_w = (int)((double)img_w * scale + 0.5);
            }

            if (new_w < 1) new_w = 1;
            if (new_h < 1) new_h = 1;

            uint32_t *scaled = malloc((size_t)new_w * new_h * sizeof(uint32_t));
            if (scaled != NULL) {
                for (int y = 0; y < new_h; y++) {
                    int src_y = y * img_h / new_h;
                    for (int x = 0; x < new_w; x++) {
                        int src_x = x * img_w / new_w;
                        scaled[(size_t)y * new_w + x] =
                            pixels[(size_t)src_y * img_w + src_x];
                    }
                }
                free(pixels);
                pixels = scaled;
                img_w = new_w;
                img_h = new_h;
            }
        }

        sixel_add_image(term, img_w, img_h, pixels, opaque);
        /* pixels ownership transferred */
    } else {
        free(pixels);
    }

    if (term->kitty_graphics.active) {
        kitty_graphics_reset_pending(term);
    }
}
