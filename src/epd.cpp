//=================================================================================================
//                   E2803 EPD Driver for 31.5" Control Board (ESP32-C5)
//
// File Name : epd.cpp
// Based on : EL351TW1.c from E Ink sample code
// Version : 1.0
//=================================================================================================

#define __EPD_C__

#include <Arduino.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>
#include "globals.h"
#include "pindefine.h"
#include "status.h"

extern "C"
{
#include "epd.h"
#include "pmic.h"
#include "comm.h"
}

static SPISettings spiSettings(10000000, MSBFIRST, SPI_MODE0); // 10MHz 1-bit SPI is fine

// SPI CS pin mapping - using TCA9555 expander pins
// 8 chip selects: EXP1_SPI1_CS0-3 (pins 0-3) and EXP1_SPI2_CS0-3 (pins 4-7)
static const unsigned char spiCsPin[8] = {
    EXP1_SPI1_CS0, EXP1_SPI1_CS1, EXP1_SPI1_CS2, EXP1_SPI1_CS3,
    EXP1_SPI2_CS0, EXP1_SPI2_CS1, EXP1_SPI2_CS2, EXP1_SPI2_CS3};

// EPD register configuration values (from E Ink sample code)
const unsigned char PSR_V[2] = {
    0xDB, 0x89};
const unsigned char PWR_V[6] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char POF_V[1] = {
    0x00};
const unsigned char DRF_V[1] = {
    0x01};
const unsigned char CDI_V[1] = {
    0xF7};
const unsigned char TCON_V[2] = {
    0x03, 0x03};
const unsigned char TRES_V[4] = {
    0x03, 0x20, 0x02, 0xD0 // Resolution: 800x720
};
const unsigned char CMD66_V[6] = {
    0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
const unsigned char VCOM_WOUT_EN_V[6] = {
    0x00, 0x00, 0x80, 0xFF, 0xCA, 0x1B};
const unsigned char EN_BUF_V[1] = {
    0x00};
const unsigned char TM_TCON_V[7] = {
    0x00, 0xF4, 0x81, 0x40, 0x00, 0x00, 0x00};
const unsigned char CCSET_V[1] = {
    0x01};
const unsigned char PWS_V[1] = {
    0x22};
const unsigned char SPIM_V[1] = {
    0x00};

// Image data buffer - allocated in PSRAM at runtime
unsigned char *epdImageDataBuffer = nullptr;

// Tracks whether PMIC_PG stayed HIGH across the busy-wait loops in epdDisplay()
static unsigned char pgWasGood = 1;

//================== GPIO Control Functions ====================================

/**
 * @brief Set EPD reset pin level
 */
static void epdResetPin(unsigned int pinStatus)
{
    // digitalWrite(EPD_RST, pinStatus);
    expander2.write1(EXP2_EPD_RST, pinStatus);
    delayms(10);
    expander2.pinMode1(EXP2_EPD_RST, OUTPUT);
}

/**
 * @brief Set all chip select pins to specified level
 * @param setLevel: GPIO_HIGH or GPIO_LOW
 */
void epdSetCsAll(unsigned int setLevel)
{
    // for(int i = 0; i < 8; i++)
    // {
    //     expander1.write1(spiCsPin[i], setLevel);
    // }
    expander1.write1(EXP1_SPI1_CS0, setLevel);
    expander1.pinMode1(EXP1_SPI1_CS0, OUTPUT);
    expander1.write1(EXP1_SPI1_CS1, setLevel);
    expander1.pinMode1(EXP1_SPI1_CS1, OUTPUT);
    expander1.write1(EXP1_SPI1_CS2, setLevel);
    expander1.pinMode1(EXP1_SPI1_CS2, OUTPUT);
    expander1.write1(EXP1_SPI1_CS3, setLevel);
    expander1.pinMode1(EXP1_SPI1_CS3, OUTPUT);
    expander1.write1(EXP1_SPI2_CS0, setLevel);
    expander1.pinMode1(EXP1_SPI2_CS0, OUTPUT);
    expander1.write1(EXP1_SPI2_CS1, setLevel);
    expander1.pinMode1(EXP1_SPI2_CS1, OUTPUT);
    expander1.write1(EXP1_SPI2_CS2, setLevel);
    expander1.pinMode1(EXP1_SPI2_CS2, OUTPUT);
    expander1.write1(EXP1_SPI2_CS3, setLevel);
    expander1.pinMode1(EXP1_SPI2_CS3, OUTPUT);
}

/**
 * @brief Set specific chip select pin
 * @param csNumber: CS index (0-7)
 * @param setLevel: GPIO_HIGH or GPIO_LOW
 */
void epdSetCs(unsigned char csNumber, unsigned int setLevel)
{
    if (csNumber < 8)
    {
        expander1.write1(spiCsPin[csNumber], setLevel);
        expander1.pinMode1(spiCsPin[csNumber], OUTPUT);
    }
}

/**
 * @brief Wait for BUSY pin to go HIGH
 */
void epdCheckBusyHigh(void)
{
    //   expander2.write1(12, HIGH);// for debug only
    //   expander2.pinMode1(12, OUTPUT);// for debug only
    int timeout = 60000; // 60 second timeout (31.5" ACeP needs up to 30s refresh)
    int elapsed = 0;
    while (!(!digitalRead(EPD_BUSY))) // added invertion because Hardware FIX mosfet
    {
        if (!getGpioLevel(PMIC_PG))
        {
            pgWasGood = 0; // no printf here, epdDisplay() reports this once at the end
        }

        delayms(1);
        elapsed++;
        if (elapsed >= timeout)
        {
            printf("ERROR: EPD BUSY timeout (waiting for HIGH)\n");
            return;
        }
    }
    Serial.println("Busy_pin ");
    Serial.println(elapsed);
}

/**
 * @brief Wait for BUSY pin to go LOW
 */
void epdCheckBusyLow(void)
{
    int timeout = 60000; // 60 second timeout
    int elapsed = 0;
    while (!digitalRead(EPD_BUSY)) // added invertion because Hardware FIX
    {
        delayms(1);
        elapsed++;
        if (elapsed >= timeout)
        {
            printf("ERROR: EPD BUSY timeout (waiting for LOW)\n");
            return;
        }
    }
}

//================== Hardware Reset ====================================

void epdHardwareReset(void)
{
    printf("EPD Hardware Reset...\n");
    epdResetPin(GPIO_LOW);
    delayms(20);
    epdResetPin(GPIO_HIGH);
    delayms(20);
    printf("EPD Hardware Reset complete\n");
}

//================== SPI Communication ====================================

/**
 * @brief Write command and data to EPD via SPI
 */
void epdWrite(unsigned char epdCommand, const unsigned char *epdData, unsigned int epdDataLength)
{
    spiTransmit(epdCommand, (unsigned char *)epdData, epdDataLength);
}

/**
 * @brief Read data from EPD via SPI
 */
void epdRead(unsigned char epdCommand, unsigned char *epdData, unsigned int epdDataLength)
{
    spiReceive(epdCommand, epdData, epdDataLength);
}

/**
 * @brief Write command only (no data) to EPD
 */
void epdWriteCommand(unsigned char epdCommand)
{
    spiTransmitCommand(epdCommand);
}

/**
 * @brief Write data only (after command) to EPD
 */
void epdWriteData(const unsigned char *epdData, unsigned int epdDataLength)
{
    spiTransmitData((unsigned char *)epdData, epdDataLength);
}

//================== EPD Initialization ====================================

void epdInit(void)
{
    printf("Initializing EPD...\n");

    // Allocate image buffer in PSRAM
    if (epdImageDataBuffer == nullptr)
    {
        epdImageDataBuffer = (unsigned char *)ps_malloc(EPD_IMAGE_DATA_BUFFER);
        if (epdImageDataBuffer == nullptr)
        {
            printf("[EPD] ERROR: Failed to allocate image buffer in PSRAM!\n");
        }
        else
        {
            printf("[EPD] Image buffer allocated in PSRAM (%u bytes)\n", EPD_IMAGE_DATA_BUFFER);
        }
    }

    // Hardware reset
    epdHardwareReset();
    epdCheckBusyHigh();

    // Initialize all driver ICs with same configuration
    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_VCOM_WOUT_EN, VCOM_WOUT_EN_V, sizeof(VCOM_WOUT_EN_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_TM_TCON, TM_TCON_V, sizeof(TM_TCON_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_CMD66, CMD66_V, sizeof(CMD66_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_PSR, PSR_V, sizeof(PSR_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_PWR, PWR_V, sizeof(PWR_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_CDI, CDI_V, sizeof(CDI_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_TCON, TCON_V, sizeof(TCON_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_TRES, TRES_V, sizeof(TRES_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_EN_BUF, EN_BUF_V, sizeof(EN_BUF_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_PWS, PWS_V, sizeof(PWS_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_SPIM, SPIM_V, sizeof(SPIM_V));
    epdSetCsAll(GPIO_HIGH);

    epdSetCsAll(GPIO_LOW);
    epdWrite(EPD_CMD_CCSET, CCSET_V, sizeof(CCSET_V));
    epdSetCsAll(GPIO_HIGH);

    printf("EPD initialization complete\n");
}

//================== Driver IC Status Check ====================================

unsigned char epdCheckDriverICStatus(void)
{
    unsigned char csx, status = DONE;
    unsigned char dataBuf[3];

    printf("Checking driver IC status...\n");

    for (csx = 0; csx < 8; csx++)
    {
        memset(dataBuf, 0, sizeof(dataBuf));
        epdSetCs(csx, GPIO_LOW);
        delayms(10);
        epdRead(0xF2, dataBuf, sizeof(dataBuf));
        epdSetCs(csx, GPIO_HIGH);

#if SHOW_LOG
        printf("Driver IC [%d] = 0x%02X 0x%02X 0x%02X\n", csx, dataBuf[0], dataBuf[1], dataBuf[2]);
#endif

        if ((dataBuf[0] & 0x01) == 0x01)
        {
#if SHOW_LOG
            printf("Driver IC [%d] is ready.\n", csx);
#endif
        }
        else
        {
#if SHOW_LOG
            printf("Driver IC [%d] did not reply.\n", csx);
#endif
            status = ERROR;
        }
    }

    return status;
}

//================== Power Management ====================================

static void epdSetPwrToPmic(unsigned char *pmicData)
{
    unsigned int buf;

    buf = voltageToRegisterData(pmicData[0], SET_VPOS_VENG2);
    initPmicData[4] = buf / 256;
    initPmicData[5] = buf % 256;

    buf = voltageToRegisterData(pmicData[1], SET_VPOS_VENG3);
    initPmicData[8] = buf / 256;
    initPmicData[9] = buf % 256;

    buf = voltageToRegisterData(pmicData[2], SET_VPOS_VENG2);
    initPmicData[6] = buf / 256;
    initPmicData[7] = buf % 256;

    buf = voltageToRegisterData(pmicData[3], SET_VPOS_VENG3);
    initPmicData[10] = buf / 256;
    initPmicData[11] = buf % 256;

    buf = voltageToRegisterData(pmicData[4], SET_VCOMDC);
    initPmicData[12] = buf / 256;
    initPmicData[13] = buf % 256;
}

//================== Display Functions ====================================

void epdDisplay(void)
{
#if SHOW_LOG
    //   expander2.write1(12, HIGH);// for debug only
    //   expander2.pinMode1(12, OUTPUT);// for debug only
    printf("Turn on PMIC.\n");
#endif
    if (enablePmic() == DONE)
    {
        pgWasGood = 1; // reset before tracking PMIC_PG across the busy-wait loops below

#if SHOW_LOG
        printf("Write PON\n");
#endif
        epdSetCsAll(GPIO_LOW);
        epdWriteCommand(EPD_CMD_PON);
        epdCheckBusyHigh();
        epdSetCsAll(GPIO_HIGH);

#if SHOW_LOG
        printf("Write DRF\n");
#endif
        epdSetCsAll(GPIO_LOW);
        delayms(30);
        epdWrite(EPD_CMD_DRF, DRF_V, sizeof(DRF_V));
        epdCheckBusyHigh();
        epdSetCsAll(GPIO_HIGH);

#if SHOW_LOG
        printf("Write POF\n");
#endif
        epdSetCsAll(GPIO_LOW);
        epdWrite(EPD_CMD_POF, POF_V, sizeof(POF_V));
        epdSetCsAll(GPIO_HIGH);
#if SHOW_LOG
        if (!pgWasGood)
        {
            printf("WARNING: PMIC_PG == LOW ==\n");
        }
#endif
    }
    else
    {
#if SHOW_LOG
        printf("PMIC enable failed - skipping display refresh.\n");
#endif
    }

#if SHOW_LOG
    printf("Turn off PMIC.\n");
#endif
    disablePmic();

#if SHOW_LOG
    printf("Display Done!!\n");
#endif
}

void epdDisplayColor(unsigned char colorSelect)
{
    unsigned long i;

    printf("Displaying solid color: 0x%02X\n", colorSelect);

    memset(epdImageDataBuffer, colorSelect, EPD_IMAGE_DATA_BUFFER);

    if (epdSetPower() == DONE)
    {
        epdSetCsAll(GPIO_LOW);
        epdWriteCommand(EPD_CMD_DTM);

        // Send image data in chunks
        for (i = 0; i < 288000 / EPD_IMAGE_DATA_BUFFER; i++)
        {
            epdWriteData(epdImageDataBuffer, EPD_IMAGE_DATA_BUFFER);
        }
        epdWriteData(epdImageDataBuffer, 288000 % EPD_IMAGE_DATA_BUFFER);
        epdSetCsAll(GPIO_HIGH);

        epdDisplay();

#if SHOW_LOG
        printf("Display color complete.\n");
#endif
    }
    else
    {
#if SHOW_LOG
        printf("Display color does not work due to epdSetPower() NG.\n");
#endif
    }
}

void epdDisplayColorBar(void)
{
    unsigned long i;

    printf("Displaying color bar pattern...\n");

    if (epdSetPower() == DONE)
    {
        epdSetCsAll(GPIO_LOW);
        epdWriteCommand(EPD_CMD_DTM);

        // BLACK (48000 bytes = 1/6 of screen)
        memset(epdImageDataBuffer, EPD_BLACK, EPD_IMAGE_DATA_BUFFER);
        for (i = 0; i < 48000 / EPD_IMAGE_DATA_BUFFER; i++)
        {
            epdWriteData(epdImageDataBuffer, EPD_IMAGE_DATA_BUFFER);
        }
        epdWriteData(epdImageDataBuffer, 48000 % EPD_IMAGE_DATA_BUFFER);

        // WHITE
        memset(epdImageDataBuffer, EPD_WHITE, EPD_IMAGE_DATA_BUFFER);
        for (i = 0; i < 48000 / EPD_IMAGE_DATA_BUFFER; i++)
        {
            epdWriteData(epdImageDataBuffer, EPD_IMAGE_DATA_BUFFER);
        }
        epdWriteData(epdImageDataBuffer, 48000 % EPD_IMAGE_DATA_BUFFER);

        // YELLOW
        memset(epdImageDataBuffer, EPD_YELLOW, EPD_IMAGE_DATA_BUFFER);
        for (i = 0; i < 48000 / EPD_IMAGE_DATA_BUFFER; i++)
        {
            epdWriteData(epdImageDataBuffer, EPD_IMAGE_DATA_BUFFER);
        }
        epdWriteData(epdImageDataBuffer, 48000 % EPD_IMAGE_DATA_BUFFER);

        // RED
        memset(epdImageDataBuffer, EPD_RED, EPD_IMAGE_DATA_BUFFER);
        for (i = 0; i < 48000 / EPD_IMAGE_DATA_BUFFER; i++)
        {
            epdWriteData(epdImageDataBuffer, EPD_IMAGE_DATA_BUFFER);
        }
        epdWriteData(epdImageDataBuffer, 48000 % EPD_IMAGE_DATA_BUFFER);

        // BLUE
        memset(epdImageDataBuffer, EPD_BLUE, EPD_IMAGE_DATA_BUFFER);
        for (i = 0; i < 48000 / EPD_IMAGE_DATA_BUFFER; i++)
        {
            epdWriteData(epdImageDataBuffer, EPD_IMAGE_DATA_BUFFER);
        }
        epdWriteData(epdImageDataBuffer, 48000 % EPD_IMAGE_DATA_BUFFER);

        // GREEN
        memset(epdImageDataBuffer, EPD_GREEN, EPD_IMAGE_DATA_BUFFER);
        for (i = 0; i < 48000 / EPD_IMAGE_DATA_BUFFER; i++)
        {
            epdWriteData(epdImageDataBuffer, EPD_IMAGE_DATA_BUFFER);
        }
        epdWriteData(epdImageDataBuffer, 48000 % EPD_IMAGE_DATA_BUFFER);

        epdSetCsAll(GPIO_HIGH);

        epdDisplay();

#if SHOW_LOG
        printf("Display color bar complete.\n");
#endif
    }
    else
    {
#if SHOW_LOG
        printf("Display color bar does not work due to epdSetPower() NG.\n");
#endif
    }
}
//
// Write the image data from the local framebuffer into each of the 8 EPD controllers
// The physical layout of the controllers is portrait mode, 1440 wide by 2560 tall. We treat the
// display as if it's landscape 2560 x 1440 and must rotate the lines as we write them.
//
// Each of the 8 controllers manages a vertical column of pixels (in landscape orientation) from the top
// of the display to the bottom. They are all 400x720 bytes, but not all are of each controller's pixels
// are visible. From left to right, here are the visible widths of each controller (in pixels):
// 0: 400, 1: 400, 2: 400, 3: 80, 4: 400, 5: 400, 6: 400, 7: 80
// 400+400+400+80+400+400+400+80 = 2560 pixels wide
// The controllers with only 80 visible columns still need the same number of bytes written to them
// as the other controllers; columns 81-399 are not visible.
//
// Within each vertical bar, the bytes are laid out as follows. Each 400 byte row of bytes defines
// 2 rows of 400 pixels, with the bytes oriented vertically and the most significant nibble on the top
// e.g. here are 4 bytes which represent a 4x2 block of pixels:
// +---+---+---+---+
// |B0H|B1H|B2H|B3H| pixels (0,0),(1,0),(2,0),(3,0) from the high nibbles of bytes 0-3
// +---+---+---+---+
// |B0L|B1L|B2L|B3L| pixels (0,1),(1,1),(2,1),(3,1) from the low nibbles of bytes 0-3
// +---+---+---+---+
//
void epdWriteImage(void *pFrameBuffer)       
{
int x, tx, ty, tw, i;
int csx, iWidth, iPitch;
uint8_t uc, *s, *d;
uint8_t *u8LineBuf;
const int pix_offs[8] = {0, 200, 400, 600, 640, 840, 1040, 1240};

    u8LineBuf = (uint8_t *)malloc(400);
    iPitch = 2560/2; // bytes per row of source image
    Serial.println("[EPD] Writing image data to EPD"); 
    for (csx=0; csx<8; csx++) { // 8 horizontal regions 
        s = (uint8_t *)pFrameBuffer;
        s += pix_offs[csx]; // 200 bytes per 400 pixel region
        epdSetCs(csx, GPIO_LOW);
        SPI.beginTransaction(spiSettings); // DEBUG - convert to QSPI
        SPI.transfer(EPD_CMD_DTM);
        for (ty = 0; ty < 1440; ty+=2) { // write 800 x 720 pixels into this controller's RAM
            uint8_t *ts = s;
            d = u8LineBuf;
            tw = (csx == 3 || csx == 7) ? 80 : 400;
            for (tx=0; tx<tw; tx+=2) {
                *d++ = (ts[0] & 0xf0) | (ts[iPitch] >> 4);
                *d++ = (ts[0] << 4) | (ts[iPitch] & 0xf);
                ts++;
            }
            SPI.writeBytes(u8LineBuf, 400);
            s += (iPitch * 2); // next pair of lines
        } // per row of this section
        SPI.endTransaction();
        epdSetCs(csx, GPIO_HIGH);
    } // per horizontal section
    free(u8LineBuf);
} /* epdWriteImage() */

unsigned char epdSetPower(void)
{
    unsigned char i, vcomStatus = DONE;
    unsigned char readTscBuf[2], readPwrBuf[5];
    // Read TSC
    epdSetCs(0, GPIO_LOW);
    epdRead(EPD_CMD_TSC, &readTscBuf[0], sizeof(readTscBuf));
    epdCheckBusyHigh();
    epdSetCs(0, GPIO_HIGH);
#if SHOW_LOG
    printf("TSC Data = 0x%02X, 0x%02X \r\n", readTscBuf[0], readTscBuf[1]);
#endif

    // PON without the external power
    epdSetCs(0, GPIO_LOW);
    epdWriteCommand(EPD_CMD_PON);
    epdCheckBusyHigh();
    epdSetCs(0, GPIO_HIGH);

    // Read PWR
    epdSetCs(0, GPIO_LOW);
    epdRead(0x9B, &readPwrBuf[0], 4);
    epdSetCs(0, GPIO_HIGH);
#if SHOW_LOG
    printf("PWR Data = 0x%02X, 0x%02X 0x%02X, 0x%02X \r\n", readPwrBuf[0], readPwrBuf[1], readPwrBuf[2], readPwrBuf[3]);
#endif

    // POF
    epdSetCs(0, GPIO_LOW);
    epdWriteCommand(EPD_CMD_POF);
    epdCheckBusyHigh();
    epdSetCs(0, GPIO_HIGH);

    // Read VCOM
    epdSetCs(0, GPIO_LOW);
    epdRead(0x8A, &readPwrBuf[4], 1);
    epdSetCs(0, GPIO_HIGH);

    if (readPwrBuf[4] == 0x00)
    {
#if SHOW_LOG
        printf("VCOM Data = 0x%02X \r\n", readPwrBuf[4]);
        printf("Data NG! \r\n");
#endif
        vcomStatus = ERROR;
    }
    else
    {
        for (i = 0; i < 4; i++)
        {
            if (readPwrBuf[i] > 120)
            {
                vcomStatus = ERROR;
#if SHOW_LOG
                printf("PWM Data [%d] = 0x%02X \r\n", i, readPwrBuf[4]);
                printf("Data NG! \r\n");
#endif
            }
        }

        if (vcomStatus == DONE)
        {
            readPwrBuf[4] = readPwrBuf[4] - 128;
            epdSetPwrToPmic(readPwrBuf);
#if SHOW_LOG
            printf("PWM data and VCOM data are both in range! \r\n");
#endif
        }
    }

    return vcomStatus;
}
