#ifndef USBH_CONF_H
#define USBH_CONF_H

#include "stm32h7xx_hal.h"

#include <stddef.h>
#include <stdio.h>

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

void *app_malloc(size_t size);
void app_free(void *ptr);
void *app_memset(void *dest, int value, size_t size);
void *app_memcpy(void *dest, const void *src, size_t size);

#define USBH_malloc app_malloc
#define USBH_free app_free
#define USBH_memset app_memset
#define USBH_memcpy app_memcpy

#if (USBH_DEBUG_LEVEL > 0U)
#define USBH_UsrLog(...) do { printf(__VA_ARGS__); printf("\r\n"); } while (0)
#else
#define USBH_UsrLog(...) do { } while (0)
#endif

#if (USBH_DEBUG_LEVEL > 1U)
#define USBH_ErrLog(...) do { printf("ERROR: "); printf(__VA_ARGS__); printf("\r\n"); } while (0)
#else
#define USBH_ErrLog(...) do { } while (0)
#endif

#if (USBH_DEBUG_LEVEL > 2U)
#define USBH_DbgLog(...) do { printf("DEBUG: "); printf(__VA_ARGS__); printf("\r\n"); } while (0)
#else
#define USBH_DbgLog(...) do { } while (0)
#endif

#endif
