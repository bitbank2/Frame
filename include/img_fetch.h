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

// Download a PNG from url, scale it to 5120×720, convert to EPD 4bpp nibble
// format, and fill BfkLx0_sections[0..7] with the result.
// WiFi.begin() must have already been called before this function.
// Returns true on success; on failure BfkLx0_sections still point to white buffers.
bool imgFetchAndConvert(const char* url);

// Download a pre-converted EPD binary (8 × 288000 bytes raw, sections concatenated)
// directly into PSRAM section buffers — no PNG decode, no color conversion.
// Use this when the server pre-converts images with img2epd.py.
bool imgFetchBinary(const char* url);

// Free the eight PSRAM section buffers allocated by imgFetch*.
// Call this after epdDisplay() has finished and the data is no longer needed.
void imgFreeBuffers(void);
