/*
 * Copyright (C) 2016  Nexell Co., Ltd.
 * Author: DeokJin, Lee <truevirtue@nexell.co.kr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <sysheader.h>
#if defined(PMIC_ON)
#include <i2c_gpio.h>
#include <nxe2000.h>
#include <mp8845.h>
#include <axp228.h>

/**********************************************
 * PMIC I2C Info
************************************************/
#define PMIC_I2C_GPIO_GRP 3 // GPIOD
#define PMIC_I2C_SCL 4 // SCL : GPIOD04
#define PMIC_I2C_SDA 5 // SDA : GPIOD05
#define PMIC_I2C_SCL_ALT 0 // SCL : ALT0
#define PMIC_I2C_SDA_ALT 0 // SDA: ALT0

/**********************************************
 * Q100 condition
 * ARM      : 1.125V
 * CORE     : 1.0V
 * SYS      : 3.3V
 * DDR_IO   : 1.5V
 * DDR      : 1.5V

 * Consumer condition
 * ARM      : 1.25V
 * CORE     : 1.1V
 * SYS      : 3.3V
 * DDR_IO   : 1.5V
 * DDR      : 1.5V
************************************************/
#define PMIC_DEF_ARM_VOL 1125000
#define PMIC_DEF_CORE_VOL 1000000
#define PMIC_DEF_3P3_VOL 3300000
#define PMIC_DEF_DDRIO_VOL 1500000
#define PMIC_DEF_DDR_VOL 1500000

void pmic_board_init(void)
{
	char data[4];

	I2C_INIT(PMIC_I2C_GPIO_GRP, PMIC_I2C_SCL, PMIC_I2C_SDA,
		PMIC_I2C_SCL_ALT, PMIC_I2C_SDA_ALT);

#ifdef AXP228_PMIC_ENABLE
	/* REG 80H : DC-DC Work mode
	 *       bit 0 : DCDC1
	 *           1 : DCDC2
	 *           2 : DCDC3
	 *           3 : DCDC4
	 *           4 : DCDC5
	 *   value 0 : PFM/PWM auto switch
	 *           1 : fixed PWM */
	axp228_read(AXP228_REG_DCDC_WORK_MODE, data, 1);
	data[0] = (data[0] & 0x1F) | DCDC_SYS | DCDC_DDR;
	axp228_write(AXP228_REG_DCDC_WORK_MODE, data, 1);

	/* Set voltage of DCDC2(VCC_ARM) */
	data[0] = axp228_get_dcdc_step(
		PMIC_DEF_ARM_VOL, AXP228_DEF_DDC234_VOL_STEP,
		AXP228_DEF_DDC234_VOL_MIN, AXP228_DEF_DDC24_VOL_MAX);
	axp228_write(AXP228_REG_DC2VOL, data, 1);

	/* Set voltage of DCDC3(VCC_CORE) */
	data[0] = axp228_get_dcdc_step(
		PMIC_DEF_CORE_VOL, AXP228_DEF_DDC234_VOL_STEP,
		AXP228_DEF_DDC234_VOL_MIN, AXP228_DEF_DDC3_VOL_MAX);
	axp228_write(AXP228_REG_DC3VOL, data, 1);

	/*
	 * VCC3P3_CORE(OTP:3.3V), VCC1P5_DDRIO(OTP:1.5V), VCC1P5_DDR(OTP:1.5V)
	 * It is not set as it is the same as the OTP setting.
	 * If it is different from OTP, please set it.
	 */
#if 0
	/* Set voltage of DCDC1(VCC3P3_CORE) */
	data[0] = axp228_get_dcdc_step(
		PMIC_DEF_3P3_VOL, AXP228_DEF_DDC1_VOL_STEP,
		AXP228_DEF_DDC1_VOL_MIN, AXP228_DEF_DDC1_VOL_MAX);
	axp228_write(AXP228_REG_DC1VOL, data, 1);

	/* Set voltage of DCDC4(VCC1P5_DDRIO) */
	data[0] = axp228_get_dcdc_step(
		PMIC_DEF_DDRIO_VOL, AXP228_DEF_DDC234_VOL_STEP,
		AXP228_DEF_DDC234_VOL_MIN, AXP228_DEF_DDC24_VOL_MAX);
	axp228_write(AXP228_REG_DC4VOL, data, 1);

	/* Set voltage of DCDC5(VCC1P5_DDR) */
	data[0] = axp228_get_dcdc_step(
		PMIC_DEF_DDR_VOL, AXP228_DEF_DDC5_VOL_STEP,
		AXP228_DEF_DDC5_VOL_MIN, AXP228_DEF_DDC5_VOL_MAX);
	axp228_write(AXP228_REG_DC5VOL, data, 1);

#endif

#else /* NXE2000_PMIC_ENABLE */
	data[0] = nxe2000_get_dcdc_step(PMIC_DEF_ARM_VOL);
	nxe2000_write(NXE2000_REG_DC1VOL, data, 1);

	data[0] = nxe2000_get_dcdc_step(PMIC_DEF_CORE_VOL);
	nxe2000_write(NXE2000_REG_DC2VOL, data, 1);

	data[0] = nxe2000_get_dcdc_step(PMIC_DEF_DDRIO_VOL);
	nxe2000_write(NXE2000_REG_DC4VOL, data, 1);

	data[0] = nxe2000_get_dcdc_step(PMIC_DEF_DDR_VOL);
	nxe2000_write(NXE2000_REG_DC5VOL, data, 1);
#endif

	I2C_DEINIT();
}

#endif /* PMIC_ON */
