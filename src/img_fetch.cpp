// Image download + EPD conversion for TRMNL V3 C5
// HTTP download pattern cherry-picked from C:\Work\trmnl\firmware (withHttp / downloadStream).
// PNG decoding via PNGdec (bitbank2/PNGdec), same library used in the firmware.

#include "img_fetch.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
// PNG_MAX_BUFFERED_PIXELS=16384 is set globally in platformio.ini build_flags so that
// PNGdec.cpp (which compiles png.inl) uses the same limit as the struct definition.
#include <PNGdec.h>

// ── Global BfkLx0_sections (replaces the static const arrays in BfkLx0.h) ──────
const uint8_t* BfkLx0_sections[IMG_FETCH_SECTIONS] = {nullptr};
static uint8_t* _secBufs[IMG_FETCH_SECTIONS]        = {nullptr};

// ── EPD 6-color palette  (RGB reference values from img2epd.py) ──────────────
static const struct { uint8_t r, g, b, nib; } kPal[] = {
    {  0,   0,   0, 0x0},   // Black
    {255, 255, 255, 0x1},   // White
    {255, 223,   0, 0x2},   // Yellow
    {196,   2,  51, 0x3},   // Red
    {  0,  86, 163, 0x5},   // Blue
    {  0, 155,  72, 0x6},   // Green
};

static uint8_t toNibble(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t best = UINT32_MAX;
    uint8_t  nib  = 0x1;
    for (auto& c : kPal) {
        int32_t dr = (int32_t)r - c.r;
        int32_t dg = (int32_t)g - c.g;
        int32_t db = (int32_t)b - c.b;
        uint32_t d = (uint32_t)(dr*dr + dg*dg + db*db);
        if (d < best) { best = d; nib = c.nib; }
    }
    return nib;
}

// ── Map a visible display column to (section, local_x) and write a nibble ────
//
// Display visible layout (left → right):
//   [CSX 0: 0-799] [CSX 1: 800-1599] [CSX 2: 1600-2399]
//   [CSX 3: 2400-2559 active, 2560-3199 dummy (internal to IC)]
//   [CSX 4: 2560-3359] [CSX 5: 3360-4159] [CSX 6: 4160-4959]
//   [CSX 7: 4960-5119 active, then dummy]
//
static void writePixel(uint32_t dc, uint32_t dr, uint8_t nib)
{
    uint32_t sec, lx;

    if (dc < 2400u) {
        sec = dc / 800u;
        lx  = dc % 800u;
    } else if (dc < 2560u) {          // IC4 active zone: vis cols 2400-2559
        sec = 3u;
        lx  = dc - 2400u;
    } else if (dc < 4960u) {          // ICs 5,6,7: vis cols 2560-4959
        sec = 4u + (dc - 2560u) / 800u;
        lx  = (dc - 2560u) % 800u;
    } else if (dc < 5120u) {          // IC8 active zone: vis cols 4960-5119
        sec = 7u;
        lx  = dc - 4960u;
    } else {
        return;
    }

    uint32_t byte = (dr * IMG_FETCH_SECTION_W + lx) >> 1u;
    if (lx & 1u) {
        _secBufs[sec][byte] = (_secBufs[sec][byte] & 0xF0u) | nib;
    } else {
        _secBufs[sec][byte] = (uint8_t)((nib << 4u) | (_secBufs[sec][byte] & 0x0Fu));
    }
}

// ── PNGdec draw callback (one call per decoded scanline) ─────────────────────
struct PngCtx {
    int srcW, srcH;
    int  cropTop;  // first source row shown (only used when yCrop=true)
    bool yCrop;    // true  = show native rows from cropTop (color images, avoids dither stripes)
                   // false = nearest-neighbour Y downscale / row-skip (1-bit images, full height)
};

// Decode one pixel from the PNGdec pixel stream and return its RGB.
// Advances *ps according to bit depth.
static inline void decodePix(int sx, uint8_t** ps, int iBpp, int iType,
                              const uint8_t* pPalette,
                              uint8_t* r, uint8_t* g, uint8_t* b)
{
    uint8_t* s = *ps;
    *r = *g = *b = 0;
    switch (iBpp) {
        case 32: *r=s[0]; *g=s[1]; *b=s[2]; *ps+=4; break;
        case 24: *r=s[0]; *g=s[1]; *b=s[2]; *ps+=3; break;
        case 8:
            if (iType == PNG_PIXEL_INDEXED && pPalette) {
                *r=pPalette[s[0]*3]; *g=pPalette[s[0]*3+1]; *b=pPalette[s[0]*3+2];
            } else { *r=*g=*b=s[0]; }
            *ps+=1; break;
        case 4: {
            uint8_t idx = (sx & 1) ? (s[0] & 0x0F) : (s[0] >> 4);
            if (iType == PNG_PIXEL_INDEXED && pPalette) {
                *r=pPalette[idx*3]; *g=pPalette[idx*3+1]; *b=pPalette[idx*3+2];
            } else { *r=*g=*b=(uint8_t)(idx|(idx<<4)); }
            if (sx & 1) *ps+=1;
            break;
        }
        case 2: {
            uint8_t idx = (s[0] >> (6-((sx&3)*2))) & 3;
            if (iType == PNG_PIXEL_INDEXED && pPalette) {
                *r=pPalette[idx*3]; *g=pPalette[idx*3+1]; *b=pPalette[idx*3+2];
            } else { *r=*g=*b=(uint8_t)(idx*85); }
            if ((sx & 3) == 3) *ps+=1;
            break;
        }
        case 1: {
            uint8_t bit = (s[0] >> (7-(sx&7))) & 1;
            if (iType == PNG_PIXEL_INDEXED && pPalette) {
                *r=pPalette[bit*3]; *g=pPalette[bit*3+1]; *b=pPalette[bit*3+2];
            } else { *r=*g=*b=(uint8_t)(bit?255:0); }
            if ((sx & 7) == 7) *ps+=1;
            break;
        }
    }
}

// PNGdec draw callback: return 1 to continue decoding, 0 to stop early.
static int pngDrawCb(PNGDRAW *pDraw)
{
    PngCtx* ctx = (PngCtx*)pDraw->pUser;

    uint32_t dr;
    if (ctx->yCrop) {
        // Color images: show source rows [cropTop .. cropTop+SECTION_H) at 1:1 Y scale.
        // Avoids horizontal stripe artifacts caused by downscaling pre-dithered 6-color rows.
        int dr_raw = (int)pDraw->y - ctx->cropTop;
        if (dr_raw < 0 || dr_raw >= (int)IMG_FETCH_SECTION_H) return 1;
        dr = (uint32_t)dr_raw;
    } else {
        // 1-bit / B&W images: nearest-neighbour Y downscale — skip rows that map to an
        // already-written display row. Shows the full image height with no cropping.
        dr = (uint32_t)((uint64_t)pDraw->y * IMG_FETCH_SECTION_H / ctx->srcH);
        if (pDraw->y > 0) {
            uint32_t prevDr = (uint32_t)((uint64_t)(pDraw->y-1) * IMG_FETCH_SECTION_H / ctx->srcH);
            if (prevDr == dr) return 1;
        }
        if (dr >= IMG_FETCH_SECTION_H) return 1;
    }

    uint8_t* s = pDraw->pPixels;
    int iBpp   = pDraw->iBpp;
    int iType  = pDraw->iPixelType;
    const uint8_t* pPal = pDraw->pPalette;
    if (iType == PNG_PIXEL_TRUECOLOR       && iBpp <= 8) iBpp *= 3;
    if (iType == PNG_PIXEL_TRUECOLOR_ALPHA && iBpp <= 8) iBpp *= 4;

    if (dr == 0)
        Serial.printf("[IMG] cb first display row (srcY=%d): iBpp=%d iType=%d px0=%02X %02X %02X\n",
                      pDraw->y, iBpp, iType, s[0], s[1], s[2]);

    int width = pDraw->iWidth;
    if (width > ctx->srcW) width = ctx->srcW;

    for (int sx = 0; sx < width; sx++) {
        uint8_t r, g, b;
        decodePix(sx, &s, iBpp, iType, pPal, &r, &g, &b);
        uint8_t nib = toNibble(r, g, b);
        uint32_t dc0 = (uint32_t)((uint64_t)sx     * DISPLAY_VISIBLE_W / ctx->srcW);
        uint32_t dc1 = (uint32_t)((uint64_t)(sx+1) * DISPLAY_VISIBLE_W / ctx->srcW);
        if (dc1 <= dc0) dc1 = dc0 + 1;
        for (uint32_t dc = dc0; dc < dc1 && dc < DISPLAY_VISIBLE_W; dc++)
            writePixel(dc, dr, nib);
    }
    return 1;
}

// ── Download helper (adapted from firmware downloadStream) ────────────────────
static uint32_t downloadToBuffer(WiFiClient* stream, int contentLen,
                                  uint8_t* buf, uint32_t maxLen)
{
    uint32_t toRead  = (contentLen > 0 && (uint32_t)contentLen < maxLen)
                       ? (uint32_t)contentLen : maxLen;
    uint32_t received   = 0;
    unsigned long tStart = millis();
    unsigned long tLast  = millis();

    while (received < toRead) {
        int avail = stream->available();
        if (avail > 0) {
            int n = stream->readBytes(buf + received,
                                      min(avail, (int)(toRead - received)));
            if (n > 0) { received += (uint32_t)n; tLast = millis(); }
        } else if (!stream->connected()) {
            break;
        } else if (millis() - tLast > 20000UL) {  // 20 s stall limit (was 5 s)
            Serial.println("[IMG] Download stall timeout");
            break;
        }
        delay(1);
    }

    Serial.printf("[IMG] Downloaded %u / %u bytes in %lu ms\n",
                  received, toRead, millis() - tStart);
    return received;
}

// ── BMP decoder ──────────────────────────────────────────────────────────────
// Handles 1-bit, 8-bit indexed, and 24/32-bit BMP (no RLE compression).
// Scales from any source resolution to DISPLAY_VISIBLE_W × IMG_FETCH_SECTION_H.
// _secBufs[] must already be allocated and pre-filled with 0x11 before calling.

static bool imgDecodeBmp(const uint8_t* buf, uint32_t len)
{
    if (len < 54 || buf[0] != 'B' || buf[1] != 'M') {
        Serial.println("[IMG] Not a BMP");
        return false;
    }

    // Use memcpy to avoid unaligned pointer casts (undefined behavior on RISC-V)
    uint32_t pixOff; memcpy(&pixOff, buf + 10, 4);
    int32_t  srcW;   memcpy(&srcW,   buf + 18, 4);
    int32_t  srcH;   memcpy(&srcH,   buf + 22, 4);
    uint16_t bpp;    memcpy(&bpp,    buf + 28, 2);
    uint32_t compress; memcpy(&compress, buf + 30, 4);

    Serial.printf("[IMG] BMP header: pixOff=%u srcW=%d srcH=%d bpp=%u compress=%u\n",
                  pixOff, srcW, srcH, bpp, compress);

    if (compress != 0) {
        Serial.printf("[IMG] Compressed BMP not supported\n");
        return false;
    }
    if (bpp != 1 && bpp != 8 && bpp != 24 && bpp != 32) {
        Serial.printf("[IMG] Unsupported BMP depth %u\n", bpp);
        return false;
    }

    bool     topDown = (srcH < 0);
    int32_t  rows    = topDown ? -srcH : srcH;
    Serial.printf("[IMG] BMP: %dx%d %ubpp topDown=%d pixOff=%u\n",
                  srcW, rows, bpp, topDown, pixOff);

    // Palette (for 1-bit and 8-bit; each entry is 4 bytes BGRA)
    uint8_t pal[256 * 4] = {};
    if (bpp <= 8) {
        uint32_t numColors = (1u << bpp);
        uint32_t palBytes  = numColors * 4u;
        if (54u + palBytes > pixOff) palBytes = pixOff - 54u;
        if (palBytes > 0 && 54u + palBytes <= len)
            memcpy(pal, buf + 54, palBytes);
    }

    // Row stride padded to 4-byte boundary
    uint32_t stride;
    switch (bpp) {
        case 1:  stride = ((uint32_t)(srcW) + 31u) / 32u * 4u; break;
        case 8:  stride = ((uint32_t)(srcW) +  3u) /  4u * 4u; break;
        case 24: stride = ((uint32_t)(srcW) * 3u + 3u) / 4u * 4u; break;
        default: stride = (uint32_t)(srcW) * 4u; break;  // 32-bit
    }

    for (int32_t sy = 0; sy < rows; sy++) {
        // BMP rows are stored bottom-up unless height was negative
        int32_t  srcRow = topDown ? sy : (rows - 1 - sy);
        uint32_t rowOff = pixOff + (uint32_t)srcRow * stride;
        if (rowOff + stride > len) break;
        const uint8_t* row = buf + rowOff;

        uint32_t dr = (uint32_t)((uint64_t)sy * IMG_FETCH_SECTION_H / (uint32_t)rows);
        if (sy > 0) {
            uint32_t prevDr = (uint32_t)((uint64_t)(sy - 1) * IMG_FETCH_SECTION_H / (uint32_t)rows);
            if (prevDr == dr) continue;
        }
        if (dr >= IMG_FETCH_SECTION_H) continue;

        for (int32_t sx = 0; sx < srcW; sx++) {
            uint8_t r = 0, g = 0, b = 0;
            switch (bpp) {
                case 1: {
                    uint8_t bit = (row[sx >> 3] >> (7 - (sx & 7))) & 1u;
                    b = pal[bit*4]; g = pal[bit*4+1]; r = pal[bit*4+2];
                    break;
                }
                case 8: {
                    uint8_t idx = row[sx];
                    b = pal[idx*4]; g = pal[idx*4+1]; r = pal[idx*4+2];
                    break;
                }
                case 24:
                    b = row[sx*3]; g = row[sx*3+1]; r = row[sx*3+2];
                    break;
                case 32:
                    b = row[sx*4]; g = row[sx*4+1]; r = row[sx*4+2];
                    break;
            }

            uint8_t  nib = toNibble(r, g, b);
            uint32_t dc0 = (uint32_t)((uint64_t)sx       * DISPLAY_VISIBLE_W / (uint32_t)srcW);
            uint32_t dc1 = (uint32_t)((uint64_t)(sx + 1) * DISPLAY_VISIBLE_W / (uint32_t)srcW);
            if (dc1 <= dc0) dc1 = dc0 + 1;
            for (uint32_t dc = dc0; dc < dc1 && dc < DISPLAY_VISIBLE_W; dc++)
                writePixel(dc, dr, nib);
        }
    }
    return true;
}

// ── PNG IDAT merger ───────────────────────────────────────────────────────────
// PNGdec has a bug reading the 4-byte CRC that follows each IDAT chunk: it reads
// the CRC bytes as the next chunk's length, the bounds check fires, and
// PNG_DECODE_ERROR is returned on any PNG with more than one IDAT chunk.
// Fix: rebuild the PNG in a new PSRAM buffer with all IDAT data merged into one
// chunk.  The merged chunk gets a dummy CRC (0x00000000) — PNGdec never checks
// chunk CRC (iOptions=0 → PNG_CHECK_CRC not set in the inflate call).
static uint8_t* pngMergeIdats(const uint8_t* src, uint32_t srcLen, uint32_t* outLen)
{
    if (srcLen < 8) return nullptr;

    // Phase 1: count IDAT chunks and total IDAT data bytes
    int     idatCount  = 0;
    uint32_t totalIdat = 0;
    uint32_t pos = 8;
    while (pos + 8 <= srcLen) {
        uint32_t clen  = ((uint32_t)src[pos]<<24)|((uint32_t)src[pos+1]<<16)|
                          ((uint32_t)src[pos+2]<<8)|src[pos+3];
        bool isIdat = memcmp(src+pos+4, "IDAT", 4) == 0;
        bool isIend = memcmp(src+pos+4, "IEND", 4) == 0;
        if (isIdat) { idatCount++; totalIdat += clen; }
        if (isIend) break;
        if (pos + 8 + clen + 4 <= pos) break; // overflow guard
        pos += 8 + clen + 4;
    }

    if (idatCount <= 1) return nullptr; // nothing to merge

    Serial.printf("[IMG] Merging %d IDAT chunks (%u bytes total) into one\n",
                  idatCount, totalIdat);

    // Phase 2: allocate output (same size as input is sufficient)
    uint8_t* dst = (uint8_t*)ps_malloc(srcLen);
    if (!dst) { Serial.println("[IMG] pngMergeIdats: ps_malloc failed"); return nullptr; }

    // Phase 3: copy signature + non-IDAT chunks, then one merged IDAT, then IEND
    uint32_t d = 0;
    memcpy(dst, src, 8); d = 8; // PNG signature

    bool idatWritten = false;
    pos = 8;
    while (pos + 8 <= srcLen) {
        uint32_t clen  = ((uint32_t)src[pos]<<24)|((uint32_t)src[pos+1]<<16)|
                          ((uint32_t)src[pos+2]<<8)|src[pos+3];
        bool isIdat = memcmp(src+pos+4, "IDAT", 4) == 0;
        bool isIend = memcmp(src+pos+4, "IEND", 4) == 0;

        if (isIend) {
            memcpy(dst+d, src+pos, 12); d += 12; // copy IEND verbatim
            break;
        }
        if (isIdat) {
            if (!idatWritten) {
                // Write merged IDAT chunk header
                dst[d+0]=(uint8_t)(totalIdat>>24); dst[d+1]=(uint8_t)(totalIdat>>16);
                dst[d+2]=(uint8_t)(totalIdat>>8);  dst[d+3]=(uint8_t)(totalIdat);
                dst[d+4]='I'; dst[d+5]='D'; dst[d+6]='A'; dst[d+7]='T';
                d += 8;
                // Collect all IDAT data in a second pass
                uint32_t p2 = 8;
                while (p2 + 8 <= srcLen) {
                    uint32_t clen2 = ((uint32_t)src[p2]<<24)|((uint32_t)src[p2+1]<<16)|
                                     ((uint32_t)src[p2+2]<<8)|src[p2+3];
                    if (memcmp(src+p2+4, "IDAT", 4) == 0) {
                        memcpy(dst+d, src+p2+8, clen2); d += clen2;
                    }
                    if (memcmp(src+p2+4, "IEND", 4) == 0) break;
                    p2 += 8 + clen2 + 4;
                }
                // Dummy CRC (not checked by PNGdec when iOptions=0)
                dst[d]=0; dst[d+1]=0; dst[d+2]=0; dst[d+3]=0; d += 4;
                idatWritten = true;
            }
            pos += 8 + clen + 4; // skip this IDAT chunk
            continue;
        }
        // Non-IDAT, non-IEND chunk: copy verbatim
        if (d + 8 + clen + 4 <= srcLen) { // bounds guard
            memcpy(dst+d, src+pos, 8+clen+4); d += 8+clen+4;
        }
        pos += 8 + clen + 4;
    }

    *outLen = d;
    return dst;
}

// ── Shared download + decode (PNG or BMP auto-detected) ──────────────────────

static bool imgDownloadAndDecode(const char* url)
{
    // HTTP download into a PSRAM buffer
    bool       isHttps = (strncmp(url, "https", 5) == 0);
    WiFiClient*       client    = nullptr;
    WiFiClientSecure* secClient = nullptr;
    if (isHttps) {
        secClient = new WiFiClientSecure();
        secClient->setInsecure();
        client = secClient;
    } else {
        client = new WiFiClient();
    }

    HTTPClient* http = new HTTPClient();
    if (!http->begin(*client, url)) {
        Serial.println("[IMG] http.begin() failed");
        delete http; delete client;
        return false;
    }
    http->setReuse(false);
    http->setTimeout(30000);
    http->setConnectTimeout(15000);
    http->addHeader("Accept-Encoding", "identity");

    Serial.printf("[IMG] GET %s\n", url);
    int code = http->GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[IMG] HTTP error %d\n", code);
        http->end(); delete http; delete client;
        return false;
    }
    int contentLen = http->getSize();
    Serial.printf("[IMG] Content-Length: %d\n", contentLen);

    const uint32_t MAX_BUF = 1536u * 1024u;
    uint32_t allocLen = (contentLen > 0 && (uint32_t)contentLen < MAX_BUF)
                        ? (uint32_t)contentLen : MAX_BUF;
    uint8_t* imgBuf = (uint8_t*)ps_malloc(allocLen);
    if (!imgBuf) {
        Serial.printf("[IMG] ps_malloc(%u) failed\n", allocLen);
        http->end(); delete http; delete client;
        return false;
    }

    WiFiClient* stream = http->getStreamPtr();
    uint32_t received = downloadToBuffer(stream, contentLen, imgBuf, allocLen);
    stream->stop();
    http->end();
    delete http; delete client;

    if (received < 4) {
        Serial.println("[IMG] Too few bytes received");
        free(imgBuf); return false;
    }

    Serial.printf("[IMG] Magic: %02X %02X %02X %02X\n",
                  imgBuf[0], imgBuf[1], imgBuf[2], imgBuf[3]);

    bool ok = false;
    static const uint8_t kPngMagic[] = {0x89, 0x50, 0x4E, 0x47};
    if (memcmp(imgBuf, kPngMagic, 4) == 0) {
        // Print IHDR fields: bit depth [24], color type [25], interlace [28]
        Serial.printf("[IMG] IHDR: bitdepth=%u colortype=%u compress=%u filter=%u interlace=%u\n",
                      imgBuf[24], imgBuf[25], imgBuf[26], imgBuf[27], imgBuf[28]);
        Serial.printf("[IMG] Free DRAM before PNG: %u bytes\n", esp_get_free_heap_size());
        // Scan PNG chunks to check for multiple IDATs or odd structure
        {
            uint32_t pos = 8; // skip PNG signature
            int idatCount = 0;
            while (pos + 8 <= received) {
                uint32_t clen = ((uint32_t)imgBuf[pos]<<24)|((uint32_t)imgBuf[pos+1]<<16)|
                                ((uint32_t)imgBuf[pos+2]<<8)|imgBuf[pos+3];
                char ctype[5] = {(char)imgBuf[pos+4],(char)imgBuf[pos+5],
                                 (char)imgBuf[pos+6],(char)imgBuf[pos+7],0};
                Serial.printf("[IMG] chunk %-4s len=%u\n", ctype, clen);
                if (strncmp(ctype,"IDAT",4) == 0) idatCount++;
                if (strncmp(ctype,"IEND",4) == 0) break;
                pos += 8 + clen + 4; // header + data + CRC
            }
            Serial.printf("[IMG] IDAT chunks: %d\n", idatCount);
        }

        // Merge multiple IDAT chunks into one (workaround for PNGdec multi-IDAT bug)
        {
            uint32_t mergedLen = 0;
            uint8_t* mergedBuf = pngMergeIdats(imgBuf, received, &mergedLen);
            if (mergedBuf) {
                free(imgBuf);
                imgBuf   = mergedBuf;
                received = mergedLen;
            }
        }

        // PNG
        PNG* png = new PNG();
        int rc = png->openRAM(imgBuf, (int)received, pngDrawCb);
        if (rc != PNG_SUCCESS) {
            Serial.printf("[IMG] PNG open error %d\n", rc);
        } else {
            int srcW = png->getWidth();
            int srcH = png->getHeight();
            // Always row-skip: matches HGD spec (compress 1440→720 by taking every other row).
            bool yCrop = false;
            PngCtx ctx = { srcW, srcH, /*cropTop=*/0, yCrop };
            Serial.printf("[IMG] PNG %dx%d %dbpp pixtype=%d\n",
                          srcW, srcH, png->getBpp(), png->getPixelType());
            int decRc = png->decode((void*)&ctx, 0);
            Serial.printf("[IMG] PNG decode rc=%d lastErr=%d\n", decRc, png->getLastError());
            ok = (decRc == PNG_SUCCESS || decRc == PNG_QUIT_EARLY);
        }
        png->close(); delete png;
    } else if (imgBuf[0] == 'B' && imgBuf[1] == 'M') {
        // BMP
        ok = imgDecodeBmp(imgBuf, received);
    } else {
        Serial.println("[IMG] Unknown image format");
    }

    free(imgBuf);
    return ok;
}

// ── Public API ────────────────────────────────────────────────────────────────

// Auto-detect PNG or BMP and decode.
bool imgFetchUrl(const char* url)
{
    for (int i = 0; i < IMG_FETCH_SECTIONS; i++) {
        _secBufs[i] = (uint8_t*)ps_malloc(IMG_FETCH_BYTES);
        if (!_secBufs[i]) {
            Serial.printf("[IMG] ps_malloc failed section %d\n", i);
            imgFreeBuffers(); return false;
        }
        memset(_secBufs[i], 0x11, IMG_FETCH_BYTES);
        BfkLx0_sections[i] = _secBufs[i];
    }
    Serial.printf("[IMG] %d section buffers ready\n", IMG_FETCH_SECTIONS);

    Serial.printf("[IMG] Waiting for WiFi (%s)...", IMG_FETCH_WIFI_SSID);
    uint32_t tWifi = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - tWifi > 20000UL) { Serial.println("\n[IMG] WiFi timeout"); return false; }
        delay(250); Serial.print('.');
    }
    Serial.printf("\n[IMG] Connected %s\n", WiFi.localIP().toString().c_str());

    bool ok = imgDownloadAndDecode(url);
    if (ok) Serial.println("[IMG] Decode complete");
    else    Serial.println("[IMG] Decode failed — buffers stay white");
    return ok;
}

bool imgFetchAndConvert(const char* url)
{
    // Allocate 8 PSRAM section buffers; pre-fill with white (0x11 = white nibble in both positions)
    for (int i = 0; i < IMG_FETCH_SECTIONS; i++) {
        _secBufs[i] = (uint8_t*)ps_malloc(IMG_FETCH_BYTES);
        if (!_secBufs[i]) {
            Serial.printf("[IMG] ps_malloc failed for section %d\n", i);
            imgFreeBuffers();
            return false;
        }
        memset(_secBufs[i], 0x11, IMG_FETCH_BYTES);
        BfkLx0_sections[i] = _secBufs[i];
    }
    Serial.printf("[IMG] %d PSRAM section buffers ready (%u KB each)\n",
                  IMG_FETCH_SECTIONS, IMG_FETCH_BYTES / 1024u);

    // Wait for WiFi connection (WiFi.begin() was already called in setup)
    Serial.printf("[IMG] Waiting for WiFi (%s)...", IMG_FETCH_WIFI_SSID);
    uint32_t tWifi = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - tWifi > 20000UL) {
            Serial.println("\n[IMG] WiFi timeout");
            return false;
        }
        delay(250);
        Serial.print('.');
    }
    Serial.printf("\n[IMG] Connected — IP: %s\n", WiFi.localIP().toString().c_str());

    // Set up HTTP/HTTPS client (pattern from firmware withHttp())
    bool       isHttps = (strncmp(url, "https", 5) == 0);
    WiFiClient*        client    = nullptr;
    WiFiClientSecure*  secClient = nullptr;

    if (isHttps) {
        secClient = new WiFiClientSecure();
        secClient->setInsecure(); // no certificate validation — add CA cert for production
        client = secClient;
    } else {
        client = new WiFiClient();
    }

    HTTPClient* http = new HTTPClient();
    if (!http->begin(*client, url)) {
        Serial.println("[IMG] http.begin() failed");
        delete http;
        delete client;
        return false;
    }
    http->setReuse(false);
    http->setTimeout(30000);
    http->setConnectTimeout(15000);
    http->addHeader("Accept-Encoding", "identity"); // disable compression for raw binary

    Serial.printf("[IMG] GET %s\n", url);
    int code = http->GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[IMG] HTTP error %d\n", code);
        http->end();
        delete http;
        delete client;
        return false;
    }
    int contentLen = http->getSize();
    Serial.printf("[IMG] Content-Length: %d bytes\n", contentLen);

    // Allocate a PSRAM buffer for the compressed PNG data.
    // A 2560×1440 6-color PNG is typically < 500 kB; cap at 1.5 MB for safety.
    const uint32_t MAX_PNG = 1536u * 1024u;
    uint32_t pngAllocLen = (contentLen > 0 && (uint32_t)contentLen < MAX_PNG)
                           ? (uint32_t)contentLen : MAX_PNG;
    uint8_t* pngBuf = (uint8_t*)ps_malloc(pngAllocLen);
    if (!pngBuf) {
        Serial.printf("[IMG] ps_malloc(%u) for PNG buffer failed\n", pngAllocLen);
        http->end();
        delete http;
        delete client;
        return false;
    }

    // Stream download into pngBuf
    WiFiClient* stream = http->getStreamPtr();
    uint32_t received = downloadToBuffer(stream, contentLen, pngBuf, pngAllocLen);
    stream->stop();
    http->end();
    delete http;
    delete client;

    if (received == 0) {
        Serial.println("[IMG] No data received");
        free(pngBuf);
        return false;
    }

    // Validate PNG magic bytes: 89 50 4E 47 0D 0A 1A 0A
    Serial.printf("[IMG] First 16 bytes: ");
    for (int i = 0; i < 16 && i < (int)received; i++) Serial.printf("%02X ", pngBuf[i]);
    Serial.println();
    static const uint8_t kPngMagic[] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    if (received < 8 || memcmp(pngBuf, kPngMagic, 8) != 0) {
        Serial.println("[IMG] ERROR: data is not a PNG (bad magic bytes)");
        // Print first 200 bytes as text (may be an HTTP error page)
        Serial.printf("[IMG] Response text: %.*s\n", (int)(received < 200u ? received : 200u), (char*)pngBuf);
        free(pngBuf);
        return false;
    }
    Serial.println("[IMG] PNG magic OK");

    // Decode PNG line-by-line into the 8 EPD section buffers
    PNG* png = new PNG();
    int rc = png->openRAM(pngBuf, (int)received, pngDrawCb);
    if (rc != PNG_SUCCESS) {
        Serial.printf("[IMG] PNG openRAM error %d\n", rc);
        delete png;
        free(pngBuf);
        return false;
    }

    int srcW = png->getWidth();
    int srcH = png->getHeight();
    bool yCrop = false;
    PngCtx ctx = { srcW, srcH, /*cropTop=*/0, yCrop };
    Serial.printf("[IMG] PNG: %d x %d, %d bpp pixtype=%d — decoding...\n",
                  srcW, srcH, png->getBpp(), png->getPixelType());

    int decRc = png->decode((void*)&ctx, 0);
    Serial.printf("[IMG] decode rc=%d (0=OK, -5=TOO_BIG, -8=QUIT_EARLY)\n", decRc);
    png->close();
    delete png;
    free(pngBuf);

    // Dump first 16 bytes of sec[0] — 0x11=white(untouched), anything else = pixels written
    Serial.printf("[IMG] sec[0] first 16 bytes:");
    for (int i = 0; i < 16; i++) Serial.printf(" %02X", _secBufs[0][i]);
    Serial.println();
    Serial.println("[IMG] Decode complete — section buffers ready");
    return true;
}

bool imgFetchBinary(const char* url)
{
    // Allocate 8 PSRAM section buffers
    for (int i = 0; i < IMG_FETCH_SECTIONS; i++) {
        _secBufs[i] = (uint8_t*)ps_malloc(IMG_FETCH_BYTES);
        if (!_secBufs[i]) {
            Serial.printf("[BIN] ps_malloc failed for section %d\n", i);
            imgFreeBuffers();
            return false;
        }
        BfkLx0_sections[i] = _secBufs[i];
    }
    Serial.printf("[BIN] %d PSRAM section buffers ready\n", IMG_FETCH_SECTIONS);

    // Wait for WiFi
    Serial.printf("[BIN] Waiting for WiFi (%s)...", IMG_FETCH_WIFI_SSID);
    uint32_t tWifi = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - tWifi > 20000UL) { Serial.println("\n[BIN] WiFi timeout"); return false; }
        delay(250); Serial.print('.');
    }
    Serial.printf("\n[BIN] Connected — IP: %s\n", WiFi.localIP().toString().c_str());

    bool isHttps = (strncmp(url, "https", 5) == 0);
    WiFiClient* client = isHttps ? (WiFiClient*)(new WiFiClientSecure()) : new WiFiClient();
    if (isHttps) static_cast<WiFiClientSecure*>(client)->setInsecure();

    HTTPClient* http = new HTTPClient();
    if (!http->begin(*client, url)) {
        Serial.println("[BIN] http.begin() failed");
        delete http; delete client; return false;
    }
    http->setReuse(false);
    http->setTimeout(60000);
    http->setConnectTimeout(15000);
    http->addHeader("Accept-Encoding", "identity");

    Serial.printf("[BIN] GET %s\n", url);
    int code = http->GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[BIN] HTTP error %d\n", code);
        http->end(); delete http; delete client; return false;
    }
    int contentLen = http->getSize();
    const uint32_t kExpected = (uint32_t)IMG_FETCH_SECTIONS * IMG_FETCH_BYTES; // 2,304,000
    Serial.printf("[BIN] Content-Length: %d  (expected %u)\n", contentLen, kExpected);

    // Stream directly into section buffers — no intermediate buffer needed
    WiFiClient* stream = http->getStreamPtr();
    uint32_t received = 0;
    int      secIdx   = 0;
    uint32_t secOff   = 0;
    unsigned long tLast = millis();

    while (secIdx < IMG_FETCH_SECTIONS) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = avail;
            if ((uint32_t)toRead > IMG_FETCH_BYTES - secOff) toRead = (int)(IMG_FETCH_BYTES - secOff);
            int n = stream->readBytes(_secBufs[secIdx] + secOff, toRead);
            secOff   += (uint32_t)n;
            received += (uint32_t)n;
            tLast = millis();
            if (secOff >= IMG_FETCH_BYTES) {
                Serial.printf("[BIN] Section %d complete\n", secIdx);
                secIdx++;
                secOff = 0;
            }
        } else if (!stream->connected() || millis() - tLast > 8000UL) {
            break;
        }
        delay(1);
    }

    stream->stop();
    http->end();
    delete http;
    delete client;

    Serial.printf("[BIN] Downloaded %u / %u bytes\n", received, kExpected);
    if (received < kExpected) {
        Serial.println("[BIN] Incomplete — aborting");
        imgFreeBuffers();
        return false;
    }
    Serial.println("[BIN] All sections ready");
    return true;
}

void imgFreeBuffers(void)
{
    for (int i = 0; i < IMG_FETCH_SECTIONS; i++) {
        free(_secBufs[i]);
        _secBufs[i]       = nullptr;
        BfkLx0_sections[i] = nullptr;
    }
}

bool imgAllocSolid(uint8_t fill_nib)
{
    imgFreeBuffers();
    uint8_t fillByte = (uint8_t)((fill_nib << 4) | fill_nib);
    for (int i = 0; i < IMG_FETCH_SECTIONS; i++) {
        _secBufs[i] = (uint8_t*)ps_malloc(IMG_FETCH_BYTES);
        if (!_secBufs[i]) {
            imgFreeBuffers();
            return false;
        }
        memset(_secBufs[i], fillByte, IMG_FETCH_BYTES);
        BfkLx0_sections[i] = _secBufs[i];
    }
    return true;
}

// ── Direct pixel write (imgDrawText lives in img_overlay.cpp) ─────────────────

void imgWritePixel(uint32_t dc, uint32_t dr, uint8_t nib) {
    writePixel(dc, dr, nib);
}
