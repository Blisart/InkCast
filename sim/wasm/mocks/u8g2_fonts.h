#ifndef _u8g2_fonts_h
#define _u8g2_fonts_h
#include <stdint.h>

#define U8X8_NOINLINE
#define U8X8_SECTION(name)
#define U8X8_UNUSED
#define U8X8_FONT_SECTION(name)
#define u8x8_pgm_read(adr) (*(const uint8_t *)(adr))
#define U8X8_PROGMEM
#define U8G2_FONT_SECTION(name)
#define U8G2_USE_LARGE_FONTS

extern const uint8_t u8g2_font_7x14_tf[];

#endif
