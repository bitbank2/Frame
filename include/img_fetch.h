#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// One section buffer = 800 × 720 pixels @ 4bpp  (matches EPD_IMAGE_DATA_BUFFER)
#define IMG_FETCH_SECTIONS      8
#define IMG_FETCH_SECTION_W     800
#define IMG_FETCH_SECTION_H     720
#define IMG_FETCH_BYTES         288000

// Display visible pixel width split across all ICs:
//   ICs 0-2 (CSX 0-2): 800 active columns each → vis cols 0-2399
//   IC  3   (CSX 3)  : 160 active + 640 dummy  → vis cols 2400-2559
//   ICs 4-6 (CSX 4-6): 800 active columns each → vis cols 2560-4959
//   IC  7   (CSX 7)  : 160 active + 640 dummy  → vis cols 4960-5119
#define DISPLAY_VISIBLE_W       5120

// Provides the same BfkLx0_sections[] symbol as the static BfkLx0.h header so
// that the existing epdWriteImage(i, BfkLx0_sections[i], ...) calls in main work
// unchanged after imgFetchAndConvert() has been called.
extern const uint8_t* BfkLx0_sections[IMG_FETCH_SECTIONS];

// Download an image (PNG or BMP auto-detected), scale to 5120×720, convert to
// EPD 4bpp nibble format and fill BfkLx0_sections[0..7].
// WiFi must already be connected before calling.
// Returns true on success; on failure BfkLx0_sections still point to white buffers.
bool imgFetchUrl(const char* url);

// Same as imgFetchUrl but only accepts PNG — kept for backward compatibility.
bool imgFetchAndConvert(const char* url);

// Download a pre-converted EPD binary (8 × 288000 bytes raw, sections concatenated)
// directly into PSRAM section buffers — no PNG decode, no color conversion.
// Use this when the server pre-converts images with img2epd.py.
bool imgFetchBinary(const char* url);

// Free the eight PSRAM section buffers allocated by imgFetch*.
// Call this after epdDisplay() has finished and the data is no longer needed.
void imgFreeBuffers(void);

// Allocate section buffers and fill every pixel with a solid EPD color nibble.
// Use to create a blank canvas for device-info screens (no image download needed).
// Caller must still call imgFreeBuffers() when done.
bool imgAllocSolid(uint8_t fill_nib);

// Write one display pixel directly into the section buffers.
// dc = display column (0..5119), dr = display row (0..719), nib = EPD color nibble.
// Call only while buffers are allocated (after imgFetchUrl, before imgFreeBuffers).
void imgWritePixel(uint32_t dc, uint32_t dr, uint8_t nib);

// Overlay ASCII text onto the current section buffers using an embedded 8x8 font.
// x, y: top-left corner in display coordinates (0..5119, 0..719).
// fg_nib, bg_nib: EPD color nibbles (0x0=Black, 0x1=White, 0x2=Yellow,
//                 0x3=Red, 0x5=Blue, 0x6=Green).
// scale: pixel multiplier — 4 gives 32x32 px per character, readable at arm's length.
// Call after imgFetchUrl() and before epdWriteImage().
void imgDrawText(uint32_t x, uint32_t y, const char* text,
                 uint8_t fg_nib, uint8_t bg_nib, uint8_t scale);
