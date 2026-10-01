#include "app.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static union
{
  max_align_t align;
  uint8_t bytes[1024];
} usb_heap;

static uint8_t usb_heap_used;

void *app_malloc(size_t size)
{
  if ((usb_heap_used != 0U) || (size > sizeof(usb_heap.bytes)))
  {
    return NULL;
  }
  usb_heap_used = 1U;
  return usb_heap.bytes;
}

void app_free(void *ptr)
{
  if (ptr == usb_heap.bytes)
  {
    usb_heap_used = 0U;
  }
}

void *app_memset(void *dest, int value, size_t size)
{
  uint8_t *out = dest;

  while (size-- != 0U)
  {
    *out++ = (uint8_t)value;
  }
  return dest;
}

void *app_memcpy(void *dest, const void *src, size_t size)
{
  uint8_t *out = dest;
  const uint8_t *in = src;

  while (size-- != 0U)
  {
    *out++ = *in++;
  }
  return dest;
}

void *memset(void *dest, int value, size_t size)
{
  return app_memset(dest, value, size);
}

void *memcpy(void *dest, const void *src, size_t size)
{
  return app_memcpy(dest, src, size);
}

static void uart_write(const char *data, size_t length)
{
  if (length != 0U)
  {
    (void)HAL_UART_Transmit(&huart3, (uint8_t *)data, (uint16_t)length,
                            HAL_MAX_DELAY);
  }
}

static int print_unsigned(uint32_t value, uint32_t base, unsigned int width,
                          char pad, int uppercase)
{
  char buffer[16];
  unsigned int length = 0U;
  int written = 0;

  do
  {
    uint32_t digit = value % base;
    buffer[length++] = (char)((digit < 10U) ? ('0' + digit) :
                             ((uppercase ? 'A' : 'a') + digit - 10U));
    value /= base;
  } while (value != 0U);

  while (length < width)
  {
    uart_write(&pad, 1U);
    written++;
    width--;
  }
  while (length != 0U)
  {
    length--;
    uart_write(&buffer[length], 1U);
    written++;
  }
  return written;
}

int printf(const char *format, ...)
{
  va_list args;
  int written = 0;

  va_start(args, format);
  while (*format != '\0')
  {
    if (*format != '%')
    {
      uart_write(format++, 1U);
      written++;
      continue;
    }

    format++;
    char pad = ' ';
    unsigned int width = 0U;
    int long_value = 0;

    if (*format == '0')
    {
      pad = '0';
      format++;
    }
    while ((*format >= '0') && (*format <= '9'))
    {
      width = (width * 10U) + (unsigned int)(*format - '0');
      format++;
    }
    if (*format == 'l')
    {
      long_value = 1;
      format++;
    }

    switch (*format)
    {
      case '%':
        uart_write("%", 1U);
        written++;
        break;
      case 'c':
      {
        char value = (char)va_arg(args, int);
        uart_write(&value, 1U);
        written++;
        break;
      }
      case 's':
      {
        const char *value = va_arg(args, const char *);
        const char *start = (value != NULL) ? value : "(null)";
        const char *end = start;
        while (*end != '\0')
        {
          end++;
        }
        uart_write(start, (size_t)(end - start));
        written += (int)(end - start);
        break;
      }
      case 'd':
      {
        int32_t value = long_value ? (int32_t)va_arg(args, long) :
                                    (int32_t)va_arg(args, int);
        if (value < 0)
        {
          uart_write("-", 1U);
          written++;
          value = -value;
        }
        written += print_unsigned((uint32_t)value, 10U, width, pad, 0);
        break;
      }
      case 'u':
      {
        uint32_t value = long_value ? (uint32_t)va_arg(args, unsigned long) :
                                     (uint32_t)va_arg(args, unsigned int);
        written += print_unsigned(value, 10U, width, pad, 0);
        break;
      }
      case 'x':
      case 'X':
      {
        uint32_t value = long_value ? (uint32_t)va_arg(args, unsigned long) :
                                     (uint32_t)va_arg(args, unsigned int);
        written += print_unsigned(value, 16U, width, pad, *format == 'X');
        break;
      }
      case 'p':
      {
        uintptr_t value = (uintptr_t)va_arg(args, void *);
        uart_write("0x", 2U);
        written += 2;
        written += print_unsigned((uint32_t)value, 16U, 8U, '0', 0);
        break;
      }
      default:
        uart_write("%", 1U);
        uart_write(format, 1U);
        written += 2;
        break;
    }
    if (*format != '\0')
    {
      format++;
    }
  }
  va_end(args);
  return written;
}

void __libc_init_array(void)
{
}
