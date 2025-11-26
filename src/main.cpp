#include <Arduino.h>
#include <Wire.h>
#include "TCA9555.h"

// Define pins for ESP32-C5
// TODO: Set your actual I2C pins for ESP32-C5
#define I2C_SDA 2  // Replace with your actual SDA pin
#define I2C_SCL 3  // Replace with your actual SCL pin

// Interrupt pins from TCA9555 expanders
#define GPIO_INT1 28  // Interrupt from first expander
#define GPIO_INT2 27  // Interrupt from second expander

// Define expander pins for your hardware
// Expander 1 pins (0-15)
#define IST9201_EN 15  // IST9201 Enable pin on expander1, pin 15

// Create two TCA9555 objects with different I2C addresses
// Default address is 0x20 (when A0, A1, A2 are all LOW)
// Second device should have different address (set via hardware pins A0, A1, A2)
TCA9555 expander1(0x20);  // First expander at default address
TCA9555 expander2(0x21);  // Second expander (adjust address based on hardware)

// Interrupt flags
volatile bool expander1_interrupt = false;
volatile bool expander2_interrupt = false;

// Interrupt service routines
void IRAM_ATTR expander1_ISR() {
  expander1_interrupt = true;
}

void IRAM_ATTR expander2_ISR() {
  expander2_interrupt = true;
}

// I2C Scanner function to find all devices on the bus
void scanI2C() {
  byte error, address;
  int deviceCount = 0;

  Serial.println("\n========== I2C Bus Scanner ==========");
  Serial.println("Scanning I2C bus (addresses 0x01 to 0x7F)...\n");

  for(address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.print(" (");
      Serial.print(address);
      Serial.print(")");

      // Identify known devices
      if (address >= 0x20 && address <= 0x27) {
        Serial.print(" - Likely TCA9555/TCA9535");
        if (address == 0x20) Serial.print(" (Expander 1)");
        if (address == 0x21) Serial.print(" (Expander 2)");
      } else {
        Serial.print(" - Unknown device (possibly IST9201?)");
      }
      Serial.println();
      deviceCount++;
    }
    else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }

  Serial.println("\n====================================");
  if (deviceCount == 0) {
    Serial.println("No I2C devices found!");
    Serial.println("Check connections and pull-up resistors.");
  } else {
    Serial.print("Total devices found: ");
    Serial.println(deviceCount);
  }
  Serial.println("====================================\n");
}

// Helper functions to control IST9201
void enableIST9201() {
  expander1.write1(IST9201_EN, HIGH);
  Serial.println("IST9201 enabled");
}

void disableIST9201() {
  expander1.write1(IST9201_EN, LOW);
  Serial.println("IST9201 disabled");
}

bool isIST9201Enabled() {
  return expander1.read1(IST9201_EN) == HIGH;
}

// Write to IST9201 via I2C (example function)
bool writeIST9201(uint8_t ist9201_addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(ist9201_addr);
  Wire.write(reg);
  Wire.write(value);
  return (Wire.endTransmission() == 0);
}

// Read from IST9201 via I2C (example function)
uint8_t readIST9201(uint8_t ist9201_addr, uint8_t reg) {
  Wire.beginTransmission(ist9201_addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return 0xFF; // Error
  }

  Wire.requestFrom(ist9201_addr, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0xFF; // Error
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("TCA9555 I/O Expander Test");

  // Initialize I2C bus
  // If I2C_SDA and I2C_SCL are -1, Wire.begin() will use default pins
  if (I2C_SDA != -1 && I2C_SCL != -1) {
    Wire.begin(I2C_SDA, I2C_SCL);
  } else {
    Wire.begin();
    Serial.println("Using default I2C pins - please update I2C_SDA and I2C_SCL defines!");
  }

  // Scan I2C bus to find all connected devices
  scanI2C();

  // Initialize first expander
  // begin() sets all pins as inputs by default
  if (expander1.begin()) {
    Serial.println("Expander 1 (0x20) initialized successfully");
  } else {
    Serial.println("Failed to initialize Expander 1");
  }

  // Initialize second expander
  if (expander2.begin()) {
    Serial.println("Expander 2 (0x21) initialized successfully");
  } else {
    Serial.println("Failed to initialize Expander 2");
  }

  // Setup interrupt pins
  pinMode(GPIO_INT1, INPUT_PULLUP);
  pinMode(GPIO_INT2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(GPIO_INT1), expander1_ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(GPIO_INT2), expander2_ISR, FALLING);
  Serial.println("Interrupts configured on pins 28 and 27");

  // Configure IST9201_EN pin (pin 15 on expander1) as OUTPUT
  expander1.pinMode1(IST9201_EN, OUTPUT);
  Serial.println("IST9201_EN (expander1 pin 15) configured as OUTPUT");

  // Enable IST9201 by setting pin HIGH
  expander1.write1(IST9201_EN, HIGH);
  Serial.println("IST9201_EN set to HIGH (enabled)");

  // Example: Set pins 0-7 of expander1 as outputs, 8-14 as inputs
  // Note: pin 15 is already configured above for IST9201_EN
  expander1.pinMode8(0, 0x00);  // Port 0 (pins 0-7): all outputs (0 = OUTPUT)
  // Port 1 (pins 8-15): pin 15 is output for IST9201_EN, pins 8-14 can be inputs
  // 0x7F = 0b01111111 (bits 0-6 are INPUT, bit 7 is OUTPUT)
  expander1.pinMode8(1, 0x7F);

  // Example: Set pins 0-3 of expander2 as outputs, 4-15 as inputs
  expander2.pinMode8(0, 0xF0);  // Lower 4 bits output, upper 4 input
  expander2.pinMode8(1, 0xFF);  // Port 1: all inputs

  Serial.println("Setup complete");
}

void loop() {
  // Check for interrupts from expander1
  if (expander1_interrupt) {
    expander1_interrupt = false;
    Serial.println("Expander 1 interrupt detected!");

    // Read the input ports to see what changed
    uint16_t inputs = expander1.read16();
    Serial.print("Expander 1 inputs: 0x");
    Serial.println(inputs, HEX);
  }

  // Check for interrupts from expander2
  if (expander2_interrupt) {
    expander2_interrupt = false;
    Serial.println("Expander 2 interrupt detected!");

    // Read the input ports to see what changed
    uint16_t inputs = expander2.read16();
    Serial.print("Expander 2 inputs: 0x");
    Serial.println(inputs, HEX);
  }

  // Example: Toggle pin 0 of expander1
  static unsigned long lastToggle = 0;
  if (millis() - lastToggle > 1000) {
    lastToggle = millis();
    static bool pinState = false;
    pinState = !pinState;

    expander1.write1(0, pinState ? HIGH : LOW);
    Serial.print("Expander 1, Pin 0: ");
    Serial.println(pinState ? "HIGH" : "LOW");
  }

  // Example: Write pattern to entire port 0 of expander2
  static unsigned long lastPattern = 0;
  if (millis() - lastPattern > 2000) {
    lastPattern = millis();
    static uint8_t pattern = 0;
    expander2.write8(0, pattern++);
    Serial.print("Expander 2, Port 0 pattern: 0x");
    Serial.println(pattern - 1, HEX);
  }
}