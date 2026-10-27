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
#include <bb_epaper.h>
BBEPAPER bbep;
uint8_t u8SpectraPal[512]; // RGB333 mapped to closest Spectra6 color

//
// bb_epaper colors to map to Spectra6 colors
// The RGB values are not correct for the panel, but for simple mapping
// these work best. These get mapped from bb_epaper color indices to
// Spectra6 color indices by the setPixel() method.
//
const int iSpectraRGB[] = { // r, g, b
    0, 0, 0, // black = 0
    192,192,192, // white = 1
    192,192,0, // yellow = 2
    192,0,0, // red = 3
    0,0,192, // blue = 4
    0,192,0, // green = 5
};
// Fixes the 'gap' in Spectra6 color numbering
const uint8_t u8SpectraAdjust[8] = {
    0, 1, 2, 3, 5, 6, 0, 0
};

// Map the Spectra6 palette to the closest RGB333 values
void CreateSpectra6Pal(void)
{
    int i, j;
    int r, g, b, r1, g1, b1;
    int dist, min_dist, min_index;

    for (i=0; i<512; i++) { // RGB333
        r = (i & 7)*36;
        g = ((i >> 3) & 7)*36;
        b = (i >> 6)*36;
        min_dist = 0x7fffffff;
        min_index = 0;
        for (j=0; j<6; j++) { // match to the closes Spectra6 color
            r1 = iSpectraRGB[j*3];
            g1 = iSpectraRGB[j*3+1];
            b1 = iSpectraRGB[j*3+2];
            dist = (r - r1) * (r - r1); // delta red squared
            dist += (g - g1) * (g - g1); // delta green squared
            dist += (b - b1) * (b - b1); // delta blue squared
            if (dist < min_dist) {
                min_dist = dist;
                min_index = j;
            }
        } // for j
        u8SpectraPal[i] = u8SpectraAdjust[min_index]; // best match palette index for this RGB333 color
    } // for i
} /* CreateSpectra6Pal() */
//
// Convert the RGB value into one of 6 Spectra6 colors
//
uint8_t GetSpectraPixel(int r, int g, int b)
{
uint8_t c;
uint16_t rgb333;

    rgb333 = (r>>5) + ((g & 0xe0) >> 2) + ((b & 0xe0) << 1);
    c = u8SpectraPal[rgb333];
    return c;
} /* GetSpectraPixel() */

/**
 * @brief Callback function for each line of PNG decoded
 * @param PNGDRAW structure containing the current line and relevant info
 * @return none
 */
//
// Draw the PNG image into the local framebuffer memory using the drawPixel() method
// to do color translation and to properly format the memory layout
//
int png_draw_6clr(PNGDRAW *pDraw)
{
    uint8_t pixel, *d, *s, *pPal, *pPalette = pDraw->pPalette;
    int iPitch, r=0, g=0, b=0, x, y, iWidth, iDelta, iBpp = pDraw->iBpp;
    bool bRotated = (pDraw->iWidth == bbep.height() && bbep.width() != bbep.height());

    y = pDraw->y;
    if (bRotated) {
        iWidth = pDraw->iWidth;
        if (iWidth > bbep.height()) iWidth = bbep.height(); // crop to width of EPD
        if (y >= bbep.width()) return 0; // past the bottom of the EPD; stop decoding
    } else {
        iWidth = pDraw->iWidth;
        if (iWidth > bbep.width()) iWidth = bbep.width(); // crop to width of EPD
        if (y >= bbep.height()) return 0;
    }
    iPitch = bbep.width()/2;
    d = (uint8_t *)bbep.getBuffer();
    d += (pDraw->y * iPitch);
    switch (pDraw->iPixelType) {
        case PNG_PIXEL_INDEXED:
            break;
        case PNG_PIXEL_TRUECOLOR:
	        if (iBpp <= 8) {
                iBpp *= 3;
	        }
            pPalette = NULL;
            break;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
	        if (iBpp <= 8) {
                iBpp *= 4;
	        }
            pPalette = NULL;
            break;
        case PNG_PIXEL_GRAYSCALE:
            pPalette = NULL;
            break;
    } // switch on pixel type
    iDelta = iBpp/8;
    s = pDraw->pPixels;
    for (x=0; x<iWidth; x++) { // slower code, but less code :)
        switch (iBpp) {
            case 24:
            case 32:
                r = s[0];
                g = s[1];
                b = s[2];
                s += iDelta;
                break;
            case 16:
                r = s[1] & 0xf8; // red
                g = ((s[0] | s[1] << 8) >> 3) & 0xfc; // green
                b = s[0] << 3;
                s += 2;
                break;
                case 8:
                    if (pPalette) {
                        pPal = &pPalette[s[0] * 3];
                        r = pPal[0];
                        g = pPal[1];
                        b = pPal[2];
                    } else {
                        r = g = b = s[0];
                    }
                    s++;
                    break;
                case 4:
                    if (pPalette) {
                        if (x & 1) {
                            pPal = &pPalette[(s[0] & 0xf) * 3];
                            s++;
                        } else {
                            pPal = &pPalette[(s[0]>>4) * 3];
                        }
                        r = pPal[0];
                        g = pPal[1];
                        b = pPal[2];
                    } else {
                        if (x & 1) {
                            r = g = b = (s[0] & 0xf) | (s[0] << 4);
                            s++;
                        } else {
                            r = g = b = (s[0] >> 4) | (s[0] & 0xf0);
                        }
                    }
                    break;
		case 2:
		    if (pPalette) {
			pPal = &pPalette[((s[0] >> ((3-(x&3))*2)) & 3) * 3];
			r = pPal[0]; g = pPal[1]; b = pPal[2];
		    } else {
			r = g = b = (s[0] << ((x&3)*2)) & 0xc0;
		    }
		    if ((x & 3) == 3) s++;
		    break;
                case 1:
                    if (pPalette) {
                        pPal = &pPalette[((s[0] >> (7-(x&7))) & 1) * 3];
                        r = pPal[0]; g = pPal[1]; b = pPal[2];
                    } else {
                        r = g = b = ((s[0] << (x&7)) & 0x80);
                    }
                    if ((x & 7) == 7) s++;
                    break;
            } // switch on bpp
            if (bRotated) {
                bbep.drawPixel(bbep.width()-1-y, x, GetSpectraPixel(r, g, b));
            } else {
                if (x & 1) {
                    pixel |= GetSpectraPixel(r, g, b);
                    *d++ = pixel;
                } else {
                    pixel = GetSpectraPixel(r, g, b) << 4;
                }
                //bbep.drawPixel(x, y, GetSpectraPixel(r, g, b));
            }
        } // for x
//        Serial.printf("y: %d\n", pDraw->y);
//        if (pDraw->y == 400) return 0;
    return 1; // continue decoding
} /* png_draw_6clr() */

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
        // PNG
        PNG* png = new PNG();
        int rc = png->openRAM(imgBuf, (int)received, png_draw_6clr);
        if (rc != PNG_SUCCESS) {
            Serial.printf("[IMG] PNG open error %d\n", rc);
        } else {
            int srcW = png->getWidth();
            int srcH = png->getHeight();
            Serial.printf("[IMG] PNG %dx%d %dbpp pixtype=%d\n",
                          srcW, srcH, png->getBpp(), png->getPixelType());
            int decRc = png->decode(NULL, 0);
            Serial.printf("[IMG] PNG decode rc=%d lastErr=%d\n", decRc, png->getLastError());
            ok = (decRc == PNG_SUCCESS || decRc == PNG_QUIT_EARLY);
        }
        png->close(); delete png;
//    } else if (imgBuf[0] == 'B' && imgBuf[1] == 'M') {
        // BMP
//        ok = imgDecodeBmp(imgBuf, received);
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
    // Allocate PSRAM local buffer
    bbep.createVirtual(2560, 1440, BBEP_7COLOR); // create memory buffer to draw full EPD image into
    Serial.println("About to create buffer");
    bbep.allocBuffer();
    Serial.println("Created buffer");
    bbep.fillScreen(BBEP_WHITE); // pre-fill with white in case the image is smaller than the display
    Serial.println("Local framebuffer created");
    CreateSpectra6Pal();

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
    // Allocate PSRAM local buffer
    bbep.createVirtual(2560, 1440, BBEP_7COLOR); // create memory buffer to draw full EPD image into
    bbep.fillScreen(BBEP_WHITE); // pre-fill with white in case the image is smaller than the display
    Serial.println("Local framebuffer created");
    CreateSpectra6Pal();

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
    int rc = png->openRAM(pngBuf, (int)received, png_draw_6clr);
    if (rc != PNG_SUCCESS) {
        Serial.printf("[IMG] PNG openRAM error %d\n", rc);
        delete png;
        free(pngBuf);
        return false;
    }

    int srcW = png->getWidth();
    int srcH = png->getHeight();
    Serial.printf("[IMG] PNG: %d x %d, %d bpp pixtype=%d — decoding...\n",
                  srcW, srcH, png->getBpp(), png->getPixelType());

    int decRc = png->decode(NULL, 0);
    Serial.printf("[IMG] decode rc=%d (0=OK, -5=TOO_BIG, -8=QUIT_EARLY)\n", decRc);
    png->close();
    delete png;
    free(pngBuf);

    Serial.println("[IMG] Decode complete");
    return true;
}

void imgFreeBuffers(void)
{
    bbep.freeBuffer();
}
