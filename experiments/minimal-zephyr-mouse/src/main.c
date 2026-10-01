/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <zephyr/kernel.h>

extern int board_usb_host_start(void);

int main(void)
{
	printf("Minimal Zephyr USB mouse host\n");
	int ret = board_usb_host_start();

	if (ret != 0) {
		printf("USB host startup failed: %d\n", ret);
	}
	return ret;
}
