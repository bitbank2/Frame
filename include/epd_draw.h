#pragma once

// EPD canvas dimensions (per driver IC section)
#define EPD_DRAW_WIDTH   800
#define EPD_DRAW_HEIGHT  720

// Color nibble values for 4bpp format
// Each byte holds 2 pixels: high nibble = even x, low nibble = odd x
#define COL_BLACK   0x0
#define COL_WHITE   0x1
#define COL_YELLOW  0x2
#define COL_RED     0x3
#define COL_BLUE    0x5
#define COL_GREEN   0x6

#ifdef __cplusplus
extern "C" {
#endif

void epdDrawSetPixel(int x, int y, uint8_t colorNibble);
void epdDrawFillRect(int x, int y, int w, int h, uint8_t colorNibble);
void epdDrawLine(int x0, int y0, int x1, int y1, uint8_t colorNibble);
void epdDrawThickLine(int x0, int y0, int x1, int y1, uint8_t colorNibble, int thickness);
void epdDrawFillCircle(int cx, int cy, int r, uint8_t colorNibble);
void epdDrawScene(void);

#ifdef __cplusplus
}
#endif
