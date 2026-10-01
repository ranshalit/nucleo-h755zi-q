ROOT := ../..
CUBE := $(ROOT)/vendor/reference/stm32cubeh7
HAL := $(CUBE)/Drivers/STM32H7xx_HAL_Driver
HAL_SRC := $(HAL)/Src
HAL_INC := $(HAL)/Inc
CMSIS := $(CUBE)/Drivers/CMSIS
DEVICE := $(CMSIS)/Device/ST/STM32H7xx
PROJECT := $(CUBE)/Projects/NUCLEO-H745ZI-Q/Examples/GPIO/GPIO_Demo

TOOLCHAIN ?= /home/ranshal/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin
CC := $(TOOLCHAIN)/arm-zephyr-eabi-gcc
OBJCOPY := $(TOOLCHAIN)/arm-zephyr-eabi-objcopy
SIZE := $(TOOLCHAIN)/arm-zephyr-eabi-size

BUILD := build
TARGET := $(BUILD)/cube-hid-mouse-cm7
LINKER := $(DEVICE)/Source/Templates/gcc/linker/stm32h755xx_flash_CM7.ld
STARTUP := $(DEVICE)/Source/Templates/gcc/startup_stm32h755xx.s
SYSTEM := $(PROJECT)/Common/Src/system_stm32h7xx.c

APP_SOURCES := \
	src/main.c \
	src/usbh_platform.c \
	src/stm32h7xx_it.c \
	src/runtime.c

HAL_SOURCES := \
	$(HAL_SRC)/stm32h7xx_hal.c \
	$(HAL_SRC)/stm32h7xx_hal_cortex.c \
	$(HAL_SRC)/stm32h7xx_hal_dma.c \
	$(HAL_SRC)/stm32h7xx_hal_dma_ex.c \
	$(HAL_SRC)/stm32h7xx_hal_flash.c \
	$(HAL_SRC)/stm32h7xx_hal_flash_ex.c \
	$(HAL_SRC)/stm32h7xx_hal_gpio.c \
	$(HAL_SRC)/stm32h7xx_hal_hcd.c \
	$(HAL_SRC)/stm32h7xx_hal_pwr.c \
	$(HAL_SRC)/stm32h7xx_hal_pwr_ex.c \
	$(HAL_SRC)/stm32h7xx_hal_rcc.c \
	$(HAL_SRC)/stm32h7xx_hal_rcc_ex.c \
	$(HAL_SRC)/stm32h7xx_hal_uart.c \
	$(HAL_SRC)/stm32h7xx_hal_uart_ex.c \
	$(HAL_SRC)/stm32h7xx_ll_usb.c

USBH_SOURCES := $(wildcard middleware/Core/Src/*.c) \
	$(wildcard middleware/Class/HID/Src/*.c)

SOURCES := $(APP_SOURCES) $(HAL_SOURCES) $(USBH_SOURCES) $(SYSTEM) $(STARTUP)

INCLUDES := \
	-Iinclude \
	-Imiddleware/Core/Inc \
	-Imiddleware/Class/HID/Inc \
	-I$(HAL_INC) \
	-I$(HAL_INC)/Legacy \
	-I$(CMSIS)/Include \
	-I$(DEVICE)/Include

COMMON_FLAGS := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard \
	-DSTM32H755xx -DCORE_CM7 -DUSE_HAL_DRIVER -DHSE_VALUE=8000000U \
	-ffunction-sections -fdata-sections -fno-common -fno-builtin \
	-Wall -Wextra -Werror -Wno-unused-parameter -Og -g3

CFLAGS := $(COMMON_FLAGS) -std=gnu11 $(INCLUDES)
LDFLAGS := $(COMMON_FLAGS) -nostdlib -T$(LINKER) -Wl,--gc-sections \
	-Wl,-Map=$(TARGET).map -Wl,--cref -lgcc

.PHONY: all clean

all: $(TARGET).elf $(TARGET).bin

$(TARGET).elf: $(SOURCES) Makefile | $(BUILD)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
