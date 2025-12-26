//=================================================================================================
//                   Pin Define for 31.5" Control Board 
//
// File Name : pindefine.h
// Author : Electronic Design Dept. II
// Data : 2023.12.15
// Version : 1.1
// Copyright : E Ink Holdings Inc.
//=================================================================================================

#ifndef __PINDEFINE_H__
#define __PINDEFINE_H__

//==============  Standard SPI Setting   ==============//
//Please modify the pin number

#ifdef GREEN_BOARD
#define SPI_CS0		1
#define SPI_CS1		2
#define SPI_CS2		3
#define SPI_CS3		4
#define SPI_CS4		15
#define SPI_CS5		16
#define SPI_CS6		17
#define SPI_CS7		18
#define SPI_CLK		5
#define SPI_Data0	9
#define SPI_Data1	10
#define SPI_Data2	23
#define SPI_Data3	24

//==============   GPIO Setting   ==============//
//Please modify the pin number
#define EPD_BUSY	4
#define EPD_RST		5
#define PMIC_PG		16
#define PMIC_PS		17 //pin for entering power save mode (IC shutdown)
//===============================================


//==============   Expander 1 GPIO Setting   ==============//
//Please modify the pin number
#define EXP1_SPI1_CS3	0
#define EXP1_SPI1_CS2	1
#define EXP1_SPI1_CS1	2
#define EXP1_SPI1_CS0	3
#define EXP1_SPI2_CS3	4
#define EXP1_SPI2_CS2	5
#define EXP1_SPI2_CS1	6
#define EXP1_SPI2_CS0	7
#define EXP1_PMIC_EN	15
#define EXP1_CS_SD_CARD	10
#define EXP1_EN_V5A 11
#define EXP1_VDDN_EN 12
#define EXP1_VDDP_EN 13
#define EXP1_VNCP_EN 14
//===============================================

//==============   Expander 2 GPIO Setting   ==============//
//Please modify the pin number
#define EXP2_BTN3	0
#define EXP2_BTN4	1
#define EXP2_BTN5	2
#define EXP2_EN_V5D	3
#define EXP2_EN_DC19	4
#define EXP2_EN_DC12	5
#define EXP2_EN_1_35V	6
#define EXP2_EN_NEGATIVE_3_5V	7
#define EXP2_GAUGE_INT	17

#endif // GREEN_BOARD

#ifdef BLACK_BOARD
//===============================================
#define SPI_CS0		15
#define SPI_CS1		16
#define SPI_CS2		17
#define SPI_CS3		18
#define SPI_CS4		8
#define SPI_CS5		3
#define SPI_CS6		9
#define SPI_CS7		10
#define SPI_CLK		12
#define SPI_Data0	45
#define SPI_Data1	48
#define SPI_Data2	13
#define SPI_Data3	14

//==============   GPIO Setting   ==============//
//Please modify the pin number
#define EPD_BUSY	6
#define EPD_RST		7
#define PMIC_PG		41
#define PMIC_PS		40 //pin for entering power save mode (IC shutdown)
#define PMIC_EN		21
#define PMIC_SCL    38 // 38 PMIC I2C SCL 11 Expander SCL
#define PMIC_SDA    39 //39 PMIC I2C SDA 10 Expander SDA
#define VDDP_SW		1
#define VDDN_SW		2
#define VNCP_SW		42
//===============================================





#endif

#define GPIO_LOW	0
#define GPIO_HIGH	1

#endif //#ifndef __PINDEFINE_H__
