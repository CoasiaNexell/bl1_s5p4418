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

#include <nx_sdmmc.h>
#include "iSDHCBOOT.h"
#include <nx_bootheader.h>

// Modified For mmc clkgen and sd clokc
#if defined(MMC_AS_100)
#define SDXC_CLKGENSRC			(1)		// PLL(0, 1, 2) = 800 MHz
#define SDXC_CLKGENDIV			(4)		// PLL1 / 2 / 2 = 200 MHz
#define SDXC_CLKDIV			(1)		// 2 : 50MHz, 1 : 100MHz
#define SDXC_CLKDIV_400KHZ		(250)		// PLL0 / 250 / 2 = 1600KHz
#define CONFIG_S5P_SDMMC_CLOCK		50000000
#define CONFIG_S5P_SDMMC_CMD_CLOCK	400000
#else /* MMC_AS_50 */
#define SDXC_CLKGENSRC			(1)		// PLL(0, 1, 2) = 800 MHz
#define SDXC_CLKGENDIV			(4)		// PLL1 / 2 / 2 = 200 MHz
#define SDXC_CLKDIV			(2)		// 2 : 50MHz, 1 : 100MHz
#define SDXC_CLKDIV_400KHZ		(250)		// PLL0 / 250 / 2 = 1600KHz
#define CONFIG_S5P_SDMMC_CLOCK		50000000
#define CONFIG_S5P_SDMMC_CMD_CLOCK	400000
#endif

#ifdef NX_DEBUG
#define dprintf         printf
#else
#define dprintf(x, ...) {}
#endif

static struct s5p4418_gpio_reg (*const g_gpio_reg)[1] =
    (struct s5p4418_gpio_reg(*)[])(PHY_BASEADDR_GPIOA_MODULE);
extern U32 getquotient(U32 dividend, U32 divisor);

//------------------------------------------------------------------------------
static struct NX_CLKGEN_RegisterSet *const __initdata pgSDClkGenReg[3] = {
	(struct NX_CLKGEN_RegisterSet *)PHY_BASEADDR_CLKGEN18_MODULE,
	(struct NX_CLKGEN_RegisterSet *)PHY_BASEADDR_CLKGEN19_MODULE,
	(struct NX_CLKGEN_RegisterSet *)PHY_BASEADDR_CLKGEN20_MODULE
};
static U32 const __initdata SDResetNum[3] = {
	RESETINDEX_OF_SDMMC0_MODULE_i_nRST,
	RESETINDEX_OF_SDMMC1_MODULE_i_nRST,
	RESETINDEX_OF_SDMMC2_MODULE_i_nRST
};
struct NX_SDMMC_RegisterSet *const __initdata pgSDXCReg[3] = {
	(struct NX_SDMMC_RegisterSet *)PHY_BASEADDR_SDMMC0_MODULE,
	(struct NX_SDMMC_RegisterSet *)PHY_BASEADDR_SDMMC1_MODULE,
	(struct NX_SDMMC_RegisterSet *)PHY_BASEADDR_SDMMC2_MODULE
};

//------------------------------------------------------------------------------
#if 1
typedef struct {
	U32 nPllNum;
	U32 nFreqHz;
	U32 nClkDiv;
	U32 nClkGenDiv;
} NX_CLKINFO_SDMMC;

static NX_CLKINFO_SDMMC clkInfo;
static CBOOL fixedPLL;

CBOOL __init NX_SDMMC_GetClkParam( NX_CLKINFO_SDMMC *pClkInfo )
{
	U32 srcFreq;
	U32 nRetry = 1, nTemp = 0;
	CBOOL   fRet = CFALSE;

	srcFreq = clkpwr_get_pllfreq(pClkInfo->nPllNum);

retry_getparam:
	for (pClkInfo->nClkDiv = 2; ; pClkInfo->nClkDiv += 2) {
		nTemp   = (pClkInfo->nFreqHz * pClkInfo->nClkDiv);
		pClkInfo->nClkGenDiv  = getquotient(srcFreq, nTemp);      // (srcFreq / nTemp)

		if (srcFreq > (pClkInfo->nFreqHz * pClkInfo->nClkDiv))
			pClkInfo->nClkGenDiv += 2;

		if (pClkInfo->nClkGenDiv < 255)
			break;
	}

	nTemp = getquotient(srcFreq, (pClkInfo->nClkGenDiv * pClkInfo->nClkDiv));
	if (nTemp <= pClkInfo->nFreqHz) {
		fRet = CTRUE;
		goto exit_getparam;
	}

	if (nRetry) {
		nRetry--;
		goto retry_getparam;
	}

exit_getparam:

	return fRet;
}
#endif

CBOOL __init NX_SDMMC_FindPLL800()
{
	CBOOL ret = CFALSE;;

	if (CONFIG_S5P_PLL0_FREQ == 800) {
		clkInfo.nPllNum = 0;
		ret = CTRUE;
	} else if (CONFIG_S5P_PLL1_FREQ == 800) {
		clkInfo.nPllNum = 1;
		ret = CTRUE;
	} else if (CONFIG_S5P_PLL2_FREQ == 800) {
		clkInfo.nPllNum = 2;
		ret = CTRUE;
	} else {
		clkInfo.nPllNum = SDXC_CLKGENSRC;
		ret = CFALSE;
	}

	return ret;
}

//------------------------------------------------------------------------------
//static CBOOL	NX_SDMMC_SetClock( SDXCBOOTSTATUS * pSDXCBootStatus, CBOOL enb, U32 divider )
static CBOOL __init NX_SDMMC_SetClock( SDXCBOOTSTATUS * pSDXCBootStatus, CBOOL enb, U32 nFreq )
{
	volatile U32 timeout;
	register struct NX_SDMMC_RegisterSet * const pSDXCReg = pgSDXCReg[pSDXCBootStatus->SDPort];
	register struct NX_CLKGEN_RegisterSet * const pSDClkGenReg = pgSDClkGenReg[pSDXCBootStatus->SDPort];
	CBOOL ret;

#if defined(VERBOSE)
	dprintf("NX_SDMMC_SetClock : divider = %d\r\n", divider);
#endif

	//	NX_ASSERT( (1==divider) || (0==(divider&1)) );		// 1 or even number
	//	NX_ASSERT( (0<divider) && (510>=divider) );			// between 1 and 510

	//--------------------------------------------------------------------------
	// 1. Confirm that no card is engaged in any transaction.
	//	If there's a transaction, wait until it has been finished.
	//	while( NX_SDXC_IsDataTransferBusy() );
	//	while( NX_SDXC_IsCardDataBusy() );

#if defined(NX_DEBUG)
	if( pSDXCReg->STATUS & (NX_SDXC_STATUS_DATABUSY | NX_SDXC_STATUS_FSMBUSY) ) {
#if defined(NX_DEBUG)
		if( pSDXCReg->STATUS & NX_SDXC_STATUS_DATABUSY )
			dprintf("NX_SDMMC_SetClock : ERROR - Data is busy\r\n" );

		if( pSDXCReg->STATUS & NX_SDXC_STATUS_FSMBUSY )
			dprintf("NX_SDMMC_SetClock : ERROR - Data Transfer is busy\r\n" );
#endif
		//return CFALSE;
		timeout = NX_SDMMC_TIMEOUT;
		while(timeout--) {
			if(!(pSDXCReg->STATUS & (NX_SDXC_STATUS_DATABUSY | NX_SDXC_STATUS_FSMBUSY)))
				break;
		}
		if(timeout == 0) {
			INFINTE_LOOP();
		}
	}
#endif

	//--------------------------------------------------------------------------
	// 2. Disable the output clock.
	pSDXCReg->CLKENA &= ~NX_SDXC_CLKENA_CLKENB;
	pSDXCReg->CLKENA |= NX_SDXC_CLKENA_LOWPWR;	// low power mode & clock disable

	pSDClkGenReg->CLKENB = NX_PCLKMODE_ALWAYS<<3 | NX_BCLKMODE_DYNAMIC<<0;

	fixedPLL = NX_SDMMC_FindPLL800();
	clkInfo.nFreqHz = nFreq;

	if (fixedPLL == CTRUE) {
		pSDClkGenReg->CLKGEN[0] = (pSDClkGenReg->CLKGEN[0] &
					   ~(0x7<<2 | 0xFF<<5))
			| (clkInfo.nPllNum<<2)		// set clock source
			| ((SDXC_CLKGENDIV-1)<<5)	// set clock divisor
			| (0UL<<1);			// set clock invert

		if (nFreq == CONFIG_S5P_SDMMC_CMD_CLOCK){
			pSDXCReg->CLKDIV = (SDXC_CLKDIV_400KHZ>>1);	// 2*n divider (0 : bypass)
		} else {
			pSDXCReg->CLKDIV = (SDXC_CLKDIV>>1);		// 2*n divider (0 : bypass)
		}
	} else {
		ret = NX_SDMMC_GetClkParam( &clkInfo );
		if (ret == CTRUE) {
			pSDClkGenReg->CLKGEN[0] = (pSDClkGenReg->CLKGEN[0] &
						   ~(0x7<<2 | 0xFF<<5))
				| (clkInfo.nPllNum<<2)		// set clock source
				| ((clkInfo.nClkGenDiv-1)<<5)	// set clock divisor
				| (0UL<<1);			// set clock invert

			pSDXCReg->CLKDIV = (clkInfo.nClkDiv>>1);    //  2*n divider (0 : bypass)
		}
	}

	pSDClkGenReg->CLKENB |= 0x1UL<<2;			// clock generation enable
	pSDXCReg->CLKENA &= ~NX_SDXC_CLKENA_LOWPWR;	// normal power mode
	//--------------------------------------------------------------------------
	// 3. Program the clock divider as required.
	//	pSDXCReg->CLKSRC = 0;	// prescaler 0
	//	pSDXCReg->CLKDIV = SDXC_CLKDIV>>1;	//	2*n divider (0 : bypass)
	//	pSDXCReg->CLKDIV = (divider>>1);	//	2*n divider (0 : bypass)

	//--------------------------------------------------------------------------
	// 4. Start a command with NX_SDXC_CMDFLAG_UPDATECLKONLY flag.
repeat_4 :
	pSDXCReg->CMD = 0 | NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_UPDATECLKONLY | NX_SDXC_CMDFLAG_STOPABORT;

	//--------------------------------------------------------------------------
	// 5. Wait until a update clock command is taken by the SDXC module.
	//	If a HLE is occurred, repeat 4.
	timeout = 0;
	while( pSDXCReg->CMD & NX_SDXC_CMDFLAG_STARTCMD ) {
		if( ++timeout > NX_SDMMC_TIMEOUT ) {
			dprintf("NX_SDMMC_SetClock : ERROR - Time-out to update clock.\r\n");
			INFINTE_LOOP();
			return CFALSE;
		}
	}

	if( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_HLE ) {
		INFINTE_LOOP();
		pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_HLE;
		goto repeat_4;
	}

	if (CFALSE == enb)
		return CTRUE;

	//--------------------------------------------------------------------------
	// 6. Enable the output clock.
	pSDXCReg->CLKENA |= NX_SDXC_CLKENA_CLKENB;

	//--------------------------------------------------------------------------
	// 7. Start a command with NX_SDXC_CMDFLAG_UPDATECLKONLY flag.
repeat_7 :
	//	pSDXCReg->CMD = 0 | NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_UPDATECLKONLY | NX_SDXC_CMDFLAG_WAITPRVDAT;
	pSDXCReg->CMD = 0 | NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_UPDATECLKONLY | NX_SDXC_CMDFLAG_STOPABORT;

	//--------------------------------------------------------------------------
	// 8. Wait until a update clock command is taken by the SDXC module.
	//	If a HLE is occurred, repeat 7.
	timeout = 0;
	while( pSDXCReg->CMD & NX_SDXC_CMDFLAG_STARTCMD ){
		if( ++timeout > NX_SDMMC_TIMEOUT ){
			dprintf("NX_SDMMC_SetClock : ERROR - TIme-out to update clock2.\r\n");
			INFINTE_LOOP();
			return CFALSE;
		}
	}

	if( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_HLE ) {
		INFINTE_LOOP();
		pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_HLE;
		goto repeat_7;
	}

	return CTRUE;
}

//------------------------------------------------------------------------------
static U32 __init NX_SDMMC_SendCommandInternal(SDXCBOOTSTATUS *pSDXCBootStatus, NX_SDMMC_COMMAND *pCommand)
{
	U32 cmd, flag;
	U32 status = 0;
	volatile U32 timeout;
	register struct NX_SDMMC_RegisterSet *const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

	NX_ASSERT(CNULL != pCommand);

#ifdef VERBOSE
	dprintf("NX_SDMMC_SendCommandInternal : Command(0x%08X), "
			"Argument(0x%08X)\r\n",
			pCommand->cmdidx, pCommand->arg);
#endif

	cmd = pCommand->cmdidx & 0xFF;
	flag = pCommand->flag;

	pSDXCReg->RINTSTS = 0xFFFFFFFF;

	//--------------------------------------------------------------------------
	// Send Command
	timeout = 0;
	do{
		pSDXCReg->RINTSTS	= NX_SDXC_RINTSTS_HLE;
		pSDXCReg->CMDARG	= pCommand->arg;
		pSDXCReg->CMD		= cmd | flag | NX_SDXC_CMDFLAG_USE_HOLD_REG;
		while( pSDXCReg->CMD & NX_SDXC_CMDFLAG_STARTCMD ) {
			if( ++timeout > NX_SDMMC_TIMEOUT ) {
				dprintf("NX_SDMMC_SendCommandInternal : ERROR - Time-Out to send command.\r\n");
				status |= NX_SDMMC_STATUS_CMDBUSY;
				INFINTE_LOOP();
				goto End;
			}
		}
	} while( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_HLE );

	//--------------------------------------------------------------------------
	// Wait until Command sent to card and got response from card.
	timeout = 0;
	while( 1 ) {
		if( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_CD )
			break;

		if( ++timeout > NX_SDMMC_TIMEOUT ) {
			dprintf("NX_SDMMC_SendCommandInternal : ERROR - Time-Out to wait command done.\r\n");
			status |= NX_SDMMC_STATUS_CMDTOUT;
			INFINTE_LOOP();
			goto End;
		}

		if( (flag & NX_SDXC_CMDFLAG_STOPABORT) && (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_HTO) ) {
			// You have to clear FIFO when HTO is occurred.
			// After that, SDXC module leaves in stopped state and takes next command.
			while( 0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY) ) {
				pSDXCReg->DATA;
			}
		}
	}

	// Check Response Error
	if( pSDXCReg->RINTSTS & (NX_SDXC_RINTSTS_RCRC | NX_SDXC_RINTSTS_RE | NX_SDXC_RINTSTS_RTO) ) {
		if( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_RCRC )		status |= NX_SDMMC_STATUS_RESCRCFAIL;
		if( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_RE )		status |= NX_SDMMC_STATUS_RESERROR;
		if( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_RTO )		status |= NX_SDMMC_STATUS_RESTOUT;
	}

	if( (status == NX_SDMMC_STATUS_NOERROR) && (flag & NX_SDXC_CMDFLAG_SHORTRSP) ) {
		pCommand->response[0] = pSDXCReg->RESP0;
		if( (flag & NX_SDXC_CMDFLAG_LONGRSP) == NX_SDXC_CMDFLAG_LONGRSP ) {
			pCommand->response[1] = pSDXCReg->RESP1;
			pCommand->response[2] = pSDXCReg->RESP2;
			pCommand->response[3] = pSDXCReg->RESP3;
		}

		if( NX_SDMMC_RSPIDX_R1B == ((pCommand->cmdidx >> 8) & 0xFF) ) {
			timeout = 0;
			do {
				if( ++timeout > NX_SDMMC_TIMEOUT ) {
					dprintf("NX_SDMMC_SendCommandInternal : ERROR - Time-Out to wait card data is ready.\r\n");
					status |= NX_SDMMC_STATUS_DATABUSY;
					INFINTE_LOOP();
					goto End;
				}
			} while( pSDXCReg->STATUS & NX_SDXC_STATUS_DATABUSY );
		}
	}

End:

#if defined(NX_DEBUG)
	if( NX_SDMMC_STATUS_NOERROR != status ) {
		dprintf("NX_SDMMC_SendCommandInternal Failed : command(0x%08X), argument(0x%08X) => status(0x%08X)\r\n",
				pCommand->cmdidx, pCommand->arg, status);
	}
#endif

	pCommand->status = status;

	return status;
}

//------------------------------------------------------------------------------
static U32 __init NX_SDMMC_SendStatus(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	U32 status;
	NX_SDMMC_COMMAND cmd;

	cmd.cmdidx	= SEND_STATUS;
	cmd.arg		= pSDXCBootStatus->rca;
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;

	status = NX_SDMMC_SendCommandInternal(pSDXCBootStatus, &cmd);

#if defined(VERBOSE) && defined(NX_DEBUG) && (1)
	if( NX_SDMMC_STATUS_NOERROR == status ) {
		dprintf("\t NX_SDMMC_SendStatus : idx:0x%08X, arg:0x%08X, resp:0x%08X\r\n",
				cmd.cmdidx, cmd.arg, cmd.response[0]);

		if( cmd.response[0] & (1UL<<31) )	dprintf( "\t\t ERROR : OUT_OF_RANGE\r\n" );
		if( cmd.response[0] & (1UL<<30) )	dprintf( "\t\t ERROR : ADDRESS_ERROR\r\n" );
		if( cmd.response[0] & (1UL<<29) )	dprintf( "\t\t ERROR : BLOCK_LEN_ERROR\r\n" );
		if( cmd.response[0] & (1UL<<28) )	dprintf( "\t\t ERROR : ERASE_SEQ_ERROR\r\n" );
		if( cmd.response[0] & (1UL<<27) )	dprintf( "\t\t ERROR : ERASE_PARAM\r\n" );
		if( cmd.response[0] & (1UL<<26) )	dprintf( "\t\t ERROR : WP_VIOLATION\r\n" );
		if( cmd.response[0] & (1UL<<24) )	dprintf( "\t\t ERROR : LOCK_UNLOCK_FAILED\r\n" );
		if( cmd.response[0] & (1UL<<23) )	dprintf( "\t\t ERROR : COM_CRC_ERROR\r\n" );
		if( cmd.response[0] & (1UL<<22) )	dprintf( "\t\t ERROR : ILLEGAL_COMMAND\r\n" );
		if( cmd.response[0] & (1UL<<21) )	dprintf( "\t\t ERROR : CARD_ECC_FAILED\r\n" );
		if( cmd.response[0] & (1UL<<20) )	dprintf( "\t\t ERROR : Internal Card Controller ERROR\r\n" );
		if( cmd.response[0] & (1UL<<19) )	dprintf( "\t\t ERROR : General Error\r\n" );
		if( cmd.response[0] & (1UL<<17) )	dprintf( "\t\t ERROR : Deferred Response\r\n" );
		if( cmd.response[0] & (1UL<<16) )	dprintf( "\t\t ERROR : CID/CSD_OVERWRITE_ERROR\r\n" );
		if( cmd.response[0] & (1UL<<15) )	dprintf( "\t\t ERROR : WP_ERASE_SKIP\r\n" );
		if( cmd.response[0] & (1UL<< 3) )	dprintf( "\t\t ERROR : AKE_SEQ_ERROR\r\n" );

		switch( (cmd.response[0]>>9) & 0xF )
		{
			case 0 : dprintf( "\t\t CURRENT_STATE : Idle\r\n");				break;
			case 1 : dprintf( "\t\t CURRENT_STATE : Ready\r\n" );			break;
			case 2 : dprintf( "\t\t CURRENT_STATE : Identification\r\n" );	break;
			case 3 : dprintf( "\t\t CURRENT_STATE : Standby\r\n" );			break;
			case 4 : dprintf( "\t\t CURRENT_STATE : Transfer\r\n" );		break;
			case 5 : dprintf( "\t\t CURRENT_STATE : Data\r\n" );			break;
			case 6 : dprintf( "\t\t CURRENT_STATE : Receive\r\n" );			break;
			case 7 : dprintf( "\t\t CURRENT_STATE : Programming\r\n" );		break;
			case 8 : dprintf( "\t\t CURRENT_STATE : Disconnect\r\n" );		break;
			case 9 : dprintf( "\t\t CURRENT_STATE : Sleep\r\n" );			break;
			default: dprintf( "\t\t CURRENT_STATE : Reserved\r\n" );		break;
		}
	}
#endif

	return status;
}

//------------------------------------------------------------------------------
static U32 __init NX_SDMMC_SendCommand(SDXCBOOTSTATUS *pSDXCBootStatus, NX_SDMMC_COMMAND *pCommand)
{
	U32 status;

	status = NX_SDMMC_SendCommandInternal(pSDXCBootStatus, pCommand);
	if (NX_SDMMC_STATUS_NOERROR != status) {
		NX_SDMMC_SendStatus(pSDXCBootStatus);
	}

	return status;
}

//------------------------------------------------------------------------------
static U32 __init NX_SDMMC_SendAppCommand(SDXCBOOTSTATUS *pSDXCBootStatus, NX_SDMMC_COMMAND *pCommand)
{
	U32 status;
	NX_SDMMC_COMMAND cmd;

	cmd.cmdidx = APP_CMD;
	cmd.arg = pSDXCBootStatus->rca;
	cmd.flag = NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT |
		NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;

	status = NX_SDMMC_SendCommandInternal(pSDXCBootStatus, &cmd);
	if (NX_SDMMC_STATUS_NOERROR == status) {
		NX_SDMMC_SendCommand(pSDXCBootStatus, pCommand);
	}

	return status;
}

//------------------------------------------------------------------------------
static CBOOL __init NX_SDMMC_IdentifyCard(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	S32 timeout;
	U32 HCS, RCA;
	NX_SDMMC_CARDTYPE CardType = NX_SDMMC_CARDTYPE_UNKNOWN;
	NX_SDMMC_COMMAND cmd;
	struct NX_SDMMC_RegisterSet *const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

#if 0
	if( CFALSE == NX_SDMMC_SetClock( pSDXCBootStatus, CTRUE, SDXC_CLKGENDIV_400KHZ ) )
		return CFALSE;
#else
	if (CFALSE == NX_SDMMC_SetClock(pSDXCBootStatus, CTRUE, 400000))
		return CFALSE;
#endif

	// Data Bus Width : 0(1-bit), 1(4-bit)
	pSDXCReg->CTYPE = 0;

	pSDXCBootStatus->rca = 0;

	//--------------------------------------------------------------------------
	//	Identify SD/MMC card
	//--------------------------------------------------------------------------
	// Go idle state
	cmd.cmdidx	= GO_IDLE_STATE;
	cmd.arg		= 0;
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_SENDINIT | NX_SDXC_CMDFLAG_STOPABORT;

	NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd);

	// Check SD Card Version
	cmd.cmdidx	= SEND_IF_COND;
	cmd.arg		= (1<<8) | 0xAA;	// argument = VHS : 2.7~3.6V and Check Pattern(0xAA)
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;

	if (NX_SDMMC_STATUS_NOERROR ==
			NX_SDMMC_SendCommandInternal(pSDXCBootStatus, &cmd)) {
		// Ver 2.0 or later SD Memory Card
		if (cmd.response[0] != ((1 << 8) | 0xAA))
			return CFALSE;

		HCS = 1 << 30;
		dprintf("Ver 2.0 or later SD Memory Card\r\n");
	} else {
		// voltage mismatch or Ver 1.X SD Memory Card or not SD Memory
		// Card
		HCS = 0;
		dprintf("voltage mismatch or Ver 1.X SD Memory Card or not SD Memory Card\r\n");
	}

	//--------------------------------------------------------------------------
	// voltage validation
	timeout = NX_SDMMC_TIMEOUT_IDENTIFY;

	cmd.cmdidx = APP_CMD;
	cmd.arg = pSDXCBootStatus->rca;
	cmd.flag = NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT |
		NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;

	if( NX_SDMMC_STATUS_NOERROR == NX_SDMMC_SendCommandInternal( pSDXCBootStatus, &cmd ) ) {
		//----------------------------------------------------------------------
		// SD memory card
#define FAST_BOOT	(1<<29)

		cmd.cmdidx	= SD_SEND_OP_COND;
		cmd.arg		= (HCS | FAST_BOOT | 0x00FC0000);	// 3.0 ~ 3.6V
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT | NX_SDXC_CMDFLAG_SHORTRSP;

		if( NX_SDMMC_STATUS_NOERROR != NX_SDMMC_SendCommandInternal( pSDXCBootStatus, &cmd ) ) {
			return CFALSE;
		}

		while( 0==(cmd.response[0] & (1UL<<31)) )	// Wait until card has finished the power up routine
		{
			if( NX_SDMMC_STATUS_NOERROR != NX_SDMMC_SendAppCommand( pSDXCBootStatus, &cmd ) ) {
				return CFALSE;
			}

			if( timeout-- <= 0 ) {
				dprintf("NX_SDMMC_IdentifyCard : ERROR - Time-Out to wait power up for SD.\r\n");
				return CFALSE;
			}
		}

#if defined(VERBOSE)
		dprintf("--> Found SD Memory Card.\r\n");
		dprintf("--> SD_SEND_OP_COND Response = 0x%08X\r\n", cmd.response[0]);
#endif

		CardType	= NX_SDMMC_CARDTYPE_SDMEM;
		RCA			= 0;
	} else {
		//----------------------------------------------------------------------
		// MMC memory card
		cmd.cmdidx	= GO_IDLE_STATE;
		cmd.arg		= 0;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_SENDINIT | NX_SDXC_CMDFLAG_STOPABORT;

		NX_SDMMC_SendCommand( pSDXCBootStatus, &cmd );

		do {
			cmd.cmdidx	= SEND_OP_COND;
			cmd.arg		= 0x40FC0000;	// MMC High Capacity -_-???
			cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT | NX_SDXC_CMDFLAG_SHORTRSP;
			if( NX_SDMMC_STATUS_NOERROR != NX_SDMMC_SendCommand( pSDXCBootStatus, &cmd ) ) {
				return CFALSE;
			}

			if( timeout-- <= 0 ) {
				dprintf("NX_SDMMC_IdentifyCard : ERROR - Time-Out to wait power-up for MMC.\r\n");
				return CFALSE;
			}
		} while( 0==(cmd.response[0] & (1UL<<31)) );	// Wait until card has finished the power up routine

#if defined(VERBOSE)
		dprintf("--> Found MMC Memory Card.\r\n");
		dprintf("--> SEND_OP_COND Response = 0x%08X\r\n", cmd.response[0]);
#endif

		CardType	= NX_SDMMC_CARDTYPE_MMC;
		RCA			= 2<<16;
	}

	//	NX_ASSERT( (cmd.response[0] & 0x00FC0000) == 0x00FC0000 );
	pSDXCBootStatus->bHighCapacity = (cmd.response[0] & (1<<30)) ? CTRUE : CFALSE;

#if defined(NX_DEBUG)
	if (pSDXCBootStatus->bHighCapacity)
		dprintf("--> High Capacity Memory Card.\r\n");
#endif

	//--------------------------------------------------------------------------
	// Get CID
	cmd.cmdidx	= ALL_SEND_CID;
	cmd.arg		= 0;
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_LONGRSP;
	if( NX_SDMMC_STATUS_NOERROR != NX_SDMMC_SendCommand( pSDXCBootStatus, &cmd ) )
		return CFALSE;

	//--------------------------------------------------------------------------
	// Get RCA and change to Stand-by State in data transfer mode
	cmd.cmdidx	= (CardType == NX_SDMMC_CARDTYPE_SDMEM) ? SEND_RELATIVE_ADDR : SET_RELATIVE_ADDR;
	cmd.arg		= RCA;
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;
	if( NX_SDMMC_STATUS_NOERROR != NX_SDMMC_SendCommand( pSDXCBootStatus, &cmd ) )
		return CFALSE;

	if( CardType == NX_SDMMC_CARDTYPE_SDMEM )
		pSDXCBootStatus->rca = cmd.response[0] & 0xFFFF0000;
	else
		pSDXCBootStatus->rca = RCA;

#if defined(VERBOSE)
	dprintf("RCA = 0x%08X\r\n", pSDXCBootStatus->rca);
#endif

	pSDXCBootStatus->CardType = CardType;

	return CTRUE;
}

//------------------------------------------------------------------------------
static CBOOL __init NX_SDMMC_SelectCard(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	U32 status;
	NX_SDMMC_COMMAND cmd;

	cmd.cmdidx = SELECT_CARD;
	cmd.arg = pSDXCBootStatus->rca;
	cmd.flag = NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT |
		NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;

	status = NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd);

	return (NX_SDMMC_STATUS_NOERROR == status) ? CTRUE : CFALSE;
}

//------------------------------------------------------------------------------
static CBOOL __init NX_SDMMC_SetCardDetectPullUp(SDXCBOOTSTATUS *pSDXCBootStatus, CBOOL bEnb)
{
	U32 status;
	NX_SDMMC_COMMAND cmd;

	cmd.cmdidx = SET_CLR_CARD_DETECT;
	cmd.arg = (bEnb) ? 1 : 0;
	cmd.flag = NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT |
		NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;

	status = NX_SDMMC_SendAppCommand(pSDXCBootStatus, &cmd);

	return (NX_SDMMC_STATUS_NOERROR == status) ? CTRUE : CFALSE;
}

//------------------------------------------------------------------------------
static CBOOL __init NX_SDMMC_SetBusWidth(SDXCBOOTSTATUS *pSDXCBootStatus, U32 buswidth)
{
	U32 status;
	NX_SDMMC_COMMAND cmd;
	register struct NX_SDMMC_RegisterSet * const pSDXCReg = pgSDXCReg[pSDXCBootStatus->SDPort];

	NX_ASSERT(buswidth == 1 || buswidth == 4);

	if( pSDXCBootStatus->CardType == NX_SDMMC_CARDTYPE_SDMEM ) {
		cmd.cmdidx	= SET_BUS_WIDTH;
		cmd.arg		= (buswidth>>1);
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;
		status = NX_SDMMC_SendAppCommand( pSDXCBootStatus, &cmd );
	} else {
		// ExtCSD[183] : BUS_WIDTH <= 0 : 1-bit, 1 : 4-bit, 2 : 8-bit
		cmd.cmdidx	= SWITCH_FUNC;
		cmd.arg		= (3<<24) | (183<<16) | ((buswidth>>2)<<8) | (0<<0);
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;
		status = NX_SDMMC_SendCommand( pSDXCBootStatus, &cmd );
	}

	if( NX_SDMMC_STATUS_NOERROR != status )
		return CFALSE;

	// 0 : 1-bit mode, 1 : 4-bit mode
	pSDXCReg->CTYPE = buswidth >> 2;

	return CTRUE;
}

//------------------------------------------------------------------------------
static CBOOL __init NX_SDMMC_SetBlockLength( SDXCBOOTSTATUS * pSDXCBootStatus, U32 blocklength )
{
	U32 status;
	NX_SDMMC_COMMAND cmd;
	register struct NX_SDMMC_RegisterSet * const pSDXCReg = pgSDXCReg[pSDXCBootStatus->SDPort];

	cmd.cmdidx = SET_BLOCKLEN;
	cmd.arg = blocklength;
	cmd.flag = NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT |
		NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;
	status = NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd);

	if (NX_SDMMC_STATUS_NOERROR != status)
		return CFALSE;

	pSDXCReg->BLKSIZ = blocklength;

	return CTRUE;
}

//------------------------------------------------------------------------------
CBOOL __init NX_SDMMC_Init(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	register struct NX_SDMMC_RegisterSet *const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];
	register struct NX_CLKGEN_RegisterSet *const pSDClkGenReg =
		pgSDClkGenReg[pSDXCBootStatus->SDPort];
#if 1
	CBOOL ret;

	fixedPLL = NX_SDMMC_FindPLL800();
	clkInfo.nFreqHz = CONFIG_S5P_SDMMC_CMD_CLOCK;

	ret = NX_SDMMC_GetClkParam(&clkInfo);
	if (ret == CFALSE)
		dprintf("get clock param fail.\r\n");
#endif

	// CLKGEN
	pSDClkGenReg->CLKENB = NX_PCLKMODE_ALWAYS<<3 | NX_BCLKMODE_DYNAMIC<<0;
	if (fixedPLL == CTRUE) {
		pSDClkGenReg->CLKGEN[0] = (pSDClkGenReg->CLKGEN[0] &
					   ~(0x7<<2 | 0xFF<<5))
			| (clkInfo.nPllNum<<2)			// set clock source
			| ((SDXC_CLKGENDIV-1)<<5)		// set clock divisor
			| (0UL<<1);				// set clock invert
	} else {
		pSDClkGenReg->CLKGEN[0] = (pSDClkGenReg->CLKGEN[0] &
					   ~(0x7<<2 | 0xFF<<5))
			| (clkInfo.nPllNum<<2)			// set clock source
			| ((clkInfo.nClkGenDiv-1)<<5)		// set clock divisor
			| (0UL<<1);				// set clock invert
	}
	pSDClkGenReg->CLKENB |= 0x1UL << 2; // clock generation enable

	reset_con(SDResetNum[pSDXCBootStatus->SDPort], CTRUE);  // reset on
	reset_con(SDResetNum[pSDXCBootStatus->SDPort], CFALSE); // reset negate

	pSDXCReg->PWREN = 0 << 0; // Set Power Disable

	//	pSDXCReg->UHS_REG |= 1<<0;		// for DDR mode

	pSDXCReg->CLKENA = NX_SDXC_CLKENA_LOWPWR;	// low power mode & clock disable
	pSDXCReg->CLKSRC = 0; // prescaler 0

	if (fixedPLL == CTRUE) {
		pSDXCReg->CLKCTRL = 2<<24 |		// sample clock phase shift 0:0 1:90 2:180 3:270
			3<<16 |				// drive clock phase shift 0:0 1:90 2:180 3:270
			0<<8 |				// sample clock delay
			0<<0;				// drive clock delay

		pSDXCReg->CLKDIV = (SDXC_CLKDIV_400KHZ >>1);	// 2*n divider (0 : bypass)
	} else {
		pSDXCReg->CLKCTRL = 0<<24 |		// sample clock phase shift 0:0 1:90 2:180 3:270
			2<<16 |				// drive clock phase shift 0:0 1:90 2:180 3:270
			0<<8 |				// sample clock delay
			0<<0;				// drive clock delay

		pSDXCReg->CLKDIV = (clkInfo.nClkGenDiv >> 1);
	}

	pSDXCReg->CTRL &= ~(NX_SDXC_CTRL_DMAMODE_EN | NX_SDXC_CTRL_READWAIT);	// fifo mode, not read wait(only use sdio mode)
	// Reset the controller & DMA interface & FIFO
	pSDXCReg->CTRL = NX_SDXC_CTRL_DMARST | NX_SDXC_CTRL_FIFORST | NX_SDXC_CTRL_CTRLRST;
	while( pSDXCReg->CTRL & (NX_SDXC_CTRL_DMARST | NX_SDXC_CTRL_FIFORST | NX_SDXC_CTRL_CTRLRST) );

	pSDXCReg->PWREN = 0x1<<0;	// Set Power Enable


	// Data Timeout = 0xFFFFFF, Response Timeout = 0x64
	pSDXCReg->TMOUT = (0xFFFFFFU << 8) | (0x64 << 0);

	// Data Bus Width : 0(1-bit), 1(4-bit)
	pSDXCReg->CTYPE = 0;

	// Block size
	pSDXCReg->BLKSIZ = BLOCK_LENGTH;

	// Issue when RX FIFO Count >= 8 x 4 bytes & TX FIFO Count <= 8 x 4 bytes
	pSDXCReg->FIFOTH = ((8-1)<<16) |		// Rx threshold
		(8<<0);			// Tx threshold

	// Mask & Clear All interrupts
	pSDXCReg->INTMASK = 0;
	pSDXCReg->RINTSTS = 0xFFFFFFFF;

	return CTRUE;
}

//------------------------------------------------------------------------------
CBOOL __init NX_SDMMC_Terminate(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	register struct NX_SDMMC_RegisterSet *const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];
	// Clear All interrupts
	pSDXCReg->RINTSTS = 0xFFFFFFFF;

	// Reset the controller & DMA interface & FIFO
	pSDXCReg->CTRL = NX_SDXC_CTRL_DMARST | NX_SDXC_CTRL_FIFORST | NX_SDXC_CTRL_CTRLRST;
	while( pSDXCReg->CTRL & (NX_SDXC_CTRL_DMARST | NX_SDXC_CTRL_FIFORST | NX_SDXC_CTRL_CTRLRST) );

	// Disable CLKGEN
	pgSDClkGenReg[pSDXCBootStatus->SDPort]->CLKENB = 0;

	reset_con(SDResetNum[pSDXCBootStatus->SDPort], CTRUE); // reset on

	return CTRUE;
}

//------------------------------------------------------------------------------
CBOOL __init NX_SDMMC_Open(SDXCBOOTSTATUS *pSDXCBootStatus) // U32 option )
{
	//--------------------------------------------------------------------------
	// card identification mode : Identify & Initialize
	if (CFALSE == NX_SDMMC_IdentifyCard(pSDXCBootStatus)) {
		printf("Card Identify Failure\r\n");
		return CFALSE;
	}

	//--------------------------------------------------------------------------
	// data transfer mode : Stand-by state
#if 0
	if( CFALSE == NX_SDMMC_SetClock( pSDXCBootStatus, CTRUE, SDXC_CLKGENDIV) )
		return CFALSE;
#else
	if (CFALSE == NX_SDMMC_SetClock(pSDXCBootStatus, CTRUE, CONFIG_S5P_SDMMC_CLOCK))
		return CFALSE;
#endif
	if (CFALSE == NX_SDMMC_SelectCard(pSDXCBootStatus))
		return CFALSE;

	//--------------------------------------------------------------------------
	// data transfer mode : Transfer state
	if (pSDXCBootStatus->CardType == NX_SDMMC_CARDTYPE_SDMEM) {
		NX_SDMMC_SetCardDetectPullUp(pSDXCBootStatus, CFALSE);
	}

	if (CFALSE == NX_SDMMC_SetBlockLength(pSDXCBootStatus, BLOCK_LENGTH))
		return CFALSE;

	NX_SDMMC_SetBusWidth(pSDXCBootStatus, 4);

	return CTRUE;
}

//------------------------------------------------------------------------------
CBOOL __init NX_SDMMC_Close(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	//	NX_SDMMC_SetClock( pSDXCBootStatus, CFALSE, SDXC_CLKGENDIV_400KHZ );
	NX_SDMMC_SetClock( pSDXCBootStatus, CFALSE, 400000 );
	return CTRUE;
}

//------------------------------------------------------------------------------
static CBOOL __init NX_SDMMC_ReadSectorData(SDXCBOOTSTATUS *pSDXCBootStatus, U32 numberOfSector, U32 *pdwBuffer)
{
	U32		count;
	register struct NX_SDMMC_RegisterSet * const pSDXCReg = pgSDXCReg[pSDXCBootStatus->SDPort];

	NX_ASSERT(0 == ((U32)pdwBuffer & 3));

	count = numberOfSector * BLOCK_LENGTH;
	NX_ASSERT(0 == (count % 32));

	while (0 < count) {
		if ((pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_RXDR) ||
				(pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_DTO)) {
			U32 FSize, Timeout = NX_SDMMC_TIMEOUT;
			while ((pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY) && Timeout--);
			if (0 == Timeout)
				break;
			FSize = (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOCOUNT) >> 17;
			count -= (FSize * 4);
			while (FSize) {
				*pdwBuffer++ = pSDXCReg->DATA;
				FSize--;
			}

			pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_RXDR;
		}

		// Check Errors
		if( pSDXCReg->RINTSTS & (NX_SDXC_RINTSTS_DRTO | NX_SDXC_RINTSTS_EBE | NX_SDXC_RINTSTS_SBE | NX_SDXC_RINTSTS_DCRC) ) {
#if defined(NX_DEBUG)
			dprintf("Read left = %d\r\n", count);

			if( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_DRTO )
				dprintf("ERROR : NX_SDMMC_ReadSectors - NX_SDXC_RINTSTS_DRTO\r\n");
			if( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_EBE )
				dprintf("ERROR : NX_SDMMC_ReadSectors - NX_SDXC_RINTSTS_EBE\r\n");
			if( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_SBE )
				dprintf("ERROR : NX_SDMMC_ReadSectors - NX_SDXC_RINTSTS_SBE\r\n");
			if( pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_DCRC )
				dprintf("ERROR : NX_SDMMC_ReadSectors - NX_SDXC_RINTSTS_DCRC\r\n");
#endif

			return CFALSE;
		}

		if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_DTO) {
			if (count == 0) {
				pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_DTO;
				break;
			}
		}

#if defined(NX_DEBUG)
		if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_HTO) {
			dprintf("ERROR : NX_SDMMC_ReadSectors - "
					"NX_SDXC_RINTSTS_HTO\r\n");
			pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_HTO;
		}
#endif

		NX_ASSERT(0 == (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_FRUN));
	}

	pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_DTO;

	return CTRUE;
}

//------------------------------------------------------------------------------
CBOOL __init NX_SDMMC_ReadSectors(SDXCBOOTSTATUS *pSDXCBootStatus, U32 SectorNum,
		U32 numberOfSector, U32 *pdwBuffer)
{
	CBOOL	result = CFALSE;
	U32		status;
#if defined(NX_DEBUG)
	U32	response;
#endif
	NX_SDMMC_COMMAND cmd;
	register struct NX_SDMMC_RegisterSet *const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

	NX_ASSERT(0 == ((U32)pdwBuffer & 3));

	while (pSDXCReg->STATUS & (1 << 9 | 1 << 10)); // wait while data busy or data transfer busy

	//--------------------------------------------------------------------------
	// wait until 'Ready for data' is set and card is in transfer state.
	do {
		cmd.cmdidx	= SEND_STATUS;
		cmd.arg		= pSDXCBootStatus->rca;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;
		status = NX_SDMMC_SendCommand( pSDXCBootStatus, &cmd );
		if( NX_SDMMC_STATUS_NOERROR != status )
			goto End;
	} while( !((cmd.response[0] & (1<<8)) && (((cmd.response[0]>>9)&0xF)==4)) );

	NX_ASSERT( NX_SDXC_STATUS_FIFOEMPTY == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY) );
	NX_ASSERT( 0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FSMBUSY) );

	// Set byte count
	pSDXCReg->BYTCNT = numberOfSector * BLOCK_LENGTH;

	//--------------------------------------------------------------------------
	// Send Command
	if( numberOfSector > 1) {
		cmd.cmdidx	= READ_MULTIPLE_BLOCK;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP
			| NX_SDXC_CMDFLAG_BLOCK | NX_SDXC_CMDFLAG_RXDATA | NX_SDXC_CMDFLAG_SENDAUTOSTOP;
	} else {
		cmd.cmdidx	= READ_SINGLE_BLOCK;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP
			| NX_SDXC_CMDFLAG_BLOCK | NX_SDXC_CMDFLAG_RXDATA;
	}
	cmd.arg		= (pSDXCBootStatus->bHighCapacity) ? SectorNum : SectorNum*BLOCK_LENGTH;

	status = NX_SDMMC_SendCommand( pSDXCBootStatus, &cmd );
	if( NX_SDMMC_STATUS_NOERROR != status )
		goto End;

	//--------------------------------------------------------------------------
	// Read data
	if( CTRUE == NX_SDMMC_ReadSectorData( pSDXCBootStatus, numberOfSector, pdwBuffer ) ) {
		NX_ASSERT( NX_SDXC_STATUS_FIFOEMPTY == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY) );
		NX_ASSERT( 0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOFULL) );
		NX_ASSERT( 0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOCOUNT) );

		if( numberOfSector > 1 ) {
			// Wait until the Auto-stop command has been finished.
			while( 0 == (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_ACD) );

			NX_ASSERT( 0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FSMBUSY) );

#if defined(NX_DEBUG)
			// Get Auto-stop response and then check it.
			response = pSDXCReg->RESP1;
			if( response & 0xFDF98008 ) {
				dprintf("ERROR : NX_SDMMC_ReadSectors - Auto Stop Response Failed = 0x%08X\r\n", response);
				//goto End;
			}
#endif
		}

		result = CTRUE;
	}

End:
	if( CFALSE == result ) {
		cmd.cmdidx	= STOP_TRANSMISSION;
		cmd.arg		= 0;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP | NX_SDXC_CMDFLAG_STOPABORT;
		NX_SDMMC_SendCommandInternal( pSDXCBootStatus, &cmd );

		if(0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY)) {
			pSDXCReg->CTRL = NX_SDXC_CTRL_FIFORST;				// Reest the FIFO.
			while( pSDXCReg->CTRL & NX_SDXC_CTRL_FIFORST );		// Wait until the FIFO reset is completed.
		}
	}

	return result;
}


#ifdef OTA_AB_UPDATE
void __init NX_SDMMC_Read1Sector_misc(SDXCBOOTSTATUS *pSDXCBootStatus, U32 SectorNum, U32 *tempBuf)
{
        unsigned int result = -1;
	U32	status;
	NX_SDMMC_COMMAND cmd;
	register struct NX_SDMMC_RegisterSet *const pSDXCReg = pgSDXCReg[pSDXCBootStatus->SDPort];

	while (pSDXCReg->STATUS & (1 << 9 | 1 << 10)); // wait while data busy or data transfer busy

	//--------------------------------------------------------------------------
	// wait until 'Ready for data' is set and card is in transfer state.
	do {
		cmd.cmdidx	= SEND_STATUS;
		cmd.arg		= pSDXCBootStatus->rca;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP;
		status = NX_SDMMC_SendCommand( pSDXCBootStatus, &cmd );
		if( NX_SDMMC_STATUS_NOERROR != status )
			goto End;
	} while( !((cmd.response[0] & (1<<8)) && (((cmd.response[0]>>9)&0xF)==4)) );

	// Set byte count
	pSDXCReg->BYTCNT = BLOCK_LENGTH;

	//--------------------------------------------------------------------------
	// Send Command
	cmd.cmdidx	= READ_SINGLE_BLOCK;
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_WAITPRVDAT | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP
			| NX_SDXC_CMDFLAG_BLOCK | NX_SDXC_CMDFLAG_RXDATA;

	cmd.arg		= (pSDXCBootStatus->bHighCapacity) ? SectorNum : SectorNum*BLOCK_LENGTH;

	status = NX_SDMMC_SendCommand( pSDXCBootStatus, &cmd );
	if( NX_SDMMC_STATUS_NOERROR != status )
		goto End;

	//--------------------------------------------------------------------------
	// Read data
	if( CTRUE == NX_SDMMC_ReadSectorData( pSDXCBootStatus, 1, tempBuf ) ) {
            result = 1;
	}

End:
	if( CFALSE == result ) {
		cmd.cmdidx	= STOP_TRANSMISSION;
		cmd.arg		= 0;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD | NX_SDXC_CMDFLAG_CHKRSPCRC | NX_SDXC_CMDFLAG_SHORTRSP | NX_SDXC_CMDFLAG_STOPABORT;
		NX_SDMMC_SendCommandInternal( pSDXCBootStatus, &cmd );

		if(0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY)) {
			pSDXCReg->CTRL = NX_SDXC_CTRL_FIFORST;				// Reest the FIFO.
			while( pSDXCReg->CTRL & NX_SDXC_CTRL_FIFORST );		// Wait until the FIFO reset is completed.
		}
	}
}

struct andr_slot_metadata {
	u8 priority : 4;
	u8 tries_remaining : 3;
	u8 successful_boot : 1;
	u8 verity_corrupted : 1;
	u8 reserved : 7;
} __attribute__ ((packed, aligned(2)));
//} __packed;

struct andr_bl_control {
	char slot_suffix[4];
	u32 magic;
	u8 version;
	u8 nb_slot : 3;
	u8 recovery_tries_remaining : 3;
	u8 reserved0[2];
	struct andr_slot_metadata slot_info[4];
	u8 reserved1[8];
	u32 crc32_le;
} __attribute__ ((packed, aligned(4)));
//} __packed;

static int ab_compare_slots(const struct andr_slot_metadata *a,
			    const struct andr_slot_metadata *b)
{
	if (a->priority != b->priority)
		return b->priority - a->priority;

	if (a->successful_boot != b->successful_boot)
		return b->successful_boot - a->successful_boot;

	if (a->tries_remaining != b->tries_remaining)
		return b->tries_remaining - a->tries_remaining;

	return 0;
}

static int ab_select_slot(U32 *tempBuf)
{
        struct andr_bl_control *abc = (struct andr_bl_control*)tempBuf;
        int slot = -1;
        int i = 0;

        if (abc->magic != ANDROID_BOOT_CTRL_MAGIC) {
                printf("[BL1]ANDROID: Unknown A/B metadata: %.8x\r\n", abc->magic);
                return -1;
        }
#if defined(NX_DEBUG)
        printf("[bl1][%s] ----- Slot Select INFO -----\r\n", __func__);
        printf("[bl1][%s] abc->slot_suffix  = %s\r\n",   __func__, abc->slot_suffix );
        printf("[bl1][%s] abc->magic        = 0x%x\r\n", __func__, abc->magic       );
        printf("[bl1][%s] abc->nb_slot      = %d\r\n",   __func__, abc->nb_slot     );
        printf("[bl1][%s] abc->crc32_le     = 0x%x\r\n", __func__, abc->crc32_le    );
        printf("[bl1][%s] ----------------------------\r\n", __func__);
#endif
	for (i = 0; i < abc->nb_slot; ++i) {
		if (abc->slot_info[i].verity_corrupted ||
		    !abc->slot_info[i].tries_remaining) {
			printf("[BL1]ANDROID: unbootable slot %d tries: %d, corrupt: %d\r\n",
                               i, abc->slot_info[i].tries_remaining,
                               abc->slot_info[i].verity_corrupted);
			continue;
		}
		dprintf("[BL1]ANDROID: bootable slot %d pri: %d, tries: %d, corrupt: %d, successful: %d\r\n",
			i, abc->slot_info[i].priority,
                        abc->slot_info[i].tries_remaining,
			abc->slot_info[i].verity_corrupted,
			abc->slot_info[i].successful_boot);

		if (slot < 0 || ab_compare_slots(&abc->slot_info[i], &abc->slot_info[slot]) < 0) {
                        slot = i;
		}
	}

	if (slot >= 0 && !abc->slot_info[slot].successful_boot) {
		dprintf("[BL1]ANDROID: Attempting slot %d, tries remaining %d\r\n",
		       slot,
		       abc->slot_info[slot].tries_remaining);
		abc->slot_info[slot].tries_remaining--;
	}

        return slot;
}
#endif //OTA_AB_UPDATE

//------------------------------------------------------------------------------
extern void Decrypt(U32 *SrcAddr, U32 *DestAddr, U32 Size);
static CBOOL SDMMCBOOT(SDXCBOOTSTATUS *pSDXCBootStatus, struct sbi_header *ptbi) // U32 option )
{
#if !defined(SECURE_MODE)
	unsigned int i;
	struct nx_bootheader *ptbh = (struct nx_bootheader *)ptbi;
#endif
	CBOOL	result = CFALSE;
	register struct NX_SDMMC_RegisterSet * const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

#ifdef MMC_REINIT
	if (CFALSE == NX_SDMMC_Open(pSDXCBootStatus)) {
		printf("Cannot Detect SDMMC\r\n");
		return CFALSE;
	}
#else
    if (CFALSE == NX_SDMMC_IdentifyCard(pSDXCBootStatus)) {
        printf("Card Identify Failure\r\n");
        return CFALSE;
    }
    if (CFALSE == NX_SDMMC_SetClock(pSDXCBootStatus, CTRUE, CONFIG_S5P_SDMMC_CLOCK))
        return CFALSE;

    if (CFALSE == NX_SDMMC_SelectCard(pSDXCBootStatus))
        return CFALSE;
#endif

	if (0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY)) {
		dprintf("FIFO Reset!!!\r\n");
		pSDXCReg->CTRL = NX_SDXC_CTRL_FIFORST;	// Reset the FIFO.
		// Wait until the FIFO reset is completed.
		while (pSDXCReg->CTRL & NX_SDXC_CTRL_FIFORST)
			;
	}

#ifdef OTA_AB_UPDATE
        int ab_select = 0;
        U32 tempBuf[BLOCK_LENGTH+4] = {0,};
        U32 boot_slot_ab_addr_offset = (U32)((U32)&(ptbh->tbbi.boot_slot_ab) - (U32)&(ptbh->tbbi));

	NX_SDMMC_Read1Sector_misc(pSDXCBootStatus,
                                  (MISC_SDMMC_DEVADDR + MISC_SDMMC_SLOT_OFFSET)/BLOCK_LENGTH,
                                  tempBuf);
        ab_select = ab_select_slot(tempBuf);

#if defined(NX_DEBUG)
        //Check misc READ data
        int iii = 0;
        printf("[bl1]==== misc 32byte read====\r\n");
        for(iii = 0; iii < 32; iii++) {
            if(iii % 8 == 0) {
                printf("\r\n");
                printf("0x%08x : ", &tempBuf[iii]);
            }
            printf("0x%08x ", tempBuf[iii]);
        }
        printf("\r\n");
#endif
        if (ab_select == OTA_AB_UPDATE_SUFFIX_A) {
            psbi->device_addr = BL1_SDMMCBOOT_BOOTLOADER_A;   // Sector 0x81
            dprintf("[bl1] ========== Slot selct A ==========\r\n");
        }
        else {  //OTA_AB_UPDATE_SUFFIX_B
            psbi->device_addr = BL1_SDMMCBOOT_BOOTLOADER_B;   // Sector 0x9608;
            dprintf("[bl1] ========== Slot selct B ==========\r\n");
        }
#endif //OTA_AB_UPDATE

#ifndef QUICKBOOT
	printf("Load from :0x%08X Sector\r\n",
			psbi->device_addr / BLOCK_LENGTH);
#endif
	result = NX_SDMMC_ReadSectors(pSDXCBootStatus,
			psbi->device_addr / BLOCK_LENGTH,
			2, (U32 *)ptbi);
	if(result == CFALSE) {
		printf("cannot read boot header! SDMMC boot failure\r\n");
		return result;
	}
#ifdef SECURE_ON
	if (pReg_ClkPwr->SYSRSTCONFIG & 1 << 14)
		Decrypt((U32 *)ptbi, (U32 *)ptbi, sizeof(struct nx_bootheader));
#endif
		if(ptbi->signature != HEADER_ID ) {
			dprintf("0x%08X\r\n3rd boot Sinature is wrong! SDMMC boot failure\r\n", ptbi->signature);
			return CFALSE;
		}

#if defined(SECURE_MODE)
	dprintf("SECURE - Load Addr :0x%08X,  Load Size :0x%08X,  Launch Addr :0x%08X\r\n",
			ptbi->load_addr, ptbi->load_size, ptbi->launch_addr);
#endif

#if !defined(SECURE_MODE)
#ifndef QUICKBOOT
	printf("NON_SECURE - Load Addr :0x%08X,  Load Size :0x%08X,  Launch Addr :0x%08X\r\n",
		ptbh->tbbi.load_addr, ptbh->tbbi.load_size, ptbh->tbbi.startaddr);
#endif

	U32 *src = (U32*)ptbi;
	U32 *dst = (U32*)ptbh->tbbi.load_addr;
	U32 *tb_load = dst;
	for (i = 0; i < sizeof(struct nx_bootheader) / 4; i++)
		*dst++ = *src++;

	dst = tb_load;
#endif

#ifdef OTA_AB_UPDATE
        //For BL2 notify selected slot A or B
        U32 *dst_ab = (U32*)ptbh->tbbi.load_addr;

        if (ab_select == OTA_AB_UPDATE_SUFFIX_A) {
                *(dst_ab + boot_slot_ab_addr_offset/4) = (U32)OTA_AB_UPDATE_BL2_MSG_A;
        }
        else {
                *(dst_ab + boot_slot_ab_addr_offset/4) = (U32)OTA_AB_UPDATE_BL2_MSG_B;
        }
#if defined(NX_DEBUG)
        printf("[bl1] slot marking for BL2 = 0x%x\r\n", *(dst_ab + boot_slot_ab_addr_offset/4));
#endif
#endif

#if defined(SECURE_MODE)
	result = NX_SDMMC_ReadSectors(pSDXCBootStatus,
			psbi->device_addr / BLOCK_LENGTH + 1,
			(ptbi->load_size + BLOCK_LENGTH - 1) / BLOCK_LENGTH,
			(U32 *)ptbi->load_addr);
#else
	result = NX_SDMMC_ReadSectors(pSDXCBootStatus,
			psbi->device_addr / BLOCK_LENGTH + 2,
			(ptbh->tbbi.load_size + (BLOCK_LENGTH - 1)) / BLOCK_LENGTH,
			(U32 *)(ptbh->tbbi.load_addr + BLOCK_LENGTH * 2));
	ptbi->launch_addr = ptbh->tbbi.startaddr;
#endif
#ifdef SECURE_ON
	if (pReg_ClkPwr->SYSRSTCONFIG & 1 << 14)
		Decrypt((U32 *)(ptbh->tbbi.load_addr + sizeof(struct nx_bootheader)),
			(U32 *)(ptbh->tbbi.load_addr + sizeof(struct nx_bootheader)),
			ptbh->tbbi.load_size);
#endif

	if (result == CFALSE) {
		printf("Image Read Failure\r\n");
	}

	return result;
}

/*
   sdmmc 0                   sdmmc 1                   sdmmc 2
   clk  a 29 1 gpio:0        clk  d 22 1 gpio:0        clk  c 18 2 gpio:1
   cmd  a 31 1 gpio:0        cmd  d 23 1 gpio:0        cmd  c 19 2 gpio:1
   dat0 b  1 1 gpio:0        dat0 d 24 1 gpio:0        dat0 c 20 2 gpio:1
   dat1 b  3 1 gpio:0        dat1 d 25 1 gpio:0        dat1 c 21 2 gpio:1
   dat2 b  5 1 gpio:0        dat2 d 26 1 gpio:0        dat2 c 22 2 gpio:1
   dat3 b  7 1 gpio:0        dat3 d 27 1 gpio:0        dat3 c 23 2 gpio:1
 */
void __init NX_SDPADSetALT(U32 PortNum)
{
	if(PortNum == 0)
	{
		register U32 *pGPIOARegA1 = (U32 *)&g_gpio_reg[GPIO_GROUP_A]->altfn[1];	// a 29, a 31
		register U32 *pGPIOBRegA0 = (U32 *)&g_gpio_reg[GPIO_GROUP_B]->altfn[0];	// b 1, 3, 5, 7
		*pGPIOARegA1 = (*pGPIOARegA1 & ~0xCC000000) | 0x44000000;	// all alt is 1
		*pGPIOBRegA0 = (*pGPIOBRegA0 & ~0x0000CCCC) | 0x00004444;	// all alt is 1
		g_gpio_reg[GPIO_GROUP_A]->slew                    &= ~(5<<29);
		g_gpio_reg[GPIO_GROUP_A]->slew_disalbe_default    |=  (5<<29);
		g_gpio_reg[GPIO_GROUP_A]->drv0                    |=  (5<<29);
		g_gpio_reg[GPIO_GROUP_A]->drv0_disable_default    |=  (5<<29);
		g_gpio_reg[GPIO_GROUP_A]->drv1                    |=  (5<<29);
		g_gpio_reg[GPIO_GROUP_A]->drv1_disable_default    |=  (5<<29);
		g_gpio_reg[GPIO_GROUP_A]->pullsel                 |=  (5<<29);
		g_gpio_reg[GPIO_GROUP_A]->pullsel_disable_default |=  (5<<29);
		//		g_gpio_reg[GPIO_GROUP_A]->pullenb                 |=  (5<<29);
		g_gpio_reg[GPIO_GROUP_A]->pullenb                 |=  (4<<29);         // clk is not pull-up.
		g_gpio_reg[GPIO_GROUP_A]->pullenb_disable_default |=  (5<<29);

		g_gpio_reg[GPIO_GROUP_B]->slew                    &= ~(0x55<<1);
		g_gpio_reg[GPIO_GROUP_B]->slew_disalbe_default    |=  (0x55<<1);
		g_gpio_reg[GPIO_GROUP_B]->drv0                    |=  (0x55<<1);
		g_gpio_reg[GPIO_GROUP_B]->drv0_disable_default    |=  (0x55<<1);
		g_gpio_reg[GPIO_GROUP_B]->drv1                    |=  (0x55<<1);
		g_gpio_reg[GPIO_GROUP_B]->drv1_disable_default    |=  (0x55<<1);
		g_gpio_reg[GPIO_GROUP_B]->pullsel                 |=  (0x55<<1);
		g_gpio_reg[GPIO_GROUP_B]->pullsel_disable_default |=  (0x55<<1);
		g_gpio_reg[GPIO_GROUP_B]->pullenb                 |=  (0x55<<1);
		g_gpio_reg[GPIO_GROUP_B]->pullenb_disable_default |=  (0x55<<1);
	}else
		if(PortNum == 1)
		{
			register U32 *pGPIODRegA1 = (U32 *)&g_gpio_reg[GPIO_GROUP_D]->altfn[1]; // d 22, 23, 24, 25, 26, 27
			*pGPIODRegA1 = (*pGPIODRegA1 & ~0x00FFF000) | 0x00555000;	// all alt is 1
			g_gpio_reg[GPIO_GROUP_D]->slew                    &= ~(0x3F<<22);
			g_gpio_reg[GPIO_GROUP_D]->slew_disalbe_default    |=  (0x3F<<22);
			g_gpio_reg[GPIO_GROUP_D]->drv0                    |=  (0x3F<<22);
			g_gpio_reg[GPIO_GROUP_D]->drv0_disable_default    |=  (0x3F<<22);
			g_gpio_reg[GPIO_GROUP_D]->drv1                    |=  (0x3F<<22);
			g_gpio_reg[GPIO_GROUP_D]->drv1_disable_default    |=  (0x3F<<22);
			g_gpio_reg[GPIO_GROUP_D]->pullsel                 |=  (0x3F<<22);
			g_gpio_reg[GPIO_GROUP_D]->pullsel_disable_default |=  (0x3F<<22);
			//		g_gpio_reg[GPIO_GROUP_D]->pullenb                 |=  (0x3F<<22);
			g_gpio_reg[GPIO_GROUP_D]->pullenb                 |=  (0x3E<<22);      // clk is not pull-up.
			g_gpio_reg[GPIO_GROUP_D]->pullenb_disable_default |=  (0x3F<<22);
		}
		else
		{
			register U32 *pGPIOCRegA1 = (U32 *)&g_gpio_reg[GPIO_GROUP_C]->altfn[1]; // c 18, 19, 20, 21, 22, 23
			*pGPIOCRegA1 = (*pGPIOCRegA1 & ~0x0000FFF0) | 0x0000AAA0;	// all alt is 2
			g_gpio_reg[GPIO_GROUP_C]->slew                    &= ~(0x3F<<18);
			g_gpio_reg[GPIO_GROUP_C]->slew_disalbe_default    |=  (0x3F<<18);
			g_gpio_reg[GPIO_GROUP_C]->drv0                    |=  (0x3F<<18);
			g_gpio_reg[GPIO_GROUP_C]->drv0_disable_default    |=  (0x3F<<18);
			g_gpio_reg[GPIO_GROUP_C]->drv1                    |=  (0x3F<<18);
			g_gpio_reg[GPIO_GROUP_C]->drv1_disable_default    |=  (0x3F<<18);
			g_gpio_reg[GPIO_GROUP_C]->pullsel                 |=  (0x3F<<18);
			g_gpio_reg[GPIO_GROUP_C]->pullsel_disable_default |=  (0x3F<<18);
			//		g_gpio_reg[GPIO_GROUP_C]->pullenb                 |=  (0x3F<<18);
			g_gpio_reg[GPIO_GROUP_C]->pullenb                 |=  (0x3E<<18);      // clk is not pull-up.
			g_gpio_reg[GPIO_GROUP_C]->pullenb_disable_default |=  (0x3F<<18);
		}
}

#if 0
void NX_SDPADSetGPIO(U32 PortNum)
{
	if(PortNum == 0)
	{
		register U32 *pGPIOARegA1 = (U32 *)&pReg_GPIO[GPIO_GROUP_A]->GPIOxALTFN[1];
		register U32 *pGPIOBRegA0 = (U32 *)&pReg_GPIO[GPIO_GROUP_B]->GPIOxALTFN[0];
		*pGPIOARegA1 &= ~0xCC000000;	// all gpio is 0
		*pGPIOBRegA0 &= ~0x0000CCCC;
		pReg_GPIO[GPIO_GROUP_A]->GPIOx_SLEW |= 5<<29;
		pReg_GPIO[GPIO_GROUP_A]->GPIOx_SLEW_DISABLE_DEFAULT |= 5<<29;
		pReg_GPIO[GPIO_GROUP_A]->GPIOx_DRV0 &= ~(5<<29);
		pReg_GPIO[GPIO_GROUP_A]->GPIOx_DRV0_DISABLE_DEFAULT |= 5<<29;
		pReg_GPIO[GPIO_GROUP_A]->GPIOx_DRV1 &= ~(5<<29);
		pReg_GPIO[GPIO_GROUP_A]->GPIOx_DRV1_DISABLE_DEFAULT |= 5<<29;
		pReg_GPIO[GPIO_GROUP_A]->GPIOx_PULLSEL &= ~(5<<29);
		pReg_GPIO[GPIO_GROUP_A]->GPIOx_PULLSEL_DISABLE_DEFAULT &= ~(5<<29);
		pReg_GPIO[GPIO_GROUP_A]->GPIOx_PULLENB &= ~(5<<29);
		pReg_GPIO[GPIO_GROUP_A]->GPIOx_PULLENB_DISABLE_DEFAULT &= ~(5<<29);

		pReg_GPIO[GPIO_GROUP_B]->GPIOx_SLEW |= 0x55<<1;
		pReg_GPIO[GPIO_GROUP_B]->GPIOx_SLEW_DISABLE_DEFAULT |= 0x55<<1;
		pReg_GPIO[GPIO_GROUP_B]->GPIOx_DRV0 &= ~(0x55<<1);
		pReg_GPIO[GPIO_GROUP_B]->GPIOx_DRV0_DISABLE_DEFAULT |= 0x55<<1;
		pReg_GPIO[GPIO_GROUP_B]->GPIOx_DRV1 &= ~(0x55<<1);
		pReg_GPIO[GPIO_GROUP_B]->GPIOx_DRV1_DISABLE_DEFAULT |= 0x55<<1;
		pReg_GPIO[GPIO_GROUP_B]->GPIOx_PULLSEL &= ~(0x55<<1);
		pReg_GPIO[GPIO_GROUP_B]->GPIOx_PULLSEL_DISABLE_DEFAULT &= ~(0x55<<1);
		pReg_GPIO[GPIO_GROUP_B]->GPIOx_PULLENB &= ~(0x55<<1);
		pReg_GPIO[GPIO_GROUP_B]->GPIOx_PULLENB_DISABLE_DEFAULT &= ~(0x55<<1);
	}else
		if(PortNum == 1)
		{
			register U32 *pGPIODRegA1 = (U32 *)&pReg_GPIO[GPIO_GROUP_D]->GPIOxALTFN[1];	// d 22, 23, 24, 25, 26, 27
			*pGPIODRegA1 = (*pGPIODRegA1 & ~0x00FFF000);	// all gpio is 0
			pReg_GPIO[GPIO_GROUP_D]->GPIOx_SLEW |= 0x3F<<22;
			pReg_GPIO[GPIO_GROUP_D]->GPIOx_SLEW_DISABLE_DEFAULT |= 0x3F<<22;
			pReg_GPIO[GPIO_GROUP_D]->GPIOx_DRV0 &= ~(0x3F<<22);
			pReg_GPIO[GPIO_GROUP_D]->GPIOx_DRV0_DISABLE_DEFAULT |= 0x3F<<22;
			pReg_GPIO[GPIO_GROUP_D]->GPIOx_DRV1 &= ~(0x3F<<22);
			pReg_GPIO[GPIO_GROUP_D]->GPIOx_DRV1_DISABLE_DEFAULT |= 0x3F<<22;
			pReg_GPIO[GPIO_GROUP_D]->GPIOx_PULLSEL &= ~(0x3F<<22);
			pReg_GPIO[GPIO_GROUP_D]->GPIOx_PULLSEL_DISABLE_DEFAULT &= ~(0x3F<<22);
			pReg_GPIO[GPIO_GROUP_D]->GPIOx_PULLENB &= ~(0x3F<<22);
			pReg_GPIO[GPIO_GROUP_D]->GPIOx_PULLENB_DISABLE_DEFAULT &= ~(0x3F<<22);
		}
		else
		{
			register U32 *pGPIOCRegA1 = (U32 *)&pReg_GPIO[GPIO_GROUP_C]->GPIOxALTFN[1];
			*pGPIOCRegA1 = (*pGPIOCRegA1 & ~0x0000FFF0) | 0x00005550;	// all gpio is 1
			pReg_GPIO[GPIO_GROUP_C]->GPIOx_SLEW |= 0x3F<<18;
			pReg_GPIO[GPIO_GROUP_C]->GPIOx_SLEW_DISABLE_DEFAULT |= 0x3F<<18;
			pReg_GPIO[GPIO_GROUP_C]->GPIOx_DRV0 &= ~(0x3F<<18);
			pReg_GPIO[GPIO_GROUP_C]->GPIOx_DRV0_DISABLE_DEFAULT |= 0x3F<<18;
			pReg_GPIO[GPIO_GROUP_C]->GPIOx_DRV1 &= ~(0x3F<<18);
			pReg_GPIO[GPIO_GROUP_C]->GPIOx_DRV1_DISABLE_DEFAULT |= 0x3F<<18;
			pReg_GPIO[GPIO_GROUP_C]->GPIOx_PULLSEL &= ~(0x3F<<18);
			pReg_GPIO[GPIO_GROUP_C]->GPIOx_PULLSEL_DISABLE_DEFAULT &= ~(0x3F<<18);
			pReg_GPIO[GPIO_GROUP_C]->GPIOx_PULLENB &= ~(0x3F<<18);
			pReg_GPIO[GPIO_GROUP_C]->GPIOx_PULLENB_DISABLE_DEFAULT &= ~(0x3F<<18);
		}
}
#endif

#ifdef CHIPID_NXP4330
/*
 * In fact, a function that reads the remaining data.
 */
static int __init sdmmc_read(SDXCBOOTSTATUS *pSDXCBootStatus, unsigned int devaddr,
	unsigned int load_addr, unsigned int load_size)
{
	register struct NX_SDMMC_RegisterSet * const pSDXCReg = pgSDXCReg[pSDXCBootStatus->SDPort];
	unsigned int ret = CFALSE;

	if (CTRUE == NX_SDMMC_Open(pSDXCBootStatus)) {
		if (0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY)) {
			pSDXCReg->CTRL = NX_SDXC_CTRL_FIFORST;			// Reset the FIFO.
			while(pSDXCReg->CTRL & NX_SDXC_CTRL_FIFORST);		// Wait until the FIFO reset is completed.
		}

		ret = NX_SDMMC_ReadSectors(pSDXCBootStatus, devaddr/BLOCK_LENGTH,
				(load_size + BLOCK_LENGTH - 1)/BLOCK_LENGTH, (U32*)load_addr);

		if(ret == CFALSE) {
//			printf("cannot read boot header! SDMMC boot failure\r\n");
			return -1;
		}
	} else {
//		printf("Cannot Detect SDMMC\r\n");
		return -1;
	}

	if (ret == CTRUE)
		ret = 0;
	else
		ret = -1;

	return ret;
}

/*
 * The function is added to load a 16K
 * or more data in BL1.
 */
unsigned int __init sdmmc_self_boot(void)
{
	unsigned int bl1_load_size = *((unsigned int*)(0xFFFF0000 + 0x44));
	struct sbi_header SBI;

	SDXCBOOTSTATUS lSDXCBootStatus;
	SDXCBOOTSTATUS *pSDXCBootStatus = &lSDXCBootStatus;

	CBOOL ret = 0;

	/* Fix to nxp4330 (PORT0) */
	pSDXCBootStatus->SDPort = 0;

	NX_SDPADSetALT(pSDXCBootStatus->SDPort);
	NX_SDMMC_Init(pSDXCBootStatus);

	/* Normal SD(eSD)/MMC ver 4.2 boot */
	SBI.load_addr
		= BL1_SDRAMBOOT_LOADADDR + BL1_SDMMCBOOT_LOADSIZE;
	SBI.dbi.sdmmcbi.portnumber = pSDXCBootStatus->SDPort;
	SBI.device_addr
		= BL1_SDMMCBOOT_DEVADDR + BL1_SDMMCBOOT_LOADSIZE;
	SBI.load_size
		= bl1_load_size - BL1_SDMMCBOOT_LOADSIZE;
	ret = sdmmc_read(pSDXCBootStatus,
		SBI.device_addr, SBI.load_addr, SBI.load_size);

#ifdef MMC_REINIT
	NX_SDMMC_Close(pSDXCBootStatus);
	NX_SDMMC_Terminate(pSDXCBootStatus);
#endif

	return ret;
}
#endif	// #ifdef CHIPID_NXP4330

int iSDXCBOOT(struct sbi_header *ptbi)
{
	CBOOL result = CFALSE;
	SDXCBOOTSTATUS lSDXCBootStatus;
	SDXCBOOTSTATUS *pSDXCBootStatus = &lSDXCBootStatus;

#if defined(CHIPID_NXP4330)
	psbi->dbi.sdmmcbi.portnumber = 0;
#endif

	//	psbi->dbi.sdmmcbi.portnumber = 1;
	//	psbi->device_addr = 128 * 1024;

	NX_ASSERT(psbi->dbi.sdmmcbi.portnumber < 3);
	pSDXCBootStatus->SDPort = psbi->dbi.sdmmcbi.portnumber;

	NX_SDPADSetALT(pSDXCBootStatus->SDPort);

#ifdef MMC_REINIT
	NX_SDMMC_Init(pSDXCBootStatus);
#endif

	//--------------------------------------------------------------------------
	// Normal SD(eSD)/MMC ver 4.2 boot
	result = SDMMCBOOT(pSDXCBootStatus, ptbi);

	NX_SDMMC_Close(pSDXCBootStatus);
	NX_SDMMC_Terminate(pSDXCBootStatus);

	//	NX_SDPADSetGPIO(pSDXCBootStatus->SDPort);

	return result;
}
