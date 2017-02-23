/*
 * Copyright (C) 2016  Nexell Co., Ltd.
 * Author: Sangjong, Han <hans@nexell.co.kr>
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

static unsigned int reset_idx[2] =
{
	RESETINDEX_OF_TIMER_MODULE_PRESETn,
	RESETINDEX_OF_PWM_MODULE_PRESETn
};

struct s5p4418_resetgen_reg *const g_rstgen_reg =
	(struct s5p4418_resetgen_reg * const)PHY_BASEADDR_RSTCON_MODULE;

void reset_con(unsigned int device_num, int enable)
{
	if (enable) {
		mmio_clear_32(&g_rstgen_reg->regrst[(device_num >> 5) & 0x3],
			  (0x1 << (device_num & 0x1F)));			// reset
	} else {
		mmio_set_32(&g_rstgen_reg->regrst[(device_num >> 5) & 0x3],
			(0x1 << (device_num & 0x1F)));				// reset negate
	}
}

void common_resetgen(void)
{
	unsigned int i;
	for (i = 0; i < (sizeof(reset_idx)/sizeof(unsigned int)); i++) {
		reset_con(reset_idx[i], TRUE);					// reset on
		reset_con(reset_idx[i], FALSE);					// reset negate
	}
}
