#ifndef USBH_CONF_H
#define USBH_CONF_H

#include <stm32h7xx_hal.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>

#define USBH_MAX_NUM_ENDPOINTS 2U
#define USBH_MAX_NUM_INTERFACES 2U
#define USBH_MAX_NUM_CONFIGURATION 1U
#define USBH_KEEP_CFG_DESCRIPTOR 1U
#define USBH_MAX_NUM_SUPPORTED_CLASS 1U
#define USBH_MAX_SIZE_CONFIGURATION 0x200U
#define USBH_MAX_DATA_BUFFER 0x200U
#define USBH_DEBUG_LEVEL 2U
#define USBH_USE_OS 0U
#define USBH_IN_NAK_PROCESS 0U

#define USBH_malloc k_malloc
#define USBH_free k_free
#define USBH_memset memset
#define USBH_memcpy memcpy

#define USBH_UsrLog(...) do { printf(__VA_ARGS__); printf("\r\n"); } while (0)
#define USBH_ErrLog(...) do { printf("ERROR: "); printf(__VA_ARGS__); printf("\r\n"); } while (0)
#define USBH_DbgLog(...) do { } while (0)

#endif
