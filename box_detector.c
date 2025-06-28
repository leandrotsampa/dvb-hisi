/*
 * Copyright (C) Leandro Tavares de Melo <leandrotsampa@yahoo.com.br>, All rights reserved.
 */

#include <dvb_hisi.h>
#include <hi_drv_module.h>

enum BOX_TYPE {
/* Box not detected */
	UNKNOWN,
/* Atto */
	I_SMART,
	PIXEL,
	PIXEL_T,
	PIXEL_S,
	PIXEL_P,
/* 96Boards */
	POPLAR,
/* SmartSTB */
	U5_PVR,
/* Formuler */
	S_MINI
};

static struct t_box {
	bool active;

	enum BOX_TYPE m_Type;
	char *m_Brand;
	char *m_Chipset;
	char *m_Model;
	char *m_ModelFull;
	char *m_Support;
#if defined(HI_GPIOI2C_SUPPORT)
	HI_U32 NumI2C[4];
#endif
} BOX;

static void dvb_hisi_box_reset_gpio(HI_U32 u32GpioNo) {
	HI_DRV_GPIO_SetDirBit(u32GpioNo, HI_FALSE);
	HI_DRV_GPIO_WriteBit(u32GpioNo, HI_TRUE);

	msleep_interruptible(50);
	HI_DRV_GPIO_WriteBit(u32GpioNo, HI_FALSE);

	msleep_interruptible(50);
	HI_DRV_GPIO_WriteBit(u32GpioNo, HI_TRUE);
	msleep_interruptible(50); // Sleep before try read i2c
}

static GPIO_I2C_EXT_FUNC_S *i2cGpio = HI_NULL;

static bool dvb_hisi_box_check_i2c(HI_U32 I2cNum, HI_U8 I2cDevAddr, HI_U32 RegAddr, HI_U32 RegAddrCount, HI_U32 WriteToRegAddr) {
	int ret = -1;
	HI_U8 pData[1] = { 0x00 };

	if (HI_STD_I2C_NUM > I2cNum) {
		ret = HI_DRV_I2C_Read(I2cNum, I2cDevAddr, RegAddr, RegAddrCount, pData, 1);
	} else if (i2cGpio && i2cGpio->pfnGpioI2cReadExt) {
		ret = i2cGpio->pfnGpioI2cReadExt(I2cNum, I2cDevAddr, RegAddr, RegAddrCount, pData, 1);
	}

	if (ret == HI_SUCCESS) {
		if (WriteToRegAddr) {
			if (HI_STD_I2C_NUM > I2cNum)
				HI_DRV_I2C_Write(I2cNum, I2cDevAddr, WriteToRegAddr, RegAddrCount, pData, 1);
			else if (i2cGpio->pfnGpioI2cWriteExt)
				i2cGpio->pfnGpioI2cWriteExt(I2cNum, I2cDevAddr, WriteToRegAddr, RegAddrCount, pData, 1);
			msleep_interruptible(5);
		}

		return true;
	}

	return false;
}

bool dvb_hisi_box_set_tuner_attrib(t_hi_tuner *m_Tuner) {
	HI_UNF_TUNER_ATTR_S *m_Attributes = &m_Tuner->m_Attributes;

	if (!BOX.active)
		return false;

	switch (BOX.m_Type) {
		case I_SMART:
			m_Attributes->enSigType		 = HI_UNF_TUNER_SIG_TYPE_CAB;
			m_Attributes->enTunerDevType = HI_UNF_TUNER_DEV_TYPE_MXL254;
			m_Attributes->u32TunerAddr	 = 0xA0;
			m_Attributes->enDemodDevType = HI_UNF_DEMOD_DEV_TYPE_MXL254;
			m_Attributes->u32DemodAddr	 = 0xA0;
			m_Attributes->enOutputMode	 = HI_UNF_TUNER_OUTPUT_MODE_SERIAL;
			m_Attributes->enI2cChannel	 = 1;
			m_Attributes->u32ResetGpioNo = 0x6C;

			m_Tuner->m_Name		= kstrdup("Hisilicon MXL254", GFP_KERNEL);
			m_Tuner->m_TypeName	= kstrdup("DVB-C", GFP_KERNEL);
		break;
		case PIXEL:
		case PIXEL_T:
		case PIXEL_S:
			switch (m_Tuner->m_TunerID) {
				case 2:
					if (BOX.m_Type == PIXEL_T) { /* Type ISDB-T */
						m_Attributes->enSigType		 = HI_UNF_TUNER_SIG_TYPE_DVB_T;
						m_Attributes->enTunerDevType = HI_UNF_TUNER_DEV_TYPE_TDA18280;
						m_Attributes->u32TunerAddr	 = 0xB6;
#if defined(DEMOD_DEV_TYPE_FC8300)
						m_Attributes->enDemodDevType = HI_UNF_DEMOD_DEV_TYPE_FC8300;
#else
						m_Attributes->enDemodDevType = (HI_UNF_DEMOD_DEV_TYPE_MXL683 + 2); /* HI_UNF_DEMOD_DEV_TYPE_FC8300 */
#endif
						m_Attributes->u32DemodAddr	 = 0xB4;
						m_Attributes->enOutputMode	 = HI_UNF_TUNER_OUTPUT_MODE_SERIAL;
						m_Attributes->enI2cChannel	 = 2;
						m_Attributes->u32ResetGpioNo = 0x6C;

						m_Tuner->m_Name		= kstrdup("Hisilicon FC8300", GFP_KERNEL);
						m_Tuner->m_TypeName	= kstrdup("DVB-T", GFP_KERNEL);
					} else if (BOX.m_Type == PIXEL_S) { /* Type DVB-S */
						m_Attributes->enSigType		 = HI_UNF_TUNER_SIG_TYPE_SAT;
						m_Attributes->enTunerDevType = HI_UNF_TUNER_DEV_TYPE_TDA18280;
						m_Attributes->u32TunerAddr	 = 0xC6;
#if defined(DEMOD_DEV_TYPE_GX1132)
						m_Attributes->enDemodDevType = HI_UNF_DEMOD_DEV_TYPE_GX1132;
#else
						m_Attributes->enDemodDevType = (HI_UNF_DEMOD_DEV_TYPE_MXL683 + 1); /* HI_UNF_DEMOD_DEV_TYPE_GX1132 */
#endif
						m_Attributes->u32DemodAddr	 = 0xD0;
						m_Attributes->enOutputMode	 = HI_UNF_TUNER_OUTPUT_MODE_SERIAL;
						m_Attributes->enI2cChannel	 = 2;
						m_Attributes->u32ResetGpioNo = 0x6C;

						m_Tuner->m_Name		= kstrdup("Hisilicon GX1132", GFP_KERNEL);
						m_Tuner->m_TypeName	= kstrdup("DVB-S2", GFP_KERNEL);
					} else {
						return false;
					}
				break;
				default:
					m_Attributes->enSigType		 = HI_UNF_TUNER_SIG_TYPE_CAB;
					m_Attributes->enTunerDevType = HI_UNF_TUNER_DEV_TYPE_MXL214;
					m_Attributes->u32TunerAddr	 = 0xC0;
					m_Attributes->enDemodDevType = HI_UNF_DEMOD_DEV_TYPE_MXL214;
					m_Attributes->u32DemodAddr	 = 0xA0;
					m_Attributes->enOutputMode	 = HI_UNF_TUNER_OUTPUT_MODE_SERIAL;
					m_Attributes->enI2cChannel	 = 2;
					m_Attributes->u32ResetGpioNo = 0x43;

					m_Tuner->m_Name		= kstrdup("Hisilicon MXL214", GFP_KERNEL);
					m_Tuner->m_TypeName	= kstrdup("DVB-C", GFP_KERNEL);
				break;
			}
		break;
		case PIXEL_P:
			switch (m_Tuner->m_TunerID) {
				case 1:
				case 4:
					m_Attributes->enSigType		 = HI_UNF_TUNER_SIG_TYPE_SAT;
					m_Attributes->enTunerDevType = HI_UNF_TUNER_DEV_TYPE_TDA18280;
					m_Attributes->u32TunerAddr	 = 0xC6;
#if defined(DEMOD_DEV_TYPE_GX1132)
					m_Attributes->enDemodDevType = HI_UNF_DEMOD_DEV_TYPE_GX1132;
#else
					m_Attributes->enDemodDevType = (HI_UNF_DEMOD_DEV_TYPE_MXL683 + 1); /* HI_UNF_DEMOD_DEV_TYPE_GX1132 */
#endif
					m_Attributes->u32DemodAddr	 = (m_Tuner->m_TunerID == 1) ? 0xA0 : 0xB4;
					m_Attributes->enOutputMode	 = HI_UNF_TUNER_OUTPUT_MODE_SERIAL;
#if defined(HI_GPIOI2C_SUPPORT)
					m_Attributes->enI2cChannel	 = (m_Tuner->m_TunerID == 1) ? BOX.NumI2C[1] : BOX.NumI2C[2];
#endif
					m_Attributes->u32ResetGpioNo = (m_Tuner->m_TunerID == 1) ? 0x1B : 0x1C;

					m_Tuner->m_Name		= kstrdup("Hisilicon GX1132", GFP_KERNEL);
					m_Tuner->m_TypeName	= kstrdup("DVB-S2", GFP_KERNEL);
				break;
				default:
					m_Attributes->enSigType		 = HI_UNF_TUNER_SIG_TYPE_CAB;
					m_Attributes->enTunerDevType = HI_UNF_TUNER_DEV_TYPE_MXL214;
					m_Attributes->u32TunerAddr	 = 0xC0;
					m_Attributes->enDemodDevType = HI_UNF_DEMOD_DEV_TYPE_MXL214;
					m_Attributes->u32DemodAddr	 = 0xA0;
					m_Attributes->enOutputMode	 = HI_UNF_TUNER_OUTPUT_MODE_SERIAL_2BIT;
#if defined(HI_GPIOI2C_SUPPORT)
					m_Attributes->enI2cChannel	 = BOX.NumI2C[0];
#endif
					m_Attributes->u32ResetGpioNo = 0x30;

					m_Tuner->m_Name		= kstrdup("Hisilicon MXL214", GFP_KERNEL);
					m_Tuner->m_TypeName	= kstrdup("DVB-C", GFP_KERNEL);
				break;
			}
		break;
		case POPLAR:
			switch (m_Tuner->m_TunerID) {
				case 0:
					m_Attributes->enSigType		 = HI_UNF_TUNER_SIG_TYPE_DVB_T;
					m_Attributes->enTunerDevType = HI_UNF_TUNER_DEV_TYPE_MXL608;
					m_Attributes->u32TunerAddr	 = 0xC0;
					m_Attributes->enDemodDevType = HI_UNF_DEMOD_DEV_TYPE_3137;
					m_Attributes->u32DemodAddr	 = 0xB8;
					m_Attributes->enOutputMode	 = HI_UNF_TUNER_OUTPUT_MODE_SERIAL_2BIT;
					m_Attributes->enI2cChannel	 = 2;
					m_Attributes->u32ResetGpioNo = 0x4F;

					m_Tuner->m_Name		= kstrdup("Hisilicon MXL608", GFP_KERNEL);
					m_Tuner->m_TypeName	= kstrdup("DVB-T", GFP_KERNEL);
				break;
				default:
					return false;
				break;
			}
		break;
		case U5_PVR:
			switch (m_Tuner->m_TunerID) {
				case 0:
					m_Attributes->enSigType		 = HI_UNF_TUNER_SIG_TYPE_SAT;
					m_Attributes->enTunerDevType = HI_UNF_TUNER_DEV_TYPE_AV2011;
					m_Attributes->u32TunerAddr	 = 0xC6;
					m_Attributes->enDemodDevType = HI_UNF_DEMOD_DEV_TYPE_3136;
					m_Attributes->u32DemodAddr	 = 0xB4;
					m_Attributes->enOutputMode	 = HI_UNF_TUNER_OUTPUT_MODE_SERIAL;
					m_Attributes->enI2cChannel	 = 1;
					m_Attributes->u32ResetGpioNo = 0x43;

					m_Tuner->m_Name		= kstrdup("Hisilicon AV2011", GFP_KERNEL);
					m_Tuner->m_TypeName	= kstrdup("DVB-S2", GFP_KERNEL);
				break;
				case 1:
					m_Attributes->enSigType		 = HI_UNF_TUNER_SIG_TYPE_DVB_T;
					m_Attributes->enTunerDevType = HI_UNF_TUNER_DEV_TYPE_MXL608;
					m_Attributes->u32TunerAddr	 = 0xC0;
					m_Attributes->enDemodDevType = HI_UNF_DEMOD_DEV_TYPE_3137;
					m_Attributes->u32DemodAddr	 = 0x28;
					m_Attributes->enOutputMode	 = HI_UNF_TUNER_OUTPUT_MODE_SERIAL;
					m_Attributes->enI2cChannel	 = 2;
					m_Attributes->u32ResetGpioNo = 0x3C;

					m_Tuner->m_Name		= kstrdup("Hisilicon AVL68XX", GFP_KERNEL);
					m_Tuner->m_TypeName	= kstrdup("DVB-T", GFP_KERNEL);
				break;
				default:
					return false;
				break;
			}
		break;
		case S_MINI:
			switch (m_Tuner->m_TunerID) {
				case 0:
					m_Attributes->enSigType		 = HI_UNF_TUNER_SIG_TYPE_SAT;
					m_Attributes->enTunerDevType = 100; /* HI_UNF_TUNER_DEV_TYPE_RS6000 */
					m_Attributes->u32TunerAddr	 = 0x42;
					m_Attributes->enDemodDevType = 0x200; /* HI_UNF_DEMOD_DEV_TYPE_M88RS6000 */
					m_Attributes->u32DemodAddr	 = 0xD2;
					m_Attributes->enOutputMode	 = HI_UNF_TUNER_OUTPUT_MODE_SERIAL;
					m_Attributes->enI2cChannel	 = 1;
					m_Attributes->u32ResetGpioNo = 0x22;

					m_Tuner->m_Name		= kstrdup("Hisilicon RS6000", GFP_KERNEL);
					m_Tuner->m_TypeName	= kstrdup("DVB-S2", GFP_KERNEL);
				break;
				default:
					return false;
				break;
			}
		break;
		default:
			return false;
		break;
	}

	return true;
}

bool dvb_hisi_box_set_additional_attrib(t_hi_tuner *m_Tuner) {
	HI_UNF_TUNER_ATTR_S *m_Attributes = &m_Tuner->m_Attributes;

	switch (m_Attributes->enSigType) {
		case HI_UNF_TUNER_SIG_TYPE_CAB:
		case HI_UNF_TUNER_SIG_TYPE_J83B:
		{
			HI_UNF_TUNER_CAB_ATTR_S stCabTunerAttr;
			int (*HI_DRV_TUNER_SetCabAttr)(HI_U32 u32TunerPort, HI_UNF_TUNER_CAB_ATTR_S *pstCabTunerAttr);

			if (!(HI_DRV_TUNER_SetCabAttr = (void*)kallsyms_lookup_name("tuner_set_cab_attr"))) {
				dprintk("[ERROR] %s: Failed to load HI_DRV_TUNER_SetCabAttr function.\n", __FUNCTION__);
				return false;
			}

			stCabTunerAttr.u32DemodClk   = 24000;
			stCabTunerAttr.enTSSerialPIN = HI_UNF_TUNER_TS_SERIAL_PIN_0;

			if (HI_DRV_TUNER_SetCabAttr(m_Tuner->m_TunerID, &stCabTunerAttr) != HI_SUCCESS)
				dprintk("[ERROR] %s Tuner %d: HI_DRV_TUNER_SetCabAttr failed.\n", __FUNCTION__, m_Tuner->m_TunerID);
		}
		break;
		case HI_UNF_TUNER_SIG_TYPE_SAT:
		{
			HI_U8 LNBBuffer[1];
			HI_UNF_TUNER_SAT_ATTR_S stSatTunerAttrib;

			stSatTunerAttrib.u32DemodClk       = 24000;
			stSatTunerAttrib.u16TunerMaxLPF    = 40;
			stSatTunerAttrib.u16TunerI2CClk    = 400;
			stSatTunerAttrib.enRFAGC           = HI_UNF_TUNER_RFAGC_INVERT;
			stSatTunerAttrib.enIQSpectrum      = HI_UNF_TUNER_IQSPECTRUM_NORMAL;
			stSatTunerAttrib.enTSClkPolar      = HI_UNF_TUNER_TSCLK_POLAR_RISING;
			stSatTunerAttrib.enTSFormat        = HI_UNF_TUNER_TS_FORMAT_TS;
			stSatTunerAttrib.enTSSerialPIN     = HI_UNF_TUNER_TS_SERIAL_PIN_0;
			stSatTunerAttrib.enDiSEqCWave      = HI_UNF_TUNER_DISEQCWAVE_NORMAL;
			stSatTunerAttrib.enLNBCtrlDev      = HI_UNF_LNBCTRL_DEV_TYPE_MPS8125;
			stSatTunerAttrib.u16LNBDevAddress  = 0;

			if (HI_DRV_TUNER_SetSatAttr(m_Tuner->m_TunerID, &stSatTunerAttrib) != HI_SUCCESS)
				dprintk("[ERROR] %s Tuner %d: HI_DRV_TUNER_SetSatAttr failed.\n", __FUNCTION__, m_Tuner->m_TunerID);

			memset(LNBBuffer, 0, 1);
			if (HI_DRV_I2C_Write(2, 0x14, 0, 0, LNBBuffer, 1) != HI_SUCCESS)
				dprintk("[ERROR] %s Tuner %d: Failed to LNB Power On.\n", __FUNCTION__, m_Tuner->m_TunerID);

			if (BOX.m_Type == U5_PVR) {
				HI_UNF_TUNER_TSOUT_SET_S stTunerTSOut;

				stTunerTSOut.enTSOutput[0] = HI_UNF_TUNER_OUTPUT_TSDAT7;
				stTunerTSOut.enTSOutput[1] = HI_UNF_TUNER_OUTPUT_TSDAT1;
				stTunerTSOut.enTSOutput[2] = HI_UNF_TUNER_OUTPUT_TSDAT2;
				stTunerTSOut.enTSOutput[3] = HI_UNF_TUNER_OUTPUT_TSDAT3;
				stTunerTSOut.enTSOutput[4] = HI_UNF_TUNER_OUTPUT_TSDAT4;
				stTunerTSOut.enTSOutput[5] = HI_UNF_TUNER_OUTPUT_TSDAT5;
				stTunerTSOut.enTSOutput[6] = HI_UNF_TUNER_OUTPUT_TSDAT6;
				stTunerTSOut.enTSOutput[7] = HI_UNF_TUNER_OUTPUT_TSDAT0;
				stTunerTSOut.enTSOutput[8] = HI_UNF_TUNER_OUTPUT_TSSYNC;
				stTunerTSOut.enTSOutput[9] = HI_UNF_TUNER_OUTPUT_TSVLD;
				stTunerTSOut.enTSOutput[10] = HI_UNF_TUNER_OUTPUT_TSERR;

				if (HI_DRV_TUNER_SetTsOut(m_Tuner->m_TunerID, &stTunerTSOut)!= HI_SUCCESS)
					dprintk("[ERROR] %s Tuner %d: HI_DRV_TUNER_SetTsOut failed.\n", __FUNCTION__, m_Tuner->m_TunerID);
			}
		}
		break;
		case HI_UNF_TUNER_SIG_TYPE_DVB_T:
		case HI_UNF_TUNER_SIG_TYPE_DVB_T2:
		case HI_UNF_TUNER_SIG_TYPE_ISDB_T:
		case HI_UNF_TUNER_SIG_TYPE_ATSC_T:
		case HI_UNF_TUNER_SIG_TYPE_DTMB:
		{
			HI_UNF_TUNER_TER_ATTR_S stTunerTerAttr;
			int (*HI_DRV_TUNER_SetTerAttr)(HI_U32 u32TunerPort, HI_UNF_TUNER_TER_ATTR_S *pstTerTunerAttr);

			if (!(HI_DRV_TUNER_SetTerAttr = (void*)kallsyms_lookup_name("tuner_set_ter_attr"))) {
				dprintk("[ERROR] %s: Failed to load HI_DRV_TUNER_SetTerAttr function.\n", __FUNCTION__);
				return false;
			}

			stTunerTerAttr.u32DemodClk		= 24000;/**<Demod reference clock frequency, KHz*/
			stTunerTerAttr.u32ResetGpioNo	= m_Attributes->u32ResetGpioNo;	/**< Demod reset GPIO NO. */
			stTunerTerAttr.u16TunerMaxLPF	= 34;	/**<Tuner max LPF, MHz*/
			stTunerTerAttr.u16TunerI2CClk	= 400;	/**<Tuner I2C clock, kHz*/

			stTunerTerAttr.enRFAGC			= HI_UNF_TUNER_RFAGC_NORMAL;		// HI_UNF_TUNER_RFAGC_MODE_E      ; /**<Tuner RF AGC mode*/
			stTunerTerAttr.enIQSpectrum		= HI_UNF_TUNER_IQSPECTRUM_NORMAL; 	// HI_UNF_TUNER_IQSPECTRUM_MODE_E ; /**<Tuner IQ spectrum mode*/
			stTunerTerAttr.enTSClkPolar		= HI_UNF_TUNER_TSCLK_POLAR_RISING;	// HI_UNF_TUNER_TSCLK_POLAR_E     ; /**<TS clock polarization*/
			stTunerTerAttr.enTSFormat		= HI_UNF_TUNER_TS_FORMAT_TS;		// HI_UNF_TUNER_TS_FORMAT_E       ; /**<TS format*/
			stTunerTerAttr.enTSSerialPIN	= HI_UNF_TUNER_TS_SERIAL_PIN_0;		// HI_UNF_TUNER_TS_SERIAL_PIN_E   ; /**<TS serial PIN*/
			stTunerTerAttr.enTSSyncHead		= HI_UNF_TUNER_TS_SYNC_HEAD_AUTO;	// HI_UNF_TUNER_TS_SYNC_HEAD_E    ; /**<TS sync head length*/

			if (HI_DRV_TUNER_SetTerAttr(m_Tuner->m_TunerID, &stTunerTerAttr) != HI_SUCCESS)
				dprintk("[ERROR] %s Tuner %d: HI_DRV_TUNER_SetTerAttr failed.\n", __FUNCTION__, m_Tuner->m_TunerID);
		}
		break;
		default:
		break;
	}

	return true;
}

bool dvb_hisi_box_set_port_attrib(t_hi_tuner *m_Tuner) {
	HI_UNF_DMX_PORT_ATTR_S *m_PortAttrib = &m_Tuner->m_PortAttrib;

	if (!BOX.active)
		return false;

	m_PortAttrib->enPortMod            = HI_UNF_DMX_PORT_MODE_EXTERNAL;
	m_PortAttrib->enPortType           = HI_UNF_DMX_PORT_TYPE_SERIAL;
	m_PortAttrib->u32SyncLostTh        = 1;
	m_PortAttrib->u32SyncLockTh        = 5;
	m_PortAttrib->u32TunerInClk        = 0;
	m_PortAttrib->u32SerialBitSelector = 1;
	m_PortAttrib->u32TunerErrMod       = 0;
	m_PortAttrib->u32UserDefLen1       = 0;
	m_PortAttrib->u32UserDefLen2       = 0;

	switch (BOX.m_Type) {
		case POPLAR:
		case I_SMART:
			m_PortAttrib->u32SerialBitSelector = 0;
		break;
		default:
		break;
	}

	return true;
}

static void dvb_hisi_box_set_info(void) {
	/** Set Brand **/
	switch (BOX.m_Type) {
		case I_SMART:
		case PIXEL:
		case PIXEL_T:
		case PIXEL_S:
		case PIXEL_P:
			BOX.m_Brand = "Atto";
		break;
		case POPLAR:
			BOX.m_Brand = "96Boards";
		break;
		case U5_PVR:
			BOX.m_Brand = "SmartSTB";
		break;
		case S_MINI:
			BOX.m_Brand = "Formuler";
		break;
		default:
		break;
	}

	/** Set Chipset **/
	switch (BOX.m_Type) {
		case I_SMART:
			BOX.m_Chipset = "hi3716cv200";
		break;
		case PIXEL:
		case PIXEL_T:
		case PIXEL_S:
		case POPLAR:
			BOX.m_Chipset = "hi3798cv200";
		break;
		case U5_PVR:
		case S_MINI:
		case PIXEL_P:
			BOX.m_Chipset = "hi3798mv200";
		break;
		default:
			BOX.m_Chipset = HI_CHIP_TYPE;
		break;
	}

	/** Set Model **/
	switch (BOX.m_Type) {
		case I_SMART:
			BOX.m_Model = "ismart";
			BOX.m_ModelFull = "i-Smart";
		break;
		case PIXEL:
		case PIXEL_T:
		case PIXEL_S:
			BOX.m_Model = "pixel";
			BOX.m_ModelFull = "Pixel";
		break;
		case PIXEL_P:
			BOX.m_Model = "ppremium";
			BOX.m_ModelFull = "Pixel Premium";
		break;
		case POPLAR:
			BOX.m_Model = "poplar";
			BOX.m_ModelFull = "Poplar";
		break;
		case U5_PVR:
			BOX.m_Model = "u5pvr";
			BOX.m_ModelFull = "U5-PVR";
		break;
		case S_MINI:
			BOX.m_Model = "smini";
			BOX.m_ModelFull = "S-Mini";
		break;
		default:
		break;
	}

	/** Set Support **/
	switch (BOX.m_Type) {
		case I_SMART:
			BOX.m_Support = "DVB-C Quad";
		break;
		case PIXEL:
			BOX.m_Support = "DVB-C Triple";
		break;
		case PIXEL_T:
			BOX.m_Support = "DVB-C Triple + ISDB-T";
		break;
		case PIXEL_S:
			BOX.m_Support = "DVB-C Triple + DVB-S";
		break;
		case PIXEL_P:
			BOX.m_Support = "DVB-C Triple + DVB-S Duo";
		break;
		case POPLAR:
			BOX.m_Support = "DVB-T";
		break;
		case U5_PVR:
			BOX.m_Support = "DVB-S + DVB-T";
		break;
		case S_MINI:
			BOX.m_Support = "DVB-S";
		break;
		default:
		break;
	}
}

/*
 * This function use i2c value of tuner demod to detect box type.
 */
bool dvb_hisi_box_load(int box_type) {
	bool ret = false;

	if (BOX.active)
		return true;
	else
		HI_DRV_MODULE_GetFunction(HI_ID_GPIO_I2C, (HI_VOID**)&i2cGpio);

	if (box_type > UNKNOWN && box_type <= S_MINI) {
		ret = true;
		BOX.active = true;
		BOX.m_Type = box_type;
#ifdef GLOBAL_DEBUG
		dprintk("[INFO] %s: Set mode manual.\n", __FUNCTION__);
#endif
		goto RETURN;
	}

	BOX.active    = true;
	BOX.m_Type    = UNKNOWN;
	BOX.m_Model   = "Unknown";
	BOX.m_Chipset = "Unknown";
	BOX.m_Brand   = "Unknown";

	/* Atto Pixel (4 - Tuners (3 - DVB-C and 1 - ISDB-T or DVB-S2)) */
	dvb_hisi_box_reset_gpio(0x43);
	/* Check tuner 0, 1 and 3 for DVB-C */
	if (dvb_hisi_box_check_i2c(2, 0xA0, -266228, 4, 0)) {
		BOX.m_Type = PIXEL;

		if (dvb_hisi_box_check_i2c(2, 0xB4, 38, 2, 189)) { /* Check tuner 2 for ISDB-T */
			BOX.m_Type = PIXEL_T;
		} else if (dvb_hisi_box_check_i2c(2, 0xD0, 0, 1, 0) ||
		         dvb_hisi_box_check_i2c(2, 0xD0, 1, 1, 0) ||
		         dvb_hisi_box_check_i2c(2, 0xD0, 2, 1, 0)) { /* Check tuner 2 for DVB-S2 */
			BOX.m_Type = PIXEL_S;
		}

		ret = true;
		goto RETURN;
	}

	/* Atto i-Smart (4 - Tuners DVB-C) */
	dvb_hisi_box_reset_gpio(0x63);
	if (dvb_hisi_box_check_i2c(1, 0xA0, 38, 2, 0)) {
		BOX.m_Type = I_SMART;
		ret = true;
		goto RETURN;
	}

	/* Poplar (1 - Tuners DVB-T2) */
	if (dvb_hisi_box_check_i2c(2, 0xB8, 38, 2, 0)) {
		BOX.m_Type = POPLAR;
		ret = true;
		goto RETURN;
	}

	/* U5-PVR (2 - Tuners (1 - DVB-S2 and 1 - DVB-T2)) */
	/* Check tuner 0 for DVB-S2 */
	if (dvb_hisi_box_check_i2c(2, 0x28, 38, 2, 0)) {
		BOX.m_Type = U5_PVR;
		ret = true;
		goto RETURN;
	}

	/* Formuler S-Mini (1 - Tuners  DVB-S2) */
	if (dvb_hisi_box_check_i2c(1, 0xD2, 38, 2, 0)) {
		BOX.m_Type = S_MINI;
		ret = true;
		goto RETURN;
	}

	/* Atto Pixel Premium (5 - Tuners (3 - DVB-C and 2 - DVB-S2)) */
#if defined(HI_GPIOI2C_SUPPORT)
	dprintk("[INFO] %s: Atto Pixel Premium start detection ...\n", __FUNCTION__);
	if (HI_DRV_GPIOI2C_CreateGpioI2c(&BOX.NumI2C[0], 52, 51) == HI_SUCCESS) {
		if (HI_DRV_GPIOI2C_CreateGpioI2c(&BOX.NumI2C[1], 19, 29) == HI_SUCCESS) {
			if (HI_DRV_GPIOI2C_CreateGpioI2c(&BOX.NumI2C[2], 26, 25) == HI_SUCCESS) {
				if (HI_DRV_GPIOI2C_CreateGpioI2c(&BOX.NumI2C[3], 2, 0) == HI_SUCCESS) {
					HI_U8 pData[1] = { 0x20 };

					HI_DRV_GPIO_SetDirBit(34, HI_FALSE);
					HI_DRV_GPIO_SetDirBit(27, HI_FALSE);
					HI_DRV_GPIO_SetDirBit(28, HI_FALSE);
					HI_DRV_GPIO_WriteBit(34, HI_TRUE);
					HI_DRV_GPIO_WriteBit(27, HI_TRUE);
					HI_DRV_GPIO_WriteBit(28, HI_TRUE);
					msleep_interruptible(50);
					HI_DRV_GPIO_WriteBit(34, HI_FALSE);
					HI_DRV_GPIO_WriteBit(27, HI_FALSE);
					HI_DRV_GPIO_WriteBit(28, HI_FALSE);
					msleep_interruptible(50);
					HI_DRV_GPIO_WriteBit(34, HI_TRUE);
					HI_DRV_GPIO_WriteBit(27, HI_TRUE);
					HI_DRV_GPIO_WriteBit(28, HI_TRUE);

					/* It's Only for DVB-S2 Tuner ??? */
					if (HI_DRV_I2C_Write(0, 0x10, 0, 1, pData, 1) != HI_SUCCESS)
						dprintk("[ERROR] %s: Failed to Write Value to Tuner DVB-S2.\n", __FUNCTION__);

					if (i2cGpio && i2cGpio->pfnGpioI2cReadExt && i2cGpio->pfnGpioI2cWriteExt) {
						memset(pData, 0, 1);
						i2cGpio->pfnGpioI2cWriteExt(BOX.NumI2C[1], 0x10, 0, 0, pData, 1);
						i2cGpio->pfnGpioI2cReadExt(BOX.NumI2C[1], 0x10, 0, 0, pData, 1);
						memset(pData, 0, 1);
						i2cGpio->pfnGpioI2cWriteExt(BOX.NumI2C[2], 0x10, 0, 0, pData, 1);
						i2cGpio->pfnGpioI2cReadExt(BOX.NumI2C[2], 0x10, 0, 0, pData, 1);
					}

					//if (dvb_hisi_box_check_i2c(BOX.NumI2C[3], 0x28, 0, 2, 1)) {
					dprintk("[INFO] %s: Atto Pixel Premium finished detection ...\n", __FUNCTION__);
					BOX.m_Type = PIXEL_P;
					ret = true;
					goto RETURN;
					//}
				}
			}
		}
	}
#endif

RETURN:
	dvb_hisi_box_set_info();
#ifdef ENIGMA2
	if (!enigma2_set_proc_value("stb/info/model", BOX.m_Model))
		dprintk("[ERROR] Failed to set info model.\n");
	if (!enigma2_set_proc_value("stb/info/chipset", BOX.m_Chipset))
		dprintk("[ERROR] Failed to set info chipset.\n");
#endif

	if (ret)
		dprintk("[INFO] BOX: %s %s (%s)\n", BOX.m_Brand, BOX.m_ModelFull, BOX.m_Support);
	else
		dprintk("[INFO] BOX: %s\n", BOX.m_Brand);

	return ret;
}

void dvb_hisi_box_unload() {
	if (BOX.active)
		BOX.active = false;
}