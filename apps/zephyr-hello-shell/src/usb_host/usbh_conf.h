/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USBH_CONF_H_
#define USBH_CONF_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stm32h7xx_hal.h>

#define USBH_MAX_NUM_ENDPOINTS       8U
#define USBH_MAX_NUM_INTERFACES      8U
#define USBH_MAX_NUM_CONFIGURATION   1U
#define USBH_KEEP_CFG_DESCRIPTOR     1U
#define USBH_MAX_NUM_SUPPORTED_CLASS 2U
#define USBH_MAX_SIZE_CONFIGURATION  0x400U
#define USBH_MAX_DATA_BUFFER         0x400U
#define USBH_DEBUG_LEVEL             2U
#define USBH_USE_OS                  0U
#define USBH_IN_NAK_PROCESS          0U

#define USBH_malloc malloc
#define USBH_free free
#define USBH_memset memset
#define USBH_memcpy memcpy

#if USBH_DEBUG_LEVEL > 0U
#define USBH_UsrLog(...) \
	do { \
		printf("ST USBH: "); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} while (0)
#else
#define USBH_UsrLog(...) do { } while (0)
#endif

#if USBH_DEBUG_LEVEL > 1U
#define USBH_ErrLog(...) \
	do { \
		printf("ST USBH ERROR: "); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} while (0)
#else
#define USBH_ErrLog(...) do { } while (0)
#endif

#if USBH_DEBUG_LEVEL > 2U
#define USBH_DbgLog(...) \
	do { \
		printf("ST USBH DEBUG: "); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} while (0)
#else
#define USBH_DbgLog(...) do { } while (0)
#endif

#endif /* USBH_CONF_H_ */
