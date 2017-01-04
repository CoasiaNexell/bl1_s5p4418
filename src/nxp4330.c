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

 #ifdef CHIPID_NXP4330
extern unsigned int __init sdmmc_self_boot(void);

/*
 * NXP4330 a part to overcome the limitations
 * on the size used in the SRAM Romboot(rev2).
 */
int __init nxp4330_self_boot(void)
{
	int boot_option  = pSBI->DBI.SDMMCBI.LoadDevice;
	unsigned int fix_bl1_size = (16 * 1024);
	int ret = 0;

	/* Make sure than the size loaded in Romboot, built size is large. */
	if (pSBI->LOADSIZE > fix_bl1_size) {
		/* Check to boot type. */
		switch (boot_option) {
			case BOOT_FROM_SDMMC:
				ret = sdmmc_self_boot();
				break;
		}
	}

	return ret;
}
#endif
