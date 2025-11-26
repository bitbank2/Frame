#include "TCA9535.h"

TCA9535::TCA9535(uint8_t address, TwoWire *wire) {
    _address = address;
    _wire = wire;
    _outputPort0 = 0x00;
    _outputPort1 = 0x00;
    _configPort0 = 0xFF; // All inputs by default
    _configPort1 = 0xFF; // All inputs by default
}

bool TCA9535::begin(int sda, int scl) {
    if (sda != -1 && scl != -1) {
        _wire->begin(sda, scl);
    } else {
        _wire->begin();
    }

    // Check if device is connected
    if (!isConnected()) {
        return false;
    }

    // Initialize with default configuration (all pins as inputs)
    writeRegister(TCA9535_CONFIG_PORT0, _configPort0);
    writeRegister(TCA9535_CONFIG_PORT1, _configPort1);

    return true;
}

void TCA9535::pinMode(uint8_t pin, uint8_t mode) {
    if (pin > 15) return;

    uint8_t port = pin / 8;
    uint8_t bit = pin % 8;

    if (port == 0) {
        if (mode == INPUT) {
            _configPort0 |= (1 << bit);
        } else {
            _configPort0 &= ~(1 << bit);
        }
        writeRegister(TCA9535_CONFIG_PORT0, _configPort0);
    } else {
        if (mode == INPUT) {
            _configPort1 |= (1 << bit);
        } else {
            _configPort1 &= ~(1 << bit);
        }
        writeRegister(TCA9535_CONFIG_PORT1, _configPort1);
    }
}

void TCA9535::digitalWrite(uint8_t pin, uint8_t value) {
    if (pin > 15) return;

    uint8_t port = pin / 8;
    uint8_t bit = pin % 8;

    if (port == 0) {
        if (value == HIGH) {
            _outputPort0 |= (1 << bit);
        } else {
            _outputPort0 &= ~(1 << bit);
        }
        writeRegister(TCA9535_OUTPUT_PORT0, _outputPort0);
    } else {
        if (value == HIGH) {
            _outputPort1 |= (1 << bit);
        } else {
            _outputPort1 &= ~(1 << bit);
        }
        writeRegister(TCA9535_OUTPUT_PORT1, _outputPort1);
    }
}

uint8_t TCA9535::digitalRead(uint8_t pin) {
    if (pin > 15) return LOW;

    uint8_t port = pin / 8;
    uint8_t bit = pin % 8;
    uint8_t value;

    if (port == 0) {
        value = readRegister(TCA9535_INPUT_PORT0);
    } else {
        value = readRegister(TCA9535_INPUT_PORT1);
    }

    return (value & (1 << bit)) ? HIGH : LOW;
}

void TCA9535::writePort(uint8_t port, uint8_t value) {
    if (port > 1) return;

    if (port == 0) {
        _outputPort0 = value;
        writeRegister(TCA9535_OUTPUT_PORT0, _outputPort0);
    } else {
        _outputPort1 = value;
        writeRegister(TCA9535_OUTPUT_PORT1, _outputPort1);
    }
}

uint8_t TCA9535::readPort(uint8_t port) {
    if (port > 1) return 0;

    if (port == 0) {
        return readRegister(TCA9535_INPUT_PORT0);
    } else {
        return readRegister(TCA9535_INPUT_PORT1);
    }
}

void TCA9535::portMode(uint8_t port, uint8_t value) {
    if (port > 1) return;

    if (port == 0) {
        _configPort0 = value;
        writeRegister(TCA9535_CONFIG_PORT0, _configPort0);
    } else {
        _configPort1 = value;
        writeRegister(TCA9535_CONFIG_PORT1, _configPort1);
    }
}

void TCA9535::setPinPolarity(uint8_t pin, bool inverted) {
    if (pin > 15) return;

    uint8_t port = pin / 8;
    uint8_t bit = pin % 8;
    uint8_t reg = (port == 0) ? TCA9535_POLARITY_PORT0 : TCA9535_POLARITY_PORT1;

    uint8_t value = readRegister(reg);

    if (inverted) {
        value |= (1 << bit);
    } else {
        value &= ~(1 << bit);
    }

    writeRegister(reg, value);
}

void TCA9535::write16(uint16_t value) {
    _outputPort0 = value & 0xFF;
    _outputPort1 = (value >> 8) & 0xFF;
    writeRegister(TCA9535_OUTPUT_PORT0, _outputPort0);
    writeRegister(TCA9535_OUTPUT_PORT1, _outputPort1);
}

uint16_t TCA9535::read16() {
    uint8_t port0 = readRegister(TCA9535_INPUT_PORT0);
    uint8_t port1 = readRegister(TCA9535_INPUT_PORT1);
    return (uint16_t)port0 | ((uint16_t)port1 << 8);
}

bool TCA9535::isConnected() {
    _wire->beginTransmission(_address);
    return (_wire->endTransmission() == 0);
}

void TCA9535::writeRegister(uint8_t reg, uint8_t value) {
    _wire->beginTransmission(_address);
    _wire->write(reg);
    _wire->write(value);
    _wire->endTransmission();
}

uint8_t TCA9535::readRegister(uint8_t reg) {
    _wire->beginTransmission(_address);
    _wire->write(reg);
    _wire->endTransmission();

    _wire->requestFrom(_address, (uint8_t)1);
    if (_wire->available()) {
        return _wire->read();
    }
    return 0;
}
