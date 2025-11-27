#pragma once

#include "TCA9555.h"

// Global TCA9555 expander objects
// These are instantiated in main.cpp and available to all modules
extern TCA9555 expander1;  // First expander at address 0x20
extern TCA9555 expander2;  // Second expander at address 0x21

// Global interrupt flags
extern volatile bool expander1_interrupt;
extern volatile bool expander2_interrupt;
