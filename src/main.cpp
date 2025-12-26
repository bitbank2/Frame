#include <Arduino.h>
#include <Wire.h>
#include "pindefine.h"

extern "C" {
#include "pmic.h"
#include "comm.h"
#include "status.h"
}


void scanI2C() {
  byte error, address;
  int deviceCount = 0;

  printf("\n========== I2C Bus Scanner ==========\n");
  printf("Scanning I2C bus (addresses 0x01 to 0x7F)...\n\n");

  for(address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      printf("Device found at address 0x%02X (%d)", address, address);

      if (address >= 0x20 && address <= 0x27) {
        printf(" - Likely TCA9555/TCA9535");
        if (address == 0x20) printf(" (Expander 1)");
        if (address == 0x21) printf(" (Expander 2)");
      } else if (address == 0x48) {
        printf(" - PMIC");
      } else {
        printf(" - Unknown device");
      }
      printf("\n");
      deviceCount++;
    }
    else if (error == 4) {
      printf("Unknown error at address 0x%02X\n", address);
    }
  }

  printf("\n====================================\n");
  if (deviceCount == 0) {
    printf("No I2C devices found!\n");
    printf("Check connections and pull-up resistors.\n");
  } else {
    printf("Total devices found: %d\n", deviceCount);
  }
  printf("====================================\n\n");
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

// I2C Scanner function - unified for both boards


#endif // GREEN_BOARD

#ifdef BLACK_BOARD
// Black board - ESP32-S3 with direct GPIO control

void gpio_init() {
  // Initialize GPIO pins as outputs
  pinMode(PMIC_EN, OUTPUT);
  pinMode(VDDP_SW, OUTPUT);
  pinMode(VDDN_SW, OUTPUT);
  pinMode(VNCP_SW, OUTPUT);
  pinMode(PMIC_PS, OUTPUT);

  pinMode(45,OUTPUT);
  pinMode(21, OUTPUT);


  // Initialize input pins
  pinMode(EPD_BUSY, INPUT);
  pinMode(PMIC_PG, INPUT);

  // Set initial states
  digitalWrite(21,HIGH);
  digitalWrite(45,HIGH);
  digitalWrite(PMIC_EN, HIGH);
  digitalWrite(VDDP_SW, HIGH);
  digitalWrite(VDDN_SW, HIGH);
  digitalWrite(VNCP_SW, HIGH);
  digitalWrite(PMIC_PS, LOW);

  printf("GPIO pins initialized\n");
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
  printf("\n\n=== ESP32-S3 BLACK BOARD Starting ===\n");
  printf("Board: Black Board with Direct GPIO\n\n");

  // Initialize GPIO pins
  gpio_init();

  // Initialize I2C bus

  if (!Wire.begin(PMIC_SDA, PMIC_SCL,100000)) {
    printf("ERROR: I2C initialization failed!\n");
    while(1) { delay(1000); }
  }
  printf("I2C initialized successfully!\n");

  delay(100);

  // Scan I2C bus
  scanI2C();

  enablePmic();

  printf("\n========== Reading PMIC Registers ==========\n");

  unsigned char reg_data[8] = {0};
  unsigned char empty_data[9];

  // Prepare data for 0x1D: register address + 8 empty bytes
  empty_data[0] = 0x1D;
  for(int i = 1; i < 9; i++) {
    empty_data[i] = 0x00;
  }

  // Send empty 8 bytes to register 0x1D to trigger measurement
  printf("Sending 8 empty bytes to PMIC register 0x1D...\n");
  if(sendPmicData(empty_data, 9) == DONE) {
    printf("Successfully sent command to 0x1D\n");
  }

  // Wait for PMIC to prepare data
  delay(100);

  // Read 8 bytes from register 0x1D
  printf("Reading 8 bytes from PMIC register 0x1D...\n");
  if(readPmicRegister(0x1D, reg_data, 8) == DONE) {
    printf("Data from register 0x1D (8 bytes):\n");
    for(int i = 0; i < 8; i++) {
      printf("  Byte %d: 0x%02X (%d)\n", i, reg_data[i], reg_data[i]);
    }
  }

  printf("\n");

  // Prepare data for 0x1E: register address + 8 empty bytes
  empty_data[0] = 0x1E;
  for(int i = 1; i < 9; i++) {
    empty_data[i] = 0x00;
  }

  // Send empty 8 bytes to register 0x1E to trigger measurement
  printf("Sending 8 empty bytes to PMIC register 0x1E...\n");
  if(sendPmicData(empty_data, 9) == DONE) {
    printf("Successfully sent command to 0x1E\n");
  }

  // Wait for PMIC to prepare data
  delay(100);

  // Read 8 bytes from register 0x1E
  printf("Reading 8 bytes from PMIC register 0x1E...\n");
  if(readPmicRegister(0x1E, reg_data, 8) == DONE) {
    printf("Data from register 0x1E (8 bytes):\n");
    for(int i = 0; i < 8; i++) {
      printf("  Byte %d: 0x%02X (%d)\n", i, reg_data[i], reg_data[i]);
    }
  }

  printf("===========================================\n\n");

  printf("Black board setup complete!\n");

  delay(1000);


  printf("Black board setup complete!\n");

#else
  #error "No board type defined! Please define GREEN_BOARD or BLACK_BOARD"
#endif
}

void loop() {
  // Main loop
  delay(1000);
}
