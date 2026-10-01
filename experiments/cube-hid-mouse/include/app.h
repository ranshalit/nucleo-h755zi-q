#ifndef APP_H
#define APP_H

#include "stm32h7xx_hal.h"

extern UART_HandleTypeDef huart3;
extern HCD_HandleTypeDef hhcd_usb_fs;

void Error_Handler(void);

#endif
