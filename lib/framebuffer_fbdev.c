/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include "framebuffer.h"

//#include "minui.h"
//#include <pixelflinger/pixelflinger.h>

typedef struct GRSurface {
    int width;
    int height;
    int row_bytes;
    int pixel_bytes;
    unsigned char* data;
    __u32 format;
} GRSurface;

enum GGLPixelFormat {
    // these constants need to match those
    // in graphics/PixelFormat.java, ui/PixelFormat.h, BlitHardware.h
    GGL_PIXEL_FORMAT_UNKNOWN    =   0,
    GGL_PIXEL_FORMAT_NONE       =   0,

    GGL_PIXEL_FORMAT_RGBA_8888   =   1,  // 4x8-bit ARGB
    GGL_PIXEL_FORMAT_RGBX_8888   =   2,  // 3x8-bit RGB stored in 32-bit chunks
    GGL_PIXEL_FORMAT_RGB_888     =   3,  // 3x8-bit RGB
    GGL_PIXEL_FORMAT_RGB_565     =   4,  // 16-bit RGB
    GGL_PIXEL_FORMAT_BGRA_8888   =   5,  // 4x8-bit BGRA
    GGL_PIXEL_FORMAT_RGBA_5551   =   6,  // 16-bit RGBA
    GGL_PIXEL_FORMAT_RGBA_4444   =   7,  // 16-bit RGBA

    GGL_PIXEL_FORMAT_A_8         =   8,  // 8-bit A
    GGL_PIXEL_FORMAT_L_8         =   9,  // 8-bit L (R=G=B = L)
    GGL_PIXEL_FORMAT_LA_88       = 0xA,  // 16-bit LA
    GGL_PIXEL_FORMAT_RGB_332     = 0xB,  // 8-bit RGB (non paletted)

    // reserved range. don't use.
    GGL_PIXEL_FORMAT_RESERVED_10 = 0x10,
    GGL_PIXEL_FORMAT_RESERVED_11 = 0x11,
    GGL_PIXEL_FORMAT_RESERVED_12 = 0x12,
    GGL_PIXEL_FORMAT_RESERVED_13 = 0x13,
    GGL_PIXEL_FORMAT_RESERVED_14 = 0x14,
    GGL_PIXEL_FORMAT_RESERVED_15 = 0x15,
    GGL_PIXEL_FORMAT_RESERVED_16 = 0x16,
    GGL_PIXEL_FORMAT_RESERVED_17 = 0x17,

    // reserved/special formats
    GGL_PIXEL_FORMAT_Z_16       =  0x18,
    GGL_PIXEL_FORMAT_S_8        =  0x19,
    GGL_PIXEL_FORMAT_SZ_24      =  0x1A,
    GGL_PIXEL_FORMAT_SZ_8       =  0x1B,

    // reserved range. don't use.
    GGL_PIXEL_FORMAT_RESERVED_20 = 0x20,
    GGL_PIXEL_FORMAT_RESERVED_21 = 0x21,
};

static GRSurface gr_framebuffer[2];
static bool double_buffered;
static GRSurface* gr_draw = NULL;
static int displayed_buffer;

static struct fb_var_screeninfo vi;
static int fb_fd = -1;
static __u32 smem_len;

static void fbdev_blank(bool blank)
{
#if defined(TW_NO_SCREEN_BLANK) && defined(TW_BRIGHTNESS_PATH) && defined(TW_MAX_BRIGHTNESS)
    int fd;
    char brightness[4];
    snprintf(brightness, 4, "%03d", TW_MAX_BRIGHTNESS/2);

    fd = open(TW_BRIGHTNESS_PATH, O_RDWR);
    if (fd < 0) {
        perror("cannot open LCD backlight");
        return;
    }
    write(fd, blank ? "000" : brightness, 3);
    close(fd);

#ifdef TW_SECONDARY_BRIGHTNESS_PATH
    fd = open(TW_SECONDARY_BRIGHTNESS_PATH, O_RDWR);
    if (fd < 0) {
        perror("cannot open LCD backlight 2");
        return;
    }
    write(fd, blank ? "000" : brightness, 3);
    close(fd);
#endif
#else
#ifndef TW_NO_SCREEN_BLANK
    int ret;

    ret = ioctl(fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
#endif
#endif
}

static void set_displayed_framebuffer(unsigned n)
{
    if (n > 1 || !double_buffered) return;

    vi.yres_virtual = gr_framebuffer[0].height * 2;
    vi.yoffset = n * gr_framebuffer[0].height;
    vi.bits_per_pixel = gr_framebuffer[0].pixel_bytes * 8;
    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("active fb swap failed");
#ifdef TW_FBIOPAN
    } else {
        if (ioctl(fb_fd, FBIOPAN_DISPLAY, &vi) < 0) {
            perror("pan failed");
        }
#endif
    }
    displayed_buffer = n;
}

static int fbdev_init() {
    int retry = 20;
    int fd = -1;
    while (fd == -1) {
        fd = open("/dev/graphics/fb0", O_RDWR);
        if (fd == -1) {
            if (--retry) {
                // wait for init to create the device node
                perror("cannot open fb0 (retrying)");
                usleep(100000);
            } else {
                perror("cannot open fb0 (giving up)");
                return -1;
            }
        }
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("failed to get fb0 info (FBIOGET_VSCREENINFO)");
        close(fd);
        return -1;
    }

#ifdef RECOVERY_FORCE_RGB_565
    // Changing fb_var_screeninfo can affect fb_fix_screeninfo,
    // so this needs done before querying for fi.
    printf("Forcing pixel format: RGB_565\n");
    vi.blue.offset    = 0;
    vi.green.offset   = 5;
    vi.red.offset     = 11;
    vi.blue.length    = 5;
    vi.green.length   = 6;
    vi.red.length     = 5;
    vi.blue.msb_right = 0;
    vi.green.msb_right = 0;
    vi.red.msb_right = 0;
    vi.transp.offset  = 0;
    vi.transp.length  = 0;
    vi.bits_per_pixel = 16;

    if (ioctl(fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("failed to put force_rgb_565 fb0 info");
        close(fd);
        return NULL;
    }
#endif

    struct fb_fix_screeninfo fi;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("failed to get fb0 info (FBIOGET_FSCREENINFO)");
        close(fd);
        return -1;
    }

    // We print this out for informational purposes only, but
    // throughout we assume that the framebuffer device uses an RGBX
    // pixel format.  This is the case for every development device I
    // have access to.  For some of those devices (eg, hammerhead aka
    // Nexus 5), FBIOGET_VSCREENINFO *reports* that it wants a
    // different format (XBGR) but actually produces the correct
    // results on the display when you write RGBX.
    //
    // If you have a device that actually *needs* another pixel format
    // (ie, BGRX, or 565), patches welcome...

    printf("fb0 reports (possibly inaccurate):\n"
           "  vi.bits_per_pixel = %d\n"
           "  vi.red.offset   = %3d   .length = %3d\n"
           "  vi.green.offset = %3d   .length = %3d\n"
           "  vi.blue.offset  = %3d   .length = %3d\n",
           vi.bits_per_pixel,
           vi.red.offset, vi.red.length,
           vi.green.offset, vi.green.length,
           vi.blue.offset, vi.blue.length);

    void* bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        perror("failed to mmap framebuffer");
        close(fd);
        return -1;
    }

    memset(bits, 0, fi.smem_len);

    gr_framebuffer[0].width = vi.xres;
    gr_framebuffer[0].height = vi.yres;
    gr_framebuffer[0].row_bytes = fi.line_length;
    gr_framebuffer[0].pixel_bytes = vi.bits_per_pixel / 8;
#ifdef RECOVERY_GRAPHICS_FORCE_USE_LINELENGTH
    printf("Forcing line length\n");
    vi.xres_virtual = fi.line_length / gr_framebuffer[0].pixel_bytes;
#endif
    gr_framebuffer[0].data = (uint8_t *)(bits);
    if (vi.bits_per_pixel == 16) {
        printf("setting GGL_PIXEL_FORMAT_RGB_565\n");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGB_565;
    } else if (vi.red.offset == 8 || vi.red.offset == 16) {
        printf("setting GGL_PIXEL_FORMAT_BGRA_8888\n");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_BGRA_8888;
    } else if (vi.red.offset == 0) {
        printf("setting GGL_PIXEL_FORMAT_RGBA_8888\n");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGBA_8888;
    } else if (vi.red.offset == 24) {
        printf("setting GGL_PIXEL_FORMAT_RGBX_8888\n");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGBX_8888;
    } else {
        if (vi.red.length == 8) {
            printf("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGBX_8888\n");
            gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGBX_8888;
        } else {
            printf("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGB_565\n");
            gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGB_565;
        }
    }

    // Drawing directly to the framebuffer takes about 5 times longer.
    // Instead, we will allocate some memory and draw to that, then
    // memcpy the data into the framebuffer later.
    gr_draw = (GRSurface*) malloc(sizeof(GRSurface));
    if (!gr_draw) {
        perror("failed to allocate gr_draw");
        close(fd);
        munmap(bits, fi.smem_len);
        return -1;
    }
    memcpy(gr_draw, gr_framebuffer, sizeof(GRSurface));
    gr_draw->data = (unsigned char*) calloc(gr_draw->height * gr_draw->row_bytes, 1);
    if (!gr_draw->data) {
        perror("failed to allocate in-memory surface");
        close(fd);
        free(gr_draw);
        munmap(bits, fi.smem_len);
        return -1;
    }

    /* check if we can use double buffering */
#ifndef RECOVERY_GRAPHICS_FORCE_SINGLE_BUFFER
    if (vi.yres * fi.line_length * 2 <= fi.smem_len) {
        double_buffered = true;
        printf("double buffered\n");

        memcpy(gr_framebuffer+1, gr_framebuffer, sizeof(GRSurface));
        gr_framebuffer[1].data = gr_framebuffer[0].data +
            gr_framebuffer[0].height * gr_framebuffer[0].row_bytes;

    } else {
#else
    {
        printf("RECOVERY_GRAPHICS_FORCE_SINGLE_BUFFER := true\n");
#endif
        double_buffered = false;
        printf("single buffered\n");
    }
#if defined(RECOVERY_BGRA)
    printf("RECOVERY_BGRA\n");
#endif
    fb_fd = fd;
    set_displayed_framebuffer(0);

    printf("framebuffer: %d (%d x %d)\n", fb_fd, gr_draw->width, gr_draw->height);

    smem_len = fi.smem_len;

    fbdev_blank(true);
    fbdev_blank(false);

    return 0;
}

static int fbdev_flip() {
#if defined(RECOVERY_BGRA)
    // In case of BGRA, do some byte swapping
    unsigned char* ucfb_vaddr = (unsigned char*)gr_draw->data;
    for (int idx = 0 ; idx < (gr_draw->height * gr_draw->row_bytes);
            idx += 4) {
        unsigned char tmp = ucfb_vaddr[idx];
        ucfb_vaddr[idx    ] = ucfb_vaddr[idx + 2];
        ucfb_vaddr[idx + 2] = tmp;
    }
#endif
    if (double_buffered) {
        // Copy from the in-memory surface to the framebuffer.
        memcpy(gr_framebuffer[1-displayed_buffer].data, gr_draw->data,
               gr_draw->height * gr_draw->row_bytes);
        set_displayed_framebuffer(1-displayed_buffer);
    } else {
        // Copy from the in-memory surface to the framebuffer.
        memcpy(gr_framebuffer[0].data, gr_draw->data,
               gr_draw->height * gr_draw->row_bytes);
    }
    return 0;
}

static void fbdev_exit() {
    close(fb_fd);
    fb_fd = -1;

    if (gr_draw) {
        free(gr_draw->data);
        free(gr_draw);
    }
    gr_draw = NULL;
    munmap(gr_framebuffer[0].data, smem_len);
}

static void* fbdev_get_frame_dest() {

    return gr_draw->data;
}

const struct fb_impl fb_impl_fbdev = {
    .name = "fbdev",
    .impl_id = FB_IMPL_FBDEV,
    .open = fbdev_init,
    .close = fbdev_exit,
    .update = fbdev_flip,
    .get_frame_dest = fbdev_get_frame_dest,
};
