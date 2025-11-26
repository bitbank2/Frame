#ifndef TCA9535_H
#define TCA9535_H

#include <Arduino.h>
#include <Wire.h>

// TCA9535 I2C Address (default with A0, A1, A2 = LOW)
#define TCA9535_ADDRESS 0x20

// TCA9535 Register Addresses
#define TCA9535_INPUT_PORT0     0x00  // Input port 0
#define TCA9535_INPUT_PORT1     0x01  // Input port 1
#define TCA9535_OUTPUT_PORT0    0x02  // Output port 0
#define TCA9535_OUTPUT_PORT1    0x03  // Output port 1
#define TCA9535_POLARITY_PORT0  0x04  // Polarity inversion port 0
#define TCA9535_POLARITY_PORT1  0x05  // Polarity inversion port 1
#define TCA9535_CONFIG_PORT0    0x06  // Configuration port 0
#define TCA9535_CONFIG_PORT1    0x07  // Configuration port 1

class TCA9535 {
public:
    /**
     * @brief Constructor for TCA9535
     * @param address I2C address of the device (default 0x20)
     * @param wire Pointer to TwoWire object (default &Wire)
     */
    TCA9535(uint8_t address = TCA9535_ADDRESS, TwoWire *wire = &Wire);

    /**
     * @brief Initialize the TCA9535
     * @param sda SDA pin number
     * @param scl SCL pin number
     * @return true if initialization successful
     */
    bool begin(int sda = -1, int scl = -1);

    /**
     * @brief Set pin mode for a single pin
     * @param pin Pin number (0-15)
     * @param mode INPUT (1) or OUTPUT (0)
     */
    void pinMode(uint8_t pin, uint8_t mode);

    /**
     * @brief Write digital value to a pin
     * @param pin Pin number (0-15)
     * @param value HIGH (1) or LOW (0)
     */
    void digitalWrite(uint8_t pin, uint8_t value);

    /**
     * @brief Read digital value from a pin
     * @param pin Pin number (0-15)
     * @return HIGH (1) or LOW (0)
     */
    uint8_t digitalRead(uint8_t pin);

    /**
     * @brief Write to entire port (8 pins at once)
     * @param port Port number (0 or 1)
     * @param value 8-bit value to write
     */
    void writePort(uint8_t port, uint8_t value);

    /**
     * @brief Read entire port (8 pins at once)
     * @param port Port number (0 or 1)
     * @return 8-bit value read from port
     */
    uint8_t readPort(uint8_t port);

    /**
     * @brief Set pin mode for entire port
     * @param port Port number (0 or 1)
     * @param value 8-bit value (1 = INPUT, 0 = OUTPUT for each bit)
     */
    void portMode(uint8_t port, uint8_t value);

    /**
     * @brief Set polarity inversion for a pin
     * @param pin Pin number (0-15)
     * @param inverted true to invert, false for normal
     */
    void setPinPolarity(uint8_t pin, bool inverted);

    /**
     * @brief Write to all 16 pins at once
     * @param value 16-bit value to write
     */
    void write16(uint16_t value);

    /**
     * @brief Read all 16 pins at once
     * @return 16-bit value read
     */
    uint16_t read16();

    /**
     * @brief Check if device is connected
     * @return true if device responds
     */
    bool isConnected();

private:
    uint8_t _address;
    TwoWire *_wire;
    uint8_t _outputPort0;
    uint8_t _outputPort1;
    uint8_t _configPort0;
    uint8_t _configPort1;

    /**
     * @brief Write a byte to a register
     * @param reg Register address
     * @param value Value to write
     */
    void writeRegister(uint8_t reg, uint8_t value);

    /**
     * @brief Read a byte from a register
     * @param reg Register address
     * @return Value read
     */
    uint8_t readRegister(uint8_t reg);
};

#endif // TCA9535_H
