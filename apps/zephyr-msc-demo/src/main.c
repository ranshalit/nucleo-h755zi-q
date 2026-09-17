/*
 * Copyright (c) 2026 Ran Shalit
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB Mass Storage demo for NUCLEO-H755ZI-Q, m7 core.
 *
 * Exposes a small in-RAM FAT-formatted disk as a USB flash drive over
 * the modern USBD stack (CONFIG_USB_DEVICE_STACK_NEXT + CONFIG_USBD_MSC_CLASS).
 * Bulk-only transport, no isochronous endpoints involved, so this avoids
 * the still-unresolved isoc/periodic-transfer stall documented for both
 * USB host mode and the UAC2 device-mode demo (see docs/usb_debug.md,
 * Steps 1-39 and Step 44).
 */

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/fs/fs.h>
#include <ff.h>
#include <stdio.h>

LOG_MODULE_REGISTER(msc_demo);

USBD_DEFINE_MSC_LUN(ram, "RAM", "Zephyr", "RAMDisk", "0.00");

static struct usbd_context *msc_usbd;
static struct fs_mount_t fs_mnt;
static FATFS fat_fs;

static int enable_usb_device_next(void)
{
	int err;

	msc_usbd = sample_usbd_init_device(NULL);
	if (msc_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	err = usbd_enable(msc_usbd);
	if (err) {
		LOG_ERR("Failed to enable device support");
		return err;
	}

	LOG_INF("USB device support enabled");

	return 0;
}

static void write_readme(void)
{
	struct fs_file_t file;
	static const char msg[] =
		"NUCLEO-H755ZI-Q USB Mass Storage demo\r\n"
		"Bulk-only USB MSC over Zephyr's modern USBD stack.\r\n"
		"Try editing/adding files -- they persist in board RAM\r\n"
		"until the next reset.\r\n";
	int rc;

	fs_file_t_init(&file);
	rc = fs_open(&file, "/RAM:/README.TXT", FS_O_CREATE | FS_O_WRITE);
	if (rc < 0) {
		LOG_ERR("Failed to create README.TXT: %d", rc);
		return;
	}

	fs_write(&file, msg, sizeof(msg) - 1);
	fs_close(&file);
}

static void setup_disk(void)
{
	struct fs_statvfs sbuf;
	int rc;

	fs_mnt.type = FS_FATFS;
	fs_mnt.fs_data = &fat_fs;
	fs_mnt.mnt_point = "/RAM:";

	rc = fs_mount(&fs_mnt);
	if (rc < 0) {
		LOG_ERR("Failed to mount RAM disk (%d), formatting", rc);
		rc = fs_mkfs(FS_FATFS, (uintptr_t)"RAM:", NULL, 0);
		if (rc < 0) {
			LOG_ERR("Failed to format RAM disk: %d", rc);
			return;
		}
		rc = fs_mount(&fs_mnt);
		if (rc < 0) {
			LOG_ERR("Failed to mount freshly-formatted disk: %d", rc);
			return;
		}
	}

	write_readme();

	/* Allow log messages to flush before printk output. */
	k_sleep(K_MSEC(50));

	rc = fs_statvfs(fs_mnt.mnt_point, &sbuf);
	if (rc == 0) {
		printk("%s: bsize=%lu frsize=%lu blocks=%lu bfree=%lu\n",
		       fs_mnt.mnt_point,
		       sbuf.f_bsize, sbuf.f_frsize,
		       sbuf.f_blocks, sbuf.f_bfree);
	}
}

int main(void)
{
	int ret;

	setup_disk();

	ret = enable_usb_device_next();
	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		return 0;
	}

	LOG_INF("Device is now a USB mass storage drive. Plug into a host.");
	return 0;
}
