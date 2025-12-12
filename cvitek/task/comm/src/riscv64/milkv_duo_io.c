#include "milkv_duo_io.h"

void duo_led_control(int enable)
{
	*(uint32_t*)(PWR_GPIO_REG_BASE | GPIO_SWPORTA_DDR) = 1 << GPIO_LED;

	if (enable) {
		*(uint32_t*)(PWR_GPIO_REG_BASE | GPIO_SWPORTA_DR) = 1 << GPIO_LED;
	} else {
		*(uint32_t*)(PWR_GPIO_REG_BASE | GPIO_SWPORTA_DR) = 0;
	}
}
