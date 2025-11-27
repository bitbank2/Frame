//=================================================================================================
//                   PMIC for 31.5" Control Board
//
// File Name : pmic.h
// Author : Electronic Design Dept. II
// Data : 2023.12.15
// Version : 1.1
// Copyright : E Ink Holdings Inc.
//=================================================================================================


#ifndef __PMIC_H__
#define __PMIC_H__

#define PMIC_ADDR	0x48

#define SET_VPOS_VENG2 2
#define SET_VPOS_VENG3 3
#define SET_VCOMDC 4

#endif //#ifndef __PMIC_H__

#ifdef __PMIC_C__
#define __PMIC_EXTERN__
#else
#define __PMIC_EXTERN__ extern
#endif

__PMIC_EXTERN__ unsigned char initPmicData[28];

__PMIC_EXTERN__ unsigned char setPmic(void);
__PMIC_EXTERN__ unsigned char sendPmicData(unsigned char *dataBuffer, unsigned int dataLength);
__PMIC_EXTERN__ void enablePmic(void);
__PMIC_EXTERN__ void disablePmic(void);
__PMIC_EXTERN__ void powerSwitchEnable(void);
__PMIC_EXTERN__ void powerSwitchDisable(void);
__PMIC_EXTERN__ unsigned int voltageToRegisterData(unsigned int voltageData, unsigned char setSelect);
