#include <Arduino.h>
#include <Wire.h>
#include "pindefine.h"

extern "C" {
#include "pmic.h"
#include "comm.h"
}

#ifdef GREEN_BOARD
#include "TCA9555.h"
#include "globals.h"

// Define pins for ESP32-C5
#define I2C_SDA 2
#define I2C_SCL 3

// Interrupt pins from TCA9555 expanders
#define GPIO_INT1 28
#define GPIO_INT2 27

// Create two TCA9555 objects with different I2C addresses
TCA9555 expander1(0x20);  // First expander at default address
TCA9555 expander2(0x21);  // Second expander (adjust address based on hardware)

// Interrupt flags
volatile bool expander1_interrupt = false;
volatile bool expander2_interrupt = false;

void expanders_voltage_gpio_init(){
  expander1.pinMode1(EXP1_EN_V5A, OUTPUT);
  expander2.pinMode1(EXP2_EN_NEGATIVE_3_5V, OUTPUT);
  expander2.pinMode1(EXP2_EN_1_35V, OUTPUT);
  expander2.pinMode1(EXP2_EN_V5D, OUTPUT);
  expander2.pinMode1(EXP2_EN_DC12, OUTPUT);
  expander2.pinMode1(EXP2_EN_DC19, OUTPUT);
}

void gpio_cs_pins_init(){
  expander1.pinMode1(EXP1_SPI1_CS3, OUTPUT);
  expander1.pinMode1(EXP1_SPI1_CS2, OUTPUT);
  expander1.pinMode1(EXP1_SPI1_CS1, OUTPUT);
  expander1.pinMode1(EXP1_SPI1_CS0, OUTPUT);
  expander1.pinMode1(EXP1_SPI2_CS3, OUTPUT);
  expander1.pinMode1(EXP1_SPI2_CS2, OUTPUT);
  expander1.pinMode1(EXP1_SPI2_CS1, OUTPUT);
  expander1.pinMode1(EXP1_SPI2_CS0, OUTPUT);
}

// Interrupt service routines
void IRAM_ATTR expander1_ISR() {
  expander1_interrupt = true;
}

void IRAM_ATTR expander2_ISR() {
  expander2_interrupt = true;
}

// I2C Scanner function
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

      if (address >= 0x20 && address <= 0x27) {
        Serial.print(" - Likely TCA9555/TCA9535");
        if (address == 0x20) Serial.print(" (Expander 1)");
        if (address == 0x21) Serial.print(" (Expander 2)");
      } else if (address == 0x48) {
        Serial.print(" - PMIC");
      } else {
        Serial.print(" - Unknown device");
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

#endif // GREEN_BOARD

#ifdef BLACK_BOARD
// Black board - ESP32-S3 with direct GPIO control

void gpio_init() {
  // Initialize GPIO pins as outputs
  pinMode(PMIC_EN, OUTPUT);
  pinMode(VDDP_SW, OUTPUT);
  pinMode(VDDN_SW, OUTPUT);
  pinMode(VNCP_SW, OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  pinMode(PMIC_PS, OUTPUT);

  // Initialize input pins
  pinMode(PMIC_PG, INPUT);
  pinMode(EPD_BUSY, INPUT);

  // Set initial states
  digitalWrite(PMIC_EN, LOW);
  digitalWrite(VDDP_SW, LOW);
  digitalWrite(VDDN_SW, LOW);
  digitalWrite(VNCP_SW, LOW);
  digitalWrite(EPD_RST, LOW);
  digitalWrite(PMIC_PS, LOW);

  Serial.println("GPIO pins initialized");
}

void scanI2C(TwoWire &wire, const char* busName) {
  byte error, address;
  int deviceCount = 0;

  Serial.print("\n========== ");
  Serial.print(busName);
  Serial.println(" I2C Bus Scanner ==========");
  Serial.println("Scanning I2C bus (addresses 0x01 to 0x7F)...\n");

  for(address = 1; address < 127; address++) {
    wire.beginTransmission(address);
    error = wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.print(" (");
      Serial.print(address);
      Serial.print(")");

      if (address == 0x48) {
        Serial.print(" - PMIC");
      } else {
        Serial.print(" - Unknown device");
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

#endif // BLACK_BOARD

void setup() {
  Serial.begin(115200);
  delay(2000);

  // Wait for Serial to be ready (important for USB CDC)
  while (!Serial && millis() < 5000) {
    delay(10);
  }

#ifdef GREEN_BOARD
  Serial.println("\n\n=== ESP32-C5 GREEN BOARD Starting ===");
  Serial.println("Board: Green Board with TCA9555 Expanders");
  Serial.println();

  Serial.println("TCA9555 I/O Expander Test");
  Serial.println("Initializing I2C...");

  Serial.print("Calling Wire.begin(");
  Serial.print(I2C_SDA);
  Serial.print(", ");
  Serial.print(I2C_SCL);
  Serial.println(")...");

  if (!Wire.begin(I2C_SDA, I2C_SCL, 100000)) {
    Serial.println("ERROR: Wire.begin() with frequency FAILED!");
    Serial.println("Trying without frequency parameter...");

    if (!Wire.begin(I2C_SDA, I2C_SCL)) {
      Serial.println("ERROR: Wire.begin() FAILED!");
      Serial.println("This is ESP_ERR_INVALID_STATE - pins may not support I2C");
      while(1) { delay(1000); }
    }
  }

  Serial.println("I2C initialized successfully!");
  Serial.print("SDA: GPIO");
  Serial.println(I2C_SDA);
  Serial.print("SCL: GPIO");
  Serial.println(I2C_SCL);

  expanders_voltage_gpio_init();

  // Initialize expander 2
  Serial.println("Initializing Expander 2 (0x21)...");
  if (expander2.begin()) {
    Serial.println("Expander 2 initialized successfully");
  } else {
    Serial.println("ERROR: Failed to initialize Expander 2");
    while(1) { delay(1000); }
  }

  // Initialize expander 1
  Serial.println("Initializing Expander 1 (0x20)...");
  if (expander1.begin()) {
    Serial.println("Expander 1 initialized successfully");
  } else {
    Serial.println("ERROR: Failed to initialize Expander 1");
    while(1) { delay(1000); }
  }

  expander1.pinMode1(EXP1_PMIC_EN, OUTPUT);

  Serial.println("Setting voltage enable pins HIGH...");

  expander1.write1(EXP1_PMIC_EN, HIGH);
  expander2.write1(EXP2_EN_DC12, HIGH);
  expander2.write1(EXP2_EN_DC19, HIGH);
  expander2.write1(EXP2_EN_V5D, HIGH);
  expander2.write1(EXP2_EN_1_35V, HIGH);
  expander2.write1(EXP2_EN_NEGATIVE_3_5V, HIGH);
  expander1.write1(EXP1_EN_V5A, HIGH);

  delay(100);

  scanI2C();

  Serial.println("Enabling PMIC...");
  enablePmic();

  delay(1000);

  Serial.println("Enabling power switches...");
  powerSwitchEnable();

  Serial.println("Green board setup complete!");

#elif defined(BLACK_BOARD)
  Serial.println("\n\n=== ESP32-S3 BLACK BOARD Starting ===");
  Serial.println("Board: Black Board with Direct GPIO");
  Serial.println();

  // Initialize GPIO pins
  gpio_init();

  // Initialize main I2C bus (for other peripherals if any)
  Serial.println("Initializing main I2C bus...");
  if (!Wire.begin()) {
    Serial.println("ERROR: Main I2C initialization failed!");
    while(1) { delay(1000); }
  }
  Serial.println("Main I2C initialized successfully!");

  // Initialize PMIC I2C bus
  Serial.println("Initializing PMIC I2C bus...");
  Serial.print("PMIC SDA: GPIO");
  Serial.println(PMIC_SDA);
  Serial.print("PMIC SCL: GPIO");
  Serial.println(PMIC_SCL);

  if (!WirePmic.begin(PMIC_SDA, PMIC_SCL, 100000)) {
    Serial.println("ERROR: PMIC I2C initialization failed!");
    while(1) { delay(1000); }
  }
  Serial.println("PMIC I2C initialized successfully!");

  delay(100);

  // Scan both I2C buses
  scanI2C(Wire, "Main");
  scanI2C(WirePmic, "PMIC");

  // Enable PMIC
  Serial.println("Setting PMIC_EN HIGH...");
  digitalWrite(PMIC_EN, HIGH);
  delay(100);

  Serial.println("Enabling PMIC...");
  enablePmic();

  delay(1000);

  Serial.println("Enabling power switches...");
  powerSwitchEnable();

  Serial.println("Black board setup complete!");

#else
  #error "No board type defined! Please define GREEN_BOARD or BLACK_BOARD"
#endif
}

void loop() {
  // Main loop
  delay(1000);
}
