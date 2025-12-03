//=================================================================================================
//                   Communication interface for 31.5" Control Board
//
// File Name : comm.cpp
// Author : Electronic Design Dept. II
// Data : 2023.11.13
// Version : 1.0
// Copyright : E Ink Holdings Inc.
//=================================================================================================

#define __COMM_C__

#include <Arduino.h>
#include <Wire.h>
#include "globals.h"

extern "C" {
#include "comm.h"
#include "pmic.h"
}

#ifdef BLACK_BOARD
// Create a separate I2C instance for PMIC on black board
TwoWire WirePmic = TwoWire(0);
#endif

//=================================================================================================
// I2C Communication Functions
//=================================================================================================

/**
 * @brief Transmit data via I2C
 * @param i2cAddress: 7-bit I2C slave address
 * @param dataBuffer: Pointer to data buffer to transmit
 * @param dataLength: Number of bytes to transmit
 * @return 1 on success, 0 on failure
 */
unsigned char i2cTransmitData(unsigned char i2cAddress, unsigned char *dataBuffer, unsigned int dataLength)
{
    if (dataBuffer == NULL || dataLength == 0) {
        return 0; // Invalid parameters
    }

#ifdef BLACK_BOARD
    // Black board uses separate I2C bus for PMIC
    TwoWire* wireInstance = (i2cAddress == PMIC_ADDR) ? &WirePmic : &Wire;
    wireInstance->beginTransmission(i2cAddress);

    for (unsigned int i = 0; i < dataLength; i++) {
        wireInstance->write(dataBuffer[i]);
    }

    unsigned char error = wireInstance->endTransmission();
#else
    // Green board uses single I2C bus
    Wire.beginTransmission(i2cAddress);

    // Write all bytes from the buffer
    for (unsigned int i = 0; i < dataLength; i++) {
        Wire.write(dataBuffer[i]);
    }

    // endTransmission returns:
    // 0: success
    // 1: data too long to fit in transmit buffer
    // 2: received NACK on transmit of address
    // 3: received NACK on transmit of data
    // 4: other error
    unsigned char error = Wire.endTransmission();
#endif

    return (error == 0) ? 1 : 0; // Return 1 on success, 0 on failure
}

/**
 * @brief Receive data via I2C
 * @param i2cAddress: 7-bit I2C slave address
 * @param dataBuffer: Pointer to buffer to store received data
 * @param dataLength: Number of bytes to receive
 * @return 1 on success, 0 on failure
 */
unsigned char i2cReceiveData(unsigned char i2cAddress, unsigned char *dataBuffer, unsigned int dataLength)
{
    if (dataBuffer == NULL || dataLength == 0) {
        return 0; // Invalid parameters
    }

#ifdef BLACK_BOARD
    // Black board uses separate I2C bus for PMIC
    TwoWire* wireInstance = (i2cAddress == PMIC_ADDR) ? &WirePmic : &Wire;
    unsigned char bytesReceived = wireInstance->requestFrom(i2cAddress, (uint8_t)dataLength);

    if (bytesReceived != dataLength) {
        return 0; // Didn't receive expected number of bytes
    }

    for (unsigned int i = 0; i < dataLength; i++) {
        if (wireInstance->available()) {
            dataBuffer[i] = wireInstance->read();
        } else {
            return 0; // No more data available
        }
    }
#else
    // Green board uses single I2C bus
    // Request bytes from I2C slave
    unsigned char bytesReceived = Wire.requestFrom(i2cAddress, (uint8_t)dataLength);

    if (bytesReceived != dataLength) {
        return 0; // Didn't receive expected number of bytes
    }

    // Read all received bytes into buffer
    for (unsigned int i = 0; i < dataLength; i++) {
        if (Wire.available()) {
            dataBuffer[i] = Wire.read();
        } else {
            return 0; // No more data available
        }
    }
#endif

    return 1; // Success
}

//=================================================================================================
// Delay Function
//=================================================================================================

/**
 * @brief Delay in milliseconds
 * @param delayTime: Delay time in milliseconds
 */
void delayms(unsigned int delayTime)
{
    delay(delayTime);
}

//=================================================================================================
// SPI Communication Functions (Placeholder - implement based on your hardware)
//=================================================================================================

unsigned char spiTransmitCommand(unsigned char commandBuf)
{
    // TODO: Implement SPI command transmit based on your hardware
    return 0;
}

unsigned char spiTransmitData(unsigned char *dataBuffer, unsigned long dataLength)
{
    // TODO: Implement SPI data transmit based on your hardware
    return 0;
}

unsigned char spiTransmitLargeData(unsigned char commandBuf, unsigned char *dataBuffer, unsigned long dataLength)
{
    // TODO: Implement SPI large data transmit based on your hardware
    return 0;
}

unsigned char spiTransmit(unsigned char commandBuf, unsigned char *dataBuffer, unsigned int dataLength)
{
    // TODO: Implement SPI transmit based on your hardware
    return 0;
}

unsigned char spiReceive(unsigned char commandBuf, unsigned char *dataBuffer, unsigned int dataLength)
{
    // TODO: Implement SPI receive based on your hardware
    return 0;
}

//=================================================================================================
// GPIO Functions (Board-specific implementation)
//=================================================================================================

/**
 * @brief Set GPIO pin level
 * @param pinNumber: GPIO pin number
 * @param voltageLevel: HIGH or LOW
 */
void setGpioLevel(unsigned char pinNumber, unsigned char voltageLevel)
{
#ifdef GREEN_BOARD
    // For green board, use expander for specific pins
    expander1.write1(pinNumber, voltageLevel);
#else
    // For black board, use direct GPIO
    digitalWrite(pinNumber, voltageLevel);
#endif
}

/**
 * @brief Get GPIO pin level
 * @param pinNumber: GPIO pin number
 * @return Current pin level (HIGH or LOW)
 */
unsigned char getGpioLevel(unsigned char pinNumber)
{
    return digitalRead(pinNumber);
}
