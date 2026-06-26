/*
 * vmu_lcd.h - shared geometry and framebuffer helpers for the SEGA Dreamcast
 * VMU LCD userspace tools.
 *
 * The VMU LCD is a 48x32 pixel, 1 bit per pixel mono panel. The framebuffer
 * fed to /dev/vmu_lcdN is exactly 192 bytes: 6 bytes per row, MSB = leftmost
 * pixel, row 0 at the top.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 */

#ifndef VMU_LCD_H
#define VMU_LCD_H

#include <string.h>

#define LCD_W		48
#define LCD_H		32
#define LCD_STRIDE	(LCD_W / 8)		/* 6 bytes per row */
#define LCD_FB_SIZE	(LCD_STRIDE * LCD_H)	/* 192 bytes */

static inline int vmu_lcd_get_pixel(const unsigned char *fb, int x, int y)
{
	if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H)
		return 0;
	return (fb[y * LCD_STRIDE + x / 8] >> (7 - (x % 8))) & 1;
}

static inline void vmu_lcd_set_pixel(unsigned char *fb, int x, int y)
{
	if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H)
		return;
	fb[y * LCD_STRIDE + x / 8] |= 0x80 >> (x % 8);
}

static inline void vmu_lcd_clear_pixel(unsigned char *fb, int x, int y)
{
	if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H)
		return;
	fb[y * LCD_STRIDE + x / 8] &= ~(0x80 >> (x % 8));
}

/* Rotate the whole image 180 degrees (VMU shown upright when in a controller). */
static inline void vmu_lcd_flip180(unsigned char *fb)
{
	unsigned char out[LCD_FB_SIZE];
	int x, y;

	memset(out, 0, sizeof(out));
	for (y = 0; y < LCD_H; y++)
		for (x = 0; x < LCD_W; x++)
			if (vmu_lcd_get_pixel(fb, x, y))
				vmu_lcd_set_pixel(out, LCD_W - 1 - x, LCD_H - 1 - y);

	memcpy(fb, out, LCD_FB_SIZE);
}

#endif /* VMU_LCD_H */
