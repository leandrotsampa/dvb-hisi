/*
 * Copyright (C) Leandro Tavares de Melo <leandrotsampa@yahoo.com.br>, All rights reserved.
 */

#include <dvb_hisi.h>

#if (DVB_API_VERSION << 8 | DVB_API_VERSION_MINOR) < 0x0503
#error ========================================================================
#error Version 5.3 or newer of DVB API is required (see at linux/dvb/version.h)
#error You can find it in kernel version >= 3.0
#error ========================================================================
#endif

struct dvb_hisi_frontend_state {
	struct dvb_frontend m_Frontend;
	struct dtv_frontend_properties m_Properties;
	t_hi_tuner *m_Tuner;
};

static int dvb_hisi_frontend_connect(unsigned int m_TunerID, const HI_UNF_TUNER_CONNECT_PARA_S *pstConnectPara, enum fe_sec_tone_mode m_Tone, unsigned int u32TimeOut) {
	int (*HI_DRV_TUNER_Connect)(TUNER_SIGNAL_S *pstPara);
	int (*HI_DRV_TUNER_Connect_Timeout)(TUNER_DATABUF_S *pstPara);
	int (*HI_DRV_TUNER_Continuous22K)(TUNER_DATA_S *pstPara);
	HI_UNF_TUNER_STATUS_S stTunerStatus;
	unsigned int u32TimeSpan = 0;
	TUNER_SIGNAL_S stTunerSignal = {0};
	TUNER_ACC_QAM_PARAMS_S stSignal = {0};

	if (!(HI_DRV_TUNER_Connect = (void*)kallsyms_lookup_name("tuner_osr_connect"))) {
		dprintk("[ERROR] %s: Failed to load HI_DRV_TUNER_Connect function.\n", __FUNCTION__);
		return HI_FAILURE;
	}

	stTunerSignal.enSigType	= pstConnectPara->enSigType;
	switch (pstConnectPara->enSigType) {
		case HI_UNF_TUNER_SIG_TYPE_CAB:
		case HI_UNF_TUNER_SIG_TYPE_J83B:
		{
			stSignal.u32Frequency = pstConnectPara->unConnectPara.stCab.u32Freq;
			stSignal.unSRBW.u32SymbolRate = pstConnectPara->unConnectPara.stCab.u32SymbolRate;
			stSignal.bSI = pstConnectPara->unConnectPara.stCab.bReverse;

			switch(pstConnectPara->unConnectPara.stCab.enModType) {
				case HI_UNF_MOD_TYPE_QAM_16:
					stSignal.enQamType = QAM_TYPE_16;
				break;
				case HI_UNF_MOD_TYPE_QAM_32:
					stSignal.enQamType = QAM_TYPE_32;
				break;
				case HI_UNF_MOD_TYPE_QAM_64:
				case HI_UNF_MOD_TYPE_DEFAULT:
					stSignal.enQamType = QAM_TYPE_64;
				break;
				case HI_UNF_MOD_TYPE_QAM_128:
					stSignal.enQamType = QAM_TYPE_128;
				break;
				case HI_UNF_MOD_TYPE_QAM_256:
				default:
					stSignal.enQamType = QAM_TYPE_256;
				break;
			}

			/* Set tuner connect timeout */
			if ((HI_DRV_TUNER_Connect_Timeout = (void*)kallsyms_lookup_name("tuner_osr_connect_timeout"))) {
				TUNER_DATABUF_S stConnectTimeout = {0};
				stConnectTimeout.u32DataBuf[0] = u32TimeOut;
				stConnectTimeout.u32Port = m_TunerID;

				if (HI_DRV_TUNER_Connect_Timeout(&stConnectTimeout) != HI_SUCCESS) {
					dprintk("[ERROR] %s Tuner %d: HI_DRV_TUNER_Connect_Timeout failed.\n", __FUNCTION__, m_TunerID);
					return HI_FAILURE;
				}
			}
		}
		break;
		case HI_UNF_TUNER_SIG_TYPE_SAT:
		{
			int LnbPower = 0;
			HI_U8 LNBBuffer[1];
			TUNER_DATA_S stTuner22K = {0};

			stSignal.u32Frequency = pstConnectPara->unConnectPara.stSat.u32Freq;
			stSignal.unSRBW.u32SymbolRate = pstConnectPara->unConnectPara.stSat.u32SymbolRate;
			stSignal.enPolar = pstConnectPara->unConnectPara.stSat.enPolar;

			if (stSignal.enPolar == HI_UNF_TUNER_FE_POLARIZATION_V) {
				LnbPower = 13;
				memset(&LNBBuffer, 0x14, sizeof(HI_U8) * 1);
			} else {
				LnbPower = 18;
				memset(&LNBBuffer, 0x1C, sizeof(HI_U8) * 1);
			}

			if (HI_DRV_I2C_Write(2, 0x14, 0, 0, (void *)&LNBBuffer, 1) != HI_SUCCESS)
				dprintk("[ERROR] %s Tuner %d: Failed to set LNB Data 0x%x.\n", __FUNCTION__, m_TunerID, LNBBuffer[0]);

			if (HI_DRV_TUNER_SetLnbOut(m_TunerID, LnbPower) != HI_SUCCESS)
				dprintk("[ERROR] %s Tuner %d: HI_DRV_TUNER_SetLnbOut failed.\n", __FUNCTION__, m_TunerID);

			if (!(HI_DRV_TUNER_Continuous22K = (void*)kallsyms_lookup_name("tuner_send_continuous_22K"))) {
				dprintk("[ERROR] %s: Failed to load HI_DRV_TUNER_Continuous22K function.\n", __FUNCTION__);
				return HI_FAILURE;
			}

			stTuner22K.u32Port = m_TunerID;
			stTuner22K.u32Data = (m_Tone == SEC_TONE_ON) ? 1 : 0;
			if (HI_DRV_TUNER_Continuous22K(&stTuner22K) != HI_SUCCESS)
				dprintk("[ERROR] %s Tuner %d: HI_DRV_TUNER_Continuous22K failed.\n", __FUNCTION__, m_TunerID);
		}
		break;
		case HI_UNF_TUNER_SIG_TYPE_DVB_T:
		case HI_UNF_TUNER_SIG_TYPE_DVB_T2:
			stSignal.u32Frequency                = pstConnectPara->unConnectPara.stTer.u32Freq;
			stSignal.unSRBW.u32BandWidth         = pstConnectPara->unConnectPara.stTer.u32BandWidth;
			stSignal.bSI                         = pstConnectPara->unConnectPara.stTer.bReverse;
			stSignal.unTer.enDVBT2.enChannelAttr = pstConnectPara->unConnectPara.stTer.enChannelMode;
			stSignal.unTer.enDVBT                = pstConnectPara->unConnectPara.stTer.enDVBTPrio;
			stSignal.u8DVBTMode                  = (pstConnectPara->enSigType == HI_UNF_TUNER_SIG_TYPE_DVB_T) ? 1 : 0;

			switch(pstConnectPara->unConnectPara.stTer.enModType) {
				case HI_UNF_MOD_TYPE_QAM_16:
					stSignal.enQamType = QAM_TYPE_16;
					break;
				case HI_UNF_MOD_TYPE_QAM_32:
					stSignal.enQamType = QAM_TYPE_32;
					break;
				case HI_UNF_MOD_TYPE_QAM_64:
				case HI_UNF_MOD_TYPE_DEFAULT:
					stSignal.enQamType = QAM_TYPE_64;
					break;
				case HI_UNF_MOD_TYPE_QAM_128:
					stSignal.enQamType = QAM_TYPE_128;
					break;
				case HI_UNF_MOD_TYPE_QAM_256:
				default:
					stSignal.enQamType = QAM_TYPE_256;
				break;
			}
		break;
		default:
			return HI_FAILURE;
		break;
	}

	/* call tuner connect operation */
	stTunerSignal.u32Port	= m_TunerID;
	stTunerSignal.stSignal	= stSignal;

	if (HI_DRV_TUNER_Connect(&stTunerSignal) != HI_SUCCESS) {
		dprintk("[ERROR] %s Tuner %d: HI_DRV_TUNER_Connect failed.\n", __FUNCTION__, m_TunerID);
		return HI_FAILURE;
	}

	if (u32TimeOut == 0)
		return HI_SUCCESS;

	while (u32TimeSpan < u32TimeOut) {
		/* get tuner lock status */
		if (HI_DRV_TUNER_GetStatus(m_TunerID, &stTunerStatus) != HI_SUCCESS) {
			dprintk("[ERROR] %s Tuner %d: HI_DRV_TUNER_GetStatus failed.\n", __FUNCTION__, m_TunerID);
			return HI_FAILURE;
		}

		if (stTunerStatus.enLockStatus == HI_UNF_TUNER_SIGNAL_LOCKED) {
			return HI_SUCCESS;
		} else {
			mdelay(10);
			u32TimeSpan += 10;
		}
	}

	return HI_ERR_TUNER_FAILED_CONNECT;
}

static int dvb_hisi_frontend_read_status(struct dvb_frontend *fe, enum fe_status *status) {
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;
	HI_UNF_TUNER_STATUS_S stTunerStatus;

	*status = FE_HAS_SIGNAL;
	if (HI_DRV_TUNER_GetStatus(state->m_Tuner->m_TunerID, &stTunerStatus) == HI_SUCCESS)
		if (stTunerStatus.enLockStatus == HI_UNF_TUNER_SIGNAL_LOCKED)
			*status = 0x1f;

	return 0;
}

static int dvb_hisi_frontend_read_ber(struct dvb_frontend *fe, u32 *ber) {
	HI_U32 BitErrorRate[3] = {0, 0, 0};
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;

	*ber = 0;
	if (HI_DRV_TUNER_GetBer(state->m_Tuner->m_TunerID, BitErrorRate) == HI_SUCCESS)
		*ber = ((BitErrorRate[0] & 0xffff) << 16) | (BitErrorRate[2] & 0xffff);

	return 0;
}

static int dvb_hisi_frontend_read_signal_strength(struct dvb_frontend *fe, u16 *strength) {
	HI_U32 SignalStrength[3] = {0, 0, 0};
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;

	*strength = 0;
	if (HI_DRV_TUNER_GetRssi(state->m_Tuner->m_TunerID, SignalStrength) == HI_SUCCESS) {
		HI_U32 s32Agc = 0;
		switch (state->m_Tuner->m_Attributes.enTunerDevType) {
			case HI_UNF_TUNER_DEV_TYPE_AV2011:
            case HI_UNF_TUNER_DEV_TYPE_AV2018:
                if(SignalStrength[1] < 1000) // >-10dbm
                    s32Agc = 97;
                else if(SignalStrength[1] < 1600) // -40dbm ~ -10dbm
                    s32Agc = (HI_U32)(67 * (SignalStrength[1] - 1600));
                else if(SignalStrength[1] < 2200)
                    s32Agc = (HI_U32)(47 * (SignalStrength[1] - 2200));
                else if(SignalStrength[1] < 3000)
                    s32Agc = (HI_U32)(27 * (SignalStrength[1] - 3000));
                else
                    s32Agc = (HI_U32)(17 * (SignalStrength[1] - 4095)); // <-80dbm
			break;
			case HI_UNF_TUNER_DEV_TYPE_MXL214:
			case HI_UNF_TUNER_DEV_TYPE_MXL254:
			case HI_UNF_TUNER_DEV_TYPE_MXL608:
			case HI_UNF_TUNER_DEV_TYPE_TDA18280:
			default:
				s32Agc = SignalStrength[1];
			break;
		}

		*strength = s32Agc * 0x28F;
	}

	return 0;
}

static int dvb_hisi_frontend_read_snr(struct dvb_frontend *fe, u16 *snr) {
	HI_U32 SNR = 0;
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;

	*snr = 0;
	if (HI_DRV_TUNER_GetSnr(state->m_Tuner->m_TunerID, &SNR) == HI_SUCCESS)
		*snr = (SNR > 100) ? SNR * 100 : SNR * 1000;

	return 0;
}

static int dvb_hisi_frontend_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks) {
	*ucblocks = 0;
	return 0;
}

#if (DVB_API_VERSION << 8 | DVB_API_VERSION_MINOR) > 0x0504
static int dvb_hisi_frontend_get_frontend(struct dvb_frontend *fe) {
#else
static int dvb_hisi_frontend_get_frontend(struct dvb_frontend *fe, struct dvb_frontend_parameters* params) {
#endif
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	switch (state->m_Tuner->m_Attributes.enSigType) {
		case HI_UNF_TUNER_SIG_TYPE_CAB:
		case HI_UNF_TUNER_SIG_TYPE_J83B:
			c->symbol_rate = state->m_Properties.symbol_rate;
			c->fec_inner = state->m_Properties.fec_inner;
			c->modulation = state->m_Properties.modulation;
		break;
		case HI_UNF_TUNER_SIG_TYPE_SAT:
			c->symbol_rate = state->m_Properties.symbol_rate;
			c->fec_inner = state->m_Properties.fec_inner;
		break;
		case HI_UNF_TUNER_SIG_TYPE_DVB_T:
		case HI_UNF_TUNER_SIG_TYPE_DVB_T2:
		case HI_UNF_TUNER_SIG_TYPE_ISDB_T:
		case HI_UNF_TUNER_SIG_TYPE_ATSC_T:
		case HI_UNF_TUNER_SIG_TYPE_DTMB:
			c->bandwidth_hz = state->m_Properties.bandwidth_hz;
			c->code_rate_HP = state->m_Properties.code_rate_HP;
			c->code_rate_LP = state->m_Properties.code_rate_LP;
			c->modulation = state->m_Properties.modulation;
			c->transmission_mode = state->m_Properties.transmission_mode;
			c->guard_interval = state->m_Properties.guard_interval;
			c->hierarchy = state->m_Properties.hierarchy;
		break;
		default:
			dprintk("[ERROR] Unregognized type %d for tuner %d.\n", state->m_Tuner->m_Attributes.enSigType, state->m_Tuner->m_TunerID);
			return -EINVAL;
		break;
	}

	c->frequency = state->m_Properties.frequency;
	c->inversion = state->m_Properties.inversion;
	return 0;
}

#if (DVB_API_VERSION << 8 | DVB_API_VERSION_MINOR) > 0x0504
static int dvb_hisi_frontend_set_frontend(struct dvb_frontend *fe) {
#else
static int dvb_hisi_frontend_set_frontend(struct dvb_frontend *fe, struct dvb_frontend_parameters* params) {
#endif
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;
	HI_UNF_MODULATION_TYPE_E m_Modulation = HI_UNF_MOD_TYPE_DEFAULT;
	HI_UNF_TUNER_CONNECT_PARA_S stConnectPara;
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	state->m_Properties.frequency = c->frequency;
	state->m_Properties.inversion = c->inversion;

	switch (state->m_Tuner->m_Attributes.enSigType) {
		case HI_UNF_TUNER_SIG_TYPE_CAB:
		case HI_UNF_TUNER_SIG_TYPE_J83B:
			state->m_Properties.symbol_rate = c->symbol_rate;
			state->m_Properties.fec_inner = c->fec_inner;
			state->m_Properties.modulation = c->modulation;
		break;
		case HI_UNF_TUNER_SIG_TYPE_SAT: // HACK: Need fix.
			state->m_Properties.symbol_rate = c->symbol_rate;
			state->m_Properties.fec_inner = c->fec_inner;

			/* Only if DVB-S2 */
			if (c->delivery_system == SYS_DVBS2) {
				/* DELIVERY SYSTEM: S2 delsys in use */
				state->m_Properties.fec_inner = 9;

				/* MODULATION */
				if (c->modulation == PSK_8)
					/* signal PSK_8 modulation used */
					state->m_Properties.fec_inner += 9;

				/* FEC */
				switch (c->fec_inner) {
					case FEC_1_2:
						state->m_Properties.fec_inner += 1;
					break;
					case FEC_2_3:
						state->m_Properties.fec_inner += 2;
					break;
					case FEC_3_4:
						state->m_Properties.fec_inner += 3;
					break;
					case FEC_4_5:
						state->m_Properties.fec_inner += 8;
					break;
					case FEC_5_6:
						state->m_Properties.fec_inner += 4;
					break;
					/*case FEC_6_7: // undefined
						state->m_Properties.fec_inner += 2;
					break;*/
					case FEC_7_8:
						state->m_Properties.fec_inner += 5;
					break;
					case FEC_8_9:
						state->m_Properties.fec_inner += 6;
					break;
					/*case FEC_AUTO: // undefined
						state->m_Properties.fec_inner += 2;
					break;*/
					case FEC_3_5:
						state->m_Properties.fec_inner += 7;
					break;
					case FEC_9_10:
						state->m_Properties.fec_inner += 9;
					break;
					default: /*FIXME: what now? */
					break;
				}

				/* ROLLOFF */
				switch (c->rolloff) {
					case ROLLOFF_20:
						state->m_Properties.inversion |= 0x08;
					break;
					case ROLLOFF_25:
						state->m_Properties.inversion |= 0x04;
					break;
					case ROLLOFF_35:
					default:
					break;
				}

				/* PILOT */
				switch (c->pilot) {
					case PILOT_ON:
						state->m_Properties.inversion |= 0x10;
					break;
					case PILOT_AUTO:
						state->m_Properties.inversion |= 0x20;
					break;
					case PILOT_OFF:
					default:
					break;
				}
			}
		break;
		case HI_UNF_TUNER_SIG_TYPE_DVB_T:
		case HI_UNF_TUNER_SIG_TYPE_DVB_T2:
		case HI_UNF_TUNER_SIG_TYPE_ISDB_T:
		case HI_UNF_TUNER_SIG_TYPE_ATSC_T:
		case HI_UNF_TUNER_SIG_TYPE_DTMB:
			state->m_Properties.bandwidth_hz = c->bandwidth_hz;
			state->m_Properties.code_rate_HP = c->code_rate_HP;
			state->m_Properties.code_rate_LP = c->code_rate_LP;
			state->m_Properties.modulation = c->modulation;
			state->m_Properties.transmission_mode = c->transmission_mode;
			state->m_Properties.guard_interval = c->guard_interval;
			state->m_Properties.hierarchy = c->hierarchy;
		break;
		default:
			dprintk("[ERROR] Unregognized type %d for tuner %d.\n", state->m_Tuner->m_Attributes.enSigType, state->m_Tuner->m_TunerID);
			return -EINVAL;
		break;
	}

	/* Hisilicon Connect Tuner */
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s setting modulation ...\n", __FUNCTION__);
#endif
	switch (c->modulation) {
		case 1:
			m_Modulation = HI_UNF_MOD_TYPE_QAM_16;
		break;
		case 2:
			m_Modulation = HI_UNF_MOD_TYPE_QAM_32;
		break;
		case 3:
			m_Modulation = HI_UNF_MOD_TYPE_QAM_64;
		break;
		case 4:
			m_Modulation = HI_UNF_MOD_TYPE_QAM_128;
		break;
		case 5:
			m_Modulation = HI_UNF_MOD_TYPE_QAM_256;
		break;
		case 6:
			if (state->m_Tuner->m_Attributes.enSigType == HI_UNF_TUNER_SIG_TYPE_CAB ||
				state->m_Tuner->m_Attributes.enSigType == HI_UNF_TUNER_SIG_TYPE_J83B)
				m_Modulation = HI_UNF_MOD_TYPE_QAM_256;
			else
				m_Modulation = HI_UNF_MOD_TYPE_DEFAULT;
		break;
		case 7:
			m_Modulation = HI_UNF_MOD_TYPE_8VSB;
		break;
		case 8:
			m_Modulation = HI_UNF_MOD_TYPE_16VSB;
		break;
		case 9:
			m_Modulation = HI_UNF_MOD_TYPE_8PSK;
		break;
		case 10:
			m_Modulation = HI_UNF_MOD_TYPE_16APSK;
		break;
		case 11:
			m_Modulation = HI_UNF_MOD_TYPE_32APSK;
		break;
		case 12:
			m_Modulation = HI_UNF_MOD_TYPE_DQPSK;
		break;
		case 13:
			m_Modulation = HI_UNF_MOD_TYPE_BPSK;
		break;
		default:
			m_Modulation = HI_UNF_MOD_TYPE_QPSK;
		break;
	}
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s setting connection parameters ...\n", __FUNCTION__);
#endif
	stConnectPara.enSigType = state->m_Tuner->m_Attributes.enSigType;
	switch (state->m_Tuner->m_Attributes.enSigType) {
		case HI_UNF_TUNER_SIG_TYPE_CAB:
		case HI_UNF_TUNER_SIG_TYPE_J83B:
			stConnectPara.unConnectPara.stCab.bReverse	    = HI_FALSE;
			stConnectPara.unConnectPara.stCab.u32Freq		= c->frequency / 1000;
			stConnectPara.unConnectPara.stCab.u32SymbolRate = c->symbol_rate;
			stConnectPara.unConnectPara.stCab.enModType	    = m_Modulation;
		break;
		case HI_UNF_TUNER_SIG_TYPE_SAT:
			stConnectPara.unConnectPara.stSat.u32Freq = c->frequency;
			stConnectPara.unConnectPara.stSat.u32SymbolRate = c->symbol_rate;
			stConnectPara.unConnectPara.stSat.enPolar = (state->m_Properties.voltage == SEC_VOLTAGE_13) ? HI_UNF_TUNER_FE_POLARIZATION_V : HI_UNF_TUNER_FE_POLARIZATION_H;
		break;
		case HI_UNF_TUNER_SIG_TYPE_DVB_T:
		case HI_UNF_TUNER_SIG_TYPE_DVB_T2:
		case HI_UNF_TUNER_SIG_TYPE_ISDB_T:
		case HI_UNF_TUNER_SIG_TYPE_ATSC_T:
		case HI_UNF_TUNER_SIG_TYPE_DTMB:
			stConnectPara.unConnectPara.stTer.u32Freq 	   	= c->frequency / 1000; 		/* <freq in KHz */
			stConnectPara.unConnectPara.stTer.u32BandWidth 	= c->bandwidth_hz / 1000;	/* <bandwidth in KHz */
			stConnectPara.unConnectPara.stTer.enModType	   	= m_Modulation;   		/* <modulation type */
			stConnectPara.unConnectPara.stTer.bReverse	   	= HI_FALSE;           /* <Spectrum reverse mode */
			stConnectPara.unConnectPara.stTer.enChannelMode	= HI_UNF_TUNER_TER_MODE_BASE;  /* DVB-T2 */
			stConnectPara.unConnectPara.stTer.enDVBTPrio	= HI_UNF_TUNER_TS_PRIORITY_HP; /* DVB-T */
		break;
		default:
		break;
	}

#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s Tuner %d: trying connect ...\n", __FUNCTION__, state->m_Tuner->m_TunerID);
	if (dvb_hisi_frontend_connect(state->m_Tuner->m_TunerID, &stConnectPara, state->m_Properties.sectone, 0) == HI_SUCCESS)
		dprintk("[NOTICE] Tuner %d: connected to frequency %u sr %u.\n", state->m_Tuner->m_TunerID, c->frequency, c->symbol_rate);

	dprintk("[INFO] %s finished.\n", __FUNCTION__);
#else
	dvb_hisi_frontend_connect(state->m_Tuner->m_TunerID, &stConnectPara, state->m_Properties.sectone, 0);
#endif
	return 0;
}

static int dvb_hisi_frontend_get_property(struct dvb_frontend *fe, struct dtv_property* tvp) {
	return HI_FAILURE;
}

static enum dvbfe_algo dvb_hisi_frontend_get_frontend_algo(struct dvb_frontend *fe) {
	return DVBFE_ALGO_SW;
}

static int dvb_hisi_frontend_sleep(struct dvb_frontend *fe) {
	return 0;
}

static int dvb_hisi_frontend_init(struct dvb_frontend *fe) {
	return 0;
}

static int dvb_hisi_frontend_set_tone(struct dvb_frontend *fe, enum fe_sec_tone_mode tone) {
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;

	HI_UNF_TUNER_SWITCH_22K_E enPort = HI_UNF_TUNER_SWITCH_22K_0;
	if (tone == SEC_TONE_ON)
		enPort = HI_UNF_TUNER_SWITCH_22K_22;
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	if (HI_DRV_TUNER_Switch22K(state->m_Tuner->m_TunerID, enPort) == HI_SUCCESS) {
		state->m_Properties.sectone = tone;
		return HI_SUCCESS;
	}
	else
		dprintk("[ERROR] Tuner %d: Failed to set TONE.\n", state->m_Tuner->m_TunerID);

	return HI_FAILURE;
}

static int dvb_hisi_frontend_set_voltage(struct dvb_frontend *fe, enum fe_sec_voltage voltage) {
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	state->m_Properties.voltage = voltage;

	return 0;
}

static int dvb_hisi_frontend_send_diseqc_burst(struct dvb_frontend *fe, enum fe_sec_mini_cmd burst) {
	TUNER_DATA_S stTunerData;
	HI_UNF_TUNER_SWITCH_TONEBURST_E enStatus;
	int (*HI_DRV_TUNER_SendTone)(TUNER_DATA_S *pstTone);
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	if (!(HI_DRV_TUNER_SendTone = (void*)kallsyms_lookup_name("tuner_send_tone"))) {
		dprintk("[ERROR] %s: Failed to load HI_DRV_TUNER_SendTone function.\n", __FUNCTION__);
		return HI_FAILURE;
	}

	if (burst == SEC_MINI_A)
		enStatus = HI_UNF_TUNER_SWITCH_TONEBURST_0;
	else if (burst == SEC_MINI_B)
		enStatus = HI_UNF_TUNER_SWITCH_TONEBURST_1;
	else
		enStatus = HI_UNF_TUNER_SWITCH_TONEBURST_NONE;

	if ((HI_UNF_TUNER_SWITCH_TONEBURST_0 == enStatus) || (HI_UNF_TUNER_SWITCH_TONEBURST_1 == enStatus)) {
		stTunerData.u32Port = state->m_Tuner->m_TunerID;
		stTunerData.u32Data = enStatus - 1;
		return HI_DRV_TUNER_SendTone(&stTunerData);
	}

	return -EINVAL;
}

static int dvb_hisi_frontend_send_diseqc_msg(struct dvb_frontend *fe, struct dvb_diseqc_master_cmd *cmd) {
	int u32Port = 0;
	int s32Port = 0;
	int s32SwitchType = 0;
	unsigned int s32CtrlType = 0;

	int enPolar = 0;
	int enLNB22K = 0;
	HI_UNF_TUNER_DISEQC_SWITCH4PORT_S st4Port;
	HI_UNF_TUNER_DISEQC_SWITCH16PORT_S st16Port;

	HI_UNF_TUNER_DISEQC_POSITION_S stPos;
	HI_UNF_TUNER_DISEQC_LIMIT_S stLimit;
	HI_UNF_TUNER_DISEQC_MOVE_S stMove;
	HI_UNF_TUNER_DISEQC_USALS_ANGULAR_S stAngular;
	HI_UNF_TUNER_DISEQC_RECALCULATE_S stRecal;

	unsigned char framing;
	unsigned char address;
	unsigned char command;
	unsigned char data;
	unsigned char data1;
	unsigned char len;
	struct dvb_diseqc_master_cmd *stCMD = kmalloc(sizeof(struct dvb_diseqc_master_cmd), GFP_KERNEL);
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	memcpy(stCMD, cmd, sizeof(struct dvb_diseqc_master_cmd));

	framing = stCMD->msg[0];
	address = stCMD->msg[1];
	command = stCMD->msg[2];
	data = stCMD->msg[3];
	data1 = stCMD->msg[4];
	len = stCMD->msg_len;

	if (framing == 0xe0 && address == 0x10 && (command == 0x38 || command == 0x39) && len == 4) {
		/* u - committed switch */
		if ((data & 0x01) == 0x01) {
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: SEC_TONE_ON\n", __FUNCTION__);
#endif
			dvb_hisi_frontend_set_tone(fe, SEC_TONE_ON);
		} else if ((data & 0x11) == 0x10) {
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: SEC_TONE_OFF\n", __FUNCTION__);
#endif
			dvb_hisi_frontend_set_tone(fe, SEC_TONE_OFF);
		}

		if ((data & 0x02) == 0x02) {
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: SEC_VOLTAGE_18\n", __FUNCTION__);
#endif
			dvb_hisi_frontend_set_voltage(fe, SEC_VOLTAGE_18);
		} else if ((data & 0x22) == 0x20) {
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: SEC_VOLTAGE_13\n", __FUNCTION__);
#endif
			dvb_hisi_frontend_set_voltage(fe, SEC_VOLTAGE_13);
		}

		/* some invalid combinations ? */
		dvb_hisi_frontend_send_diseqc_burst(fe, ((data & 0x0c) >> 2));
	}

	if (framing == 0xE0 && address == 0x00) {
		switch (command) {
			case 0x00:
				s32SwitchType = 5;
			break;
			case 0x03:
				s32SwitchType = 7;
			break;
		}
	} else if (framing == 0xE0 && address == 0x10) {
		switch (command) {
			case 0x38:
				s32SwitchType = 3;
			break;
			case 0x39:
				s32SwitchType = 4;
			break;
		}

		switch (data) {
			case 0xf0:
				enPolar = 1; 	    enLNB22K = 0; 	    s32Port = 1; 	    u32Port = 1;
			break;
			case 0xf1:
				enPolar = 1;	    enLNB22K = 1;	    s32Port = 1;	    u32Port = 2;
			break;
			case 0xf2:
				enPolar = 0;	    enLNB22K = 0;	    s32Port = 1;	    u32Port = 3;
			break;
			case 0xf3:
				enPolar = 0;	    enLNB22K = 1;	    s32Port = 1;	    u32Port = 4;
			break;
			case 0xf4:
				enPolar = 1;	    enLNB22K = 0;	    s32Port = 2;	    u32Port = 5;
			break;
			case 0xf5:
				enPolar = 1;	    enLNB22K = 1;	    s32Port = 2;	    u32Port = 6;
			break;
			case 0xf6:
				enPolar = 0;	    enLNB22K = 0;	    s32Port = 2;	    u32Port = 7;
			break;
			case 0xf7:
				enPolar = 0;	    enLNB22K = 1;	    s32Port = 2;	    u32Port = 8;
			break;
			case 0xf8:
				enPolar = 1;	    enLNB22K = 0;	    s32Port = 3;	    u32Port = 9;
			break;
			case 0xf9:
				enPolar = 1;	    enLNB22K = 1;	    s32Port = 3;	    u32Port = 10;
			break;
			case 0xfa:
				enPolar = 0;	    enLNB22K = 0;	    s32Port = 3;	    u32Port = 11;
			break;
			case 0xfb:
				enPolar = 0;	    enLNB22K = 1;	    s32Port = 3;	    u32Port = 12;
			break;
			case 0xfc:
				enPolar = 1;	    enLNB22K = 0;	    s32Port = 4;	    u32Port = 13;
			break;
			case 0xfd:
				enPolar = 1;	    enLNB22K = 1;	    s32Port = 4;	    u32Port = 14;
			break;
			case 0xfe:
				enPolar = 0;	    enLNB22K = 0;	    s32Port = 4;	    u32Port = 15;
			break;
			case 0xff:
				enPolar = 0;	    enLNB22K = 1;	    s32Port = 4;	    u32Port = 16;
			break;
		}
	} else if (framing == 0xE0 && address == 0x31) {
#ifdef FRONTEND_DEBUG
		dprintk("[INFO] %s [DISEQC]: MOTOR\n", __FUNCTION__);
#endif
		s32CtrlType = command;
	}

	switch (s32CtrlType) {
		case 0x6A:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC][MOTOR]: STORE\n", __FUNCTION__);
#endif
			stPos.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			stPos.u32Pos = data;
			HI_DRV_TUNER_DISEQC_StorePos(state->m_Tuner->m_TunerID, &stPos);
		break;
		case 0x6B:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC][MOTOR]: GOTOPOS\n", __FUNCTION__);
#endif
			stPos.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			stPos.u32Pos = data;
			HI_DRV_TUNER_DISEQC_GotoPos(state->m_Tuner->m_TunerID, &stPos);
		break;
		case 0x63:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC][MOTOR]: SETLIMIT OFF\n", __FUNCTION__);
#endif
			stLimit.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			stLimit.enLimit = HI_UNF_TUNER_DISEQC_LIMIT_OFF;
			HI_DRV_TUNER_DISEQC_SetLimit(state->m_Tuner->m_TunerID, &stLimit);
		break;
		case 0x66:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC][MOTOR]: SETLIMIT EAST\n", __FUNCTION__);
#endif
			stLimit.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			stLimit.enLimit = HI_UNF_TUNER_DISEQC_LIMIT_EAST;
			HI_DRV_TUNER_DISEQC_SetLimit(state->m_Tuner->m_TunerID, &stLimit);
		break;
		case 0x67:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC][MOTOR]: SETLIMIT WEST\n", __FUNCTION__);
#endif
			stLimit.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			stLimit.enLimit = HI_UNF_TUNER_DISEQC_LIMIT_WEST;
			HI_DRV_TUNER_DISEQC_SetLimit(state->m_Tuner->m_TunerID, &stLimit);
		break;
		case 0x68:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC][MOTOR]: MOVE EAST\n", __FUNCTION__);
#endif
			if (data == 0x00)
				data1 = 0x02;
			else if (data == 0xFF)
				data1 = 0x01;

			stMove.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			stMove.enDir   = HI_UNF_TUNER_DISEQC_MOVE_DIR_EAST;
			stMove.enType  = (HI_UNF_TUNER_DISEQC_MOVE_TYPE_E)data1;
			HI_DRV_TUNER_DISEQC_Move(state->m_Tuner->m_TunerID, &stMove);
		break;
		case 0x69:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC][MOTOR]: MOVE WEST\n", __FUNCTION__);
#endif
			if (data == 0x00)
				data1 = 0x02;
			else if (data == 0xFF)
				data1 = 0x01;

			stMove.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			stMove.enDir   = HI_UNF_TUNER_DISEQC_MOVE_DIR_WEST;
			stMove.enType  = (HI_UNF_TUNER_DISEQC_MOVE_TYPE_E)data1;
			HI_DRV_TUNER_DISEQC_Move(state->m_Tuner->m_TunerID, &stMove);
		break;
		case 0x60:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC][MOTOR]: STOP\n", __FUNCTION__);
#endif
			HI_DRV_TUNER_DISEQC_Stop(state->m_Tuner->m_TunerID, HI_UNF_TUNER_DISEQC_LEVEL_1_X);
		break;
		case 0x6E:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC][MOTOR]: USALS\n", __FUNCTION__);
#endif
			stAngular.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			stAngular.u16Angular = (data << 8) | data1;
			HI_DRV_TUNER_DISEQC_GotoAngular(state->m_Tuner->m_TunerID, &stAngular);
		break;
		case 0x6F:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC][MOTOR]: CALCULATE\n", __FUNCTION__);
#endif
			stRecal.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			HI_DRV_TUNER_DISEQC_Recalculate(state->m_Tuner->m_TunerID, &stRecal);
		break;
		case 7:
			stAngular.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			stAngular.u16Angular = data;
			HI_DRV_TUNER_DISEQC_GotoAngular(state->m_Tuner->m_TunerID, &stAngular);
		break;
	}

	switch (s32SwitchType) {
		case 0:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: SWITCH 0/12V\n", __FUNCTION__);
#endif
			/* HACK: In API this function not making nothing. */
			/* HI_DRV_TUNER_Switch012V(state->m_Tuner->m_TunerID, (HI_UNF_TUNER_SWITCH_0_12V_E)s32Port); */
		break;
		case 1:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: SWITCH TONEBUSTER\n", __FUNCTION__);
#endif
			if (s32Port == HI_UNF_TUNER_SWITCH_TONEBURST_0)
				dvb_hisi_frontend_send_diseqc_burst(fe, SEC_MINI_A);
			else if (s32Port == HI_UNF_TUNER_SWITCH_TONEBURST_1)
				dvb_hisi_frontend_send_diseqc_burst(fe, SEC_MINI_B);
		break;
		case 2:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: SWITCH 22K\n", __FUNCTION__);
#endif
			HI_DRV_TUNER_Switch22K(state->m_Tuner->m_TunerID, (HI_UNF_TUNER_SWITCH_22K_E)s32Port);
		break;
		case 3:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: DISEQC 1.0\n", __FUNCTION__);
#endif
			st4Port.enLevel  = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			st4Port.enPort   = (HI_UNF_TUNER_DISEQC_SWITCH_PORT_E)s32Port;
			st4Port.enPolar  = (HI_UNF_TUNER_FE_POLARIZATION_E)enPolar;
			st4Port.enLNB22K = (HI_UNF_TUNER_FE_LNB_22K_E)enLNB22K;
			HI_DRV_TUNER_DISEQC_Switch4Port(state->m_Tuner->m_TunerID, &st4Port);
		break;
		case 4:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: DISEQC 1.0\n", __FUNCTION__);
#endif
			st16Port.enLevel = HI_UNF_TUNER_DISEQC_LEVEL_1_X;
			st16Port.enPort  = (HI_UNF_TUNER_DISEQC_SWITCH_PORT_E)u32Port;
			HI_DRV_TUNER_DISEQC_Switch16Port(state->m_Tuner->m_TunerID, &st16Port);
		break;
		case 5:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: RESET\n", __FUNCTION__);
#endif
			HI_DRV_TUNER_DISEQC_Reset(state->m_Tuner->m_TunerID, HI_UNF_TUNER_DISEQC_LEVEL_1_X);
		break;
		case 6:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: STANDBY\n", __FUNCTION__);
#endif
			HI_DRV_TUNER_DISEQC_Standby(state->m_Tuner->m_TunerID, HI_UNF_TUNER_DISEQC_LEVEL_1_X);
		break;
		case 7:
#ifdef FRONTEND_DEBUG
			dprintk("[INFO] %s [DISEQC]: WAKEUP\n", __FUNCTION__);
#endif
			HI_DRV_TUNER_DISEQC_WakeUp(state->m_Tuner->m_TunerID, HI_UNF_TUNER_DISEQC_LEVEL_1_X);
		break;
	}

	kfree(stCMD);
	return 0;
}

static void dvb_hisi_frontend_release(struct dvb_frontend *fe) {
	struct dvb_hisi_frontend_state *state = fe->demodulator_priv;
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	kfree(state);
}

/* DVB-T */
static struct dvb_frontend_ops dvb_hisi_frontend_ofdm_ops;
static struct dvb_frontend *dvb_hisi_frontend_ofdm_attach(t_hi_tuner *m_Tuner) {
	struct dvb_frontend *fe = m_Tuner->m_Frontend;
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	if (!fe) {
		struct dvb_hisi_frontend_state *state = NULL;

		/* allocate memory for the internal state */
		state = kmalloc(sizeof(struct dvb_hisi_frontend_state), GFP_KERNEL);
		if (state == NULL) {
			return NULL;
		}

		fe = &state->m_Frontend;
		fe->demodulator_priv = state;
		state->m_Tuner = m_Tuner;
	}

	memcpy(&fe->ops, &dvb_hisi_frontend_ofdm_ops, sizeof(struct dvb_frontend_ops));
	strcpy(fe->ops.info.name, m_Tuner->m_Name);

	return fe;
}

/* DVB-S/S2 */
static struct dvb_frontend_ops dvb_hisi_frontend_qpsk_ops;
static struct dvb_frontend *dvb_hisi_frontend_qpsk_attach(t_hi_tuner *m_Tuner, bool can_2g_modulation) {
	struct dvb_frontend *fe = m_Tuner->m_Frontend;
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	if (!fe) {
		struct dvb_hisi_frontend_state *state = NULL;

		/* allocate memory for the internal state */
		state = kmalloc(sizeof(struct dvb_hisi_frontend_state), GFP_KERNEL);
		if (state == NULL) {
			return NULL;
		}

		fe = &state->m_Frontend;
		fe->demodulator_priv = state;
		state->m_Tuner = m_Tuner;
	}

	memcpy(&fe->ops, &dvb_hisi_frontend_qpsk_ops, sizeof(struct dvb_frontend_ops));
	strcpy(fe->ops.info.name, m_Tuner->m_Name);
	if (can_2g_modulation) {
		fe->ops.info.caps |= FE_CAN_2G_MODULATION;
#if (DVB_API_VERSION << 8 | DVB_API_VERSION_MINOR) > 0x0504
		fe->ops.delsys[1] = SYS_DVBS2;
#endif
	}

	return fe;
}

/* DVB-C */
static struct dvb_frontend_ops dvb_hisi_frontend_qam_ops;
static struct dvb_frontend *dvb_hisi_frontend_qam_attach(t_hi_tuner *m_Tuner) {
	struct dvb_frontend *fe = m_Tuner->m_Frontend;
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	if (!fe) {
		struct dvb_hisi_frontend_state *state = NULL;

		/* allocate memory for the internal state */
		state = kmalloc(sizeof(struct dvb_hisi_frontend_state), GFP_KERNEL);
		if (state == NULL) {
			return NULL;
		}

		fe = &state->m_Frontend;
		fe->demodulator_priv = state;
		state->m_Tuner = m_Tuner;
	}

	memcpy(&fe->ops, &dvb_hisi_frontend_qam_ops, sizeof(struct dvb_frontend_ops));
	strcpy(fe->ops.info.name, m_Tuner->m_Name);

	return fe;
}

static struct dvb_frontend_ops dvb_hisi_frontend_ofdm_ops = {
#if (DVB_API_VERSION << 8 | DVB_API_VERSION_MINOR) > 0x0504
        .delsys = { SYS_DVBT },
#endif
	    .info = {
        .name               = "Hisilicon DVB-T",
        .type               = FE_OFDM,
        .frequency_min      = 51000000,
        .frequency_max      = 863250000,
        .frequency_stepsize	= 62500,
        .caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
                FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
                FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_FEC_AUTO |
                FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
                FE_CAN_TRANSMISSION_MODE_AUTO |
                FE_CAN_GUARD_INTERVAL_AUTO |
                FE_CAN_HIERARCHY_AUTO | FE_CAN_INVERSION_AUTO,
	},

	.release = dvb_hisi_frontend_release,

	.init = dvb_hisi_frontend_init,
	.sleep = dvb_hisi_frontend_sleep,

	.get_property = dvb_hisi_frontend_get_property,
	.get_frontend = dvb_hisi_frontend_get_frontend,
	.get_frontend_algo = dvb_hisi_frontend_get_frontend_algo,
	.set_frontend = dvb_hisi_frontend_set_frontend,

	.read_status = dvb_hisi_frontend_read_status,
	.read_ber = dvb_hisi_frontend_read_ber,
	.read_signal_strength = dvb_hisi_frontend_read_signal_strength,
	.read_snr = dvb_hisi_frontend_read_snr,
	.read_ucblocks = dvb_hisi_frontend_read_ucblocks,
};

static struct dvb_frontend_ops dvb_hisi_frontend_qam_ops = {
#if (DVB_API_VERSION << 8 | DVB_API_VERSION_MINOR) > 0x0504
	    .delsys = { SYS_DVBC_ANNEX_A },
#endif
	    .info = {
	    .name	    	    = "Hisilicon DVB-C",
	    .type	    	    = FE_QAM,
	    .frequency_stepsize = 62500,
	    .frequency_min	    = 51000000,
	    .frequency_max	    = 858000000,
	    .symbol_rate_min	= (57840000/2)/64,     /* SACLK/64 == (XIN/2)/64 */
	    .symbol_rate_max    = (57840000/2)/4,      /* SACLK/4 */
	    .caps = FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 |
	    	    FE_CAN_QAM_128 | FE_CAN_QAM_256 |
	    	    FE_CAN_FEC_AUTO | FE_CAN_INVERSION_AUTO
	},

	.release = dvb_hisi_frontend_release,

	.init = dvb_hisi_frontend_init,
	.sleep = dvb_hisi_frontend_sleep,

	.get_property = dvb_hisi_frontend_get_property,
	.get_frontend = dvb_hisi_frontend_get_frontend,
	.get_frontend_algo = dvb_hisi_frontend_get_frontend_algo,
	.set_frontend = dvb_hisi_frontend_set_frontend,

	.read_status = dvb_hisi_frontend_read_status,
	.read_ber = dvb_hisi_frontend_read_ber,
	.read_signal_strength = dvb_hisi_frontend_read_signal_strength,
	.read_snr = dvb_hisi_frontend_read_snr,
	.read_ucblocks = dvb_hisi_frontend_read_ucblocks,
};

static struct dvb_frontend_ops dvb_hisi_frontend_qpsk_ops = {
#if (DVB_API_VERSION << 8 | DVB_API_VERSION_MINOR) > 0x0504
	    .delsys = { SYS_DVBS },
#endif
	    .info = {
	    .name	    	    = "Hisilicon DVB-S2",
	    .type	    	    = FE_QPSK,
	    .frequency_min	    = 950000,
	    .frequency_max	    = 2150000,
	    .frequency_stepsize = 250,           /* kHz for QPSK frontends */
	    .frequency_tolerance= 29500,
	    .symbol_rate_min	= 1000000,
	    .symbol_rate_max	= 60000000,
	    .caps = FE_CAN_INVERSION_AUTO |
	    	    FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
	    	    FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
	    	    FE_CAN_QPSK
	},

	.release = dvb_hisi_frontend_release,

	.init = dvb_hisi_frontend_init,
	.sleep = dvb_hisi_frontend_sleep,

	.get_property = dvb_hisi_frontend_get_property,
	.get_frontend = dvb_hisi_frontend_get_frontend,
	.get_frontend_algo = dvb_hisi_frontend_get_frontend_algo,
	.set_frontend = dvb_hisi_frontend_set_frontend,

	.read_status = dvb_hisi_frontend_read_status,
	.read_ber = dvb_hisi_frontend_read_ber,
	.read_signal_strength = dvb_hisi_frontend_read_signal_strength,
	.read_snr = dvb_hisi_frontend_read_snr,
	.read_ucblocks = dvb_hisi_frontend_read_ucblocks,

	.set_voltage = dvb_hisi_frontend_set_voltage,
	.set_tone = dvb_hisi_frontend_set_tone,

	.diseqc_send_master_cmd         = dvb_hisi_frontend_send_diseqc_msg,
	.diseqc_send_burst              = dvb_hisi_frontend_send_diseqc_burst,
};

bool dvb_hisi_frontend_load(t_hi_tuner *m_Tuner) {
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	switch (m_Tuner->m_Attributes.enSigType) {
		case HI_UNF_TUNER_SIG_TYPE_CAB:
		case HI_UNF_TUNER_SIG_TYPE_J83B:
			m_Tuner->m_Frontend = dvb_hisi_frontend_qam_attach(m_Tuner);
			m_Tuner->IsNeedParse= true;
		break;
		case HI_UNF_TUNER_SIG_TYPE_SAT:
			//m_Tuner->m_Frontend = dvb_hisi_frontend_qpsk_attach(m_Tuner, false); /* DVB-S */
			m_Tuner->m_Frontend = dvb_hisi_frontend_qpsk_attach(m_Tuner, true); /* DVB-S2 */
			m_Tuner->IsNeedParse= true;
		break;
		case HI_UNF_TUNER_SIG_TYPE_DVB_T:
		case HI_UNF_TUNER_SIG_TYPE_DVB_T2:
		case HI_UNF_TUNER_SIG_TYPE_ISDB_T:
		case HI_UNF_TUNER_SIG_TYPE_ATSC_T:
		case HI_UNF_TUNER_SIG_TYPE_DTMB:
			m_Tuner->m_Frontend = dvb_hisi_frontend_ofdm_attach(m_Tuner);
			m_Tuner->IsNeedParse= false;
		break;
		default:
			dprintk("[ERROR] Unregognized type %d for tuner %d.\n", m_Tuner->m_Attributes.enSigType, m_Tuner->m_TunerID);
		break;
	}

	return m_Tuner->m_Frontend ? true : false;
}

int dvb_hisi_frontend_unload(t_hi_tuner *m_Tuner) {
#ifdef FRONTEND_DEBUG
	dprintk("[INFO] %s called\n", __FUNCTION__);
#endif
	return m_Tuner->m_Frontend ? dvb_unregister_frontend(m_Tuner->m_Frontend) : 0;
}