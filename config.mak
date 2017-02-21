 # Copyright (C) 2016  Nexell Co., Ltd.
 # Author: Sangjong, Han <hans@nexell.co.kr>
 #
 # This program is free software; you can redistribute it and/or
 # modify it under the terms of the GNU General Public License
 #
 # as published by the Free Software Foundation; either version 2
 # of the License, or (at your option) any later version.
 #
 # This program is distributed in the hope that it will be useful,
 # but WITHOUT ANY WARRANTY; without even the implied warranty of
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 # GNU General Public License for more details.
 #
 # You should have received a copy of the GNU General Public License
 # along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Build Version info
VERINFO				= V100

#########################################################################
# build environment
##########################################################################
CFLAGS				:=
DEBUG				?= n

# chip name
#CHIPNAME			?= NXP4330
CHIPNAME			?= S5P4418

# (ddr3/lpddr3) memory type
MEMTYPE				?= DDR3
#MEMTYPE			?= LPDDR3
MEMTEST				?= n

# power management ic(pmic) on/off
INITPMIC			?= YES
#INITPMIC			?= NO

CRC_CHECK			?= n

# supported (thridboot) boot mode
SUPPORT_USB_BOOT		?= y
SUPPORT_SDMMC_BOOT		?= y

ifeq ($(CHIPNAME), NXP4330)
#BOARD				?= LEPUS
BOARD				?= NAVI
#BOARD				?= SMART_VOICE
else
#BOARD				?= SVT
#BOARD				?= ASB
#BOARD				?= DRONE
BOARD				?= AVN
#BOARD				?= LAVENDA
#BOARD				?= RAPTOR
endif

# supported kernel version (3.18-3.4/4.1-4.4)
KERNEL_VER			?= 3
#KERNEL_VER			?= 4

# system log Message
SYSLOG				?= y

# secure boot
SECURE_ON			?= 0

# arm mode - secure/non-secure
SECURE				?= y

# cross-tool pre-header
ifeq ($(OS),Windows_NT)
CROSS_TOOL_TOP			=
CROSS_TOOL			= $(CROSS_TOOL_TOP)arm-none-eabi-
else
CROSS_TOOL_TOP			=
CROSS_TOOL			= $(CROSS_TOOL_TOP)arm-eabi-
endif

# Top Names
PROJECT_NAME			= $(CHIPNAME)_2ndboot_$(MEMTYPE)_$(VERINFO)
TARGET_NAME			= bl1-$(shell echo $(BOARD) | tr A-Z a-z)
LDS_NAME			= pyrope_2ndboot

# Directories
DIR_PROJECT_TOP			=

DIR_OBJOUTPUT			= obj
DIR_TARGETOUTPUT		= out

CODE_MAIN_INCLUDE		=

# Build Environment
CPU				= cortex-a9
CC				= $(CROSS_TOOL)gcc
LD 				= $(CROSS_TOOL)ld
AS 				= $(CROSS_TOOL)as
AR 				= $(CROSS_TOOL)ar
MAKEBIN				= $(CROSS_TOOL)objcopy
OBJCOPY				= $(CROSS_TOOL)objcopy
RANLIB 				= $(CROSS_TOOL)ranlib

GCC_LIB				= $(shell $(CC) -print-libgcc-file-name)

ifeq ($(SECURE_ON), 1)
CFLAGS              +=  -DSECURE_ON
endif

ifeq ($(DEBUG), y)
CFLAGS				= -DNX_DEBUG -O0
Q				=
else
CFLAGS				= -DNX_RELEASE -Os
Q				= @
endif

# MISC tools for MS-DOS
ifeq ($(OS),Windows_NT)
MKDIR				= mkdir
RM				= del /q /F
MV				= move
CD				= cd
CP				= copy
ECHO				= echo
RMDIR				= rmdir /S /Q
else
MKDIR				= mkdir
RM				= rm -f
MV				= mv
CD				= cd
CP				= cp
ECHO				= echo
RMDIR				= rm -rf
endif

#########################################################################
# flags variables
#########################################################################
# FLAGS
ARFLAGS				= rcs
ARFLAGS_REMOVE			= -d
ARLIBFLAGS			= -v -s

ASFLAG				= -D__ASSEMBLY__

CFLAGS				+=	-g -Wall				\
					-Wextra -ffreestanding -fno-builtin	\
					-msoft-float				\
					-mlittle-endian				\
					-mcpu=$(CPU)				\
					-mstructure-size-boundary=32		\
					$(CODE_MAIN_INCLUDE)			\
					-D__arm -DLOAD_FROM_$(BOOTFROM)		\
					-DMEMTYPE_$(MEMTYPE)			\
					-DINITPMIC_$(INITPMIC)			\
					-DCHIPID_$(CHIPNAME)			\
					-D_2NDBOOT_MODE -D$(BOARD)

# arm mode - secure/non-secure
ifeq ($(SECURE), y)
CFLAGS				+=	-DSECURE_MODE
endif

# supported kernel version (3.18-4.3/4.1-4.4)
ifeq ($(KERNEL_VER), 3)
CFLAGS				+=	-DKERNEL_VER_3_4
endif

# system log messgae
ifeq ($(SYSLOG), y)
CFLAGS				+=	-DSYSLOG_ON
endif

# power managemnt ic(pmic) on/off
ifeq ($(INITPMIC), YES)
CFLAGS				+=	-D$(BOARD)_PMIC_INIT
endif

# memory test
ifeq ($(MEMTEST), y)
#MEMTEST_TYPE			+=	STANDARD
MEMTEST_TYPE			+=	SIMPLE
CFLAGS				+=	-D$(MEMTEST_TYPE)_MEMTEST
endif

# supported crc check
ifeq ($(CRC_CHECK), y)
CHECKSUM			+=	CRC_CHECK
CFLAGS				+=	-D$(CHECKSUM)_ON
endif
