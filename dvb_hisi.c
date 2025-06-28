/*
 * Copyright (C) Leandro Tavares de Melo <leandrotsampa@yahoo.com.br>, All rights reserved.
 */

#include <bitreader.h>
#include <dvb_hisi.h>
#include <module_version.h>

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/* Global Variables */
static int box_type = 0;
static DMX_MMZ_BUF_S *m_PoolBuf;
static t_hi_tuner *m_Tuners[MAX_ADAPTER] = { NULL };

/* Functions */
t_hi_tuner *dvb_hisi_get_tuner(int TunerID) {
	int i;

	for (i = 0; i < MAX_ADAPTER; i++)
		if (m_Tuners[i])
			if (m_Tuners[i]->m_TunerID == TunerID)
				return m_Tuners[i];

	return NULL;
}

static t_channel *dvb_hisi_channel_alloc(t_hi_tuner *m_Tuner) {
	int i;

	for (i = 0; i < MAX_CHANNEL; ++i) {
		write_lock(&m_Tuner->lock_channel);
		if (!m_Tuner->channels[i].active) {
			int j;
			m_Tuner->channels[i].ts_id       = -1;
			m_Tuner->channels[i].pmt_pid     = -1;
			m_Tuner->channels[i].network_pid = -1;
			m_Tuner->channels[i].number      = -1;
			m_Tuner->channels[i].index       = -1;
			m_Tuner->channels[i].use_ca      = -1;
			for (j = 0; j < MAX_CHANNEL_CA; ++j) {
				m_Tuner->channels[i].ca[j].pid       = -1;
				m_Tuner->channels[i].ca[j].system_id = -1;
				m_Tuner->channels[i].ca[j].active    = false;
				m_Tuner->channels[i].ca[j].crc32     = ((atomic_t){(0)});
			}
			memset(&m_Tuner->channels[i].pids, 0, sizeof(unsigned int) * MAX_CHANNEL_PIDS);
			memset(&m_Tuner->channels[i].m_ParsePMT, 0, sizeof(t_tsparse));
			m_Tuner->channels[i].active      = true;

			write_unlock(&m_Tuner->lock_channel);
			return m_Tuner->channels + i;
		}
		write_unlock(&m_Tuner->lock_channel);
	}

	return NULL;
}

static void dvb_hisi_channel_dealloc(t_hi_tuner *m_Tuner, t_channel *channel) {
	if (channel) {
		int i;
		write_lock(&m_Tuner->lock_channel);
		channel->active              = false;
		channel->m_ParsePMT.status   = false;
		channel->m_ParsePMT.have_size= 0;
		channel->m_ParsePMT.need_size= 0;

		if (channel->index != -1)
			if (!dvb_hisi_ca_key_dealloc(m_Tuner->m_TunerID, channel->number)) {
#ifdef GLOBAL_DEBUG
				dprintk("[ERROR] %s Tuner %d: Failed to dealloc Key for channel %d.\n", __FUNCTION__, m_Tuner->m_TunerID, channel->number);
#endif
			}

		channel->use_ca = -1;
		for (i = 0; i < MAX_CHANNEL_CA; ++i) {
			if (channel->ca[i].active)
				dvb_hisi_pid_dealloc(m_Tuner, channel->ca[i].pid);

			channel->ca[i].pid       = -1;
			channel->ca[i].system_id = -1;
			channel->ca[i].active    = false;
		}
		write_unlock(&m_Tuner->lock_channel);
	}
}

t_channel *dvb_hisi_channel_search(t_hi_tuner *m_Tuner, unsigned int number) {
	int i;

	if (!m_Tuner)
		return NULL;

	for (i = 0; i < MAX_CHANNEL; ++i) {
		read_lock(&m_Tuner->lock_channel);
		if (m_Tuner->channels[i].active && m_Tuner->channels[i].number == number) {
			read_unlock(&m_Tuner->lock_channel);
			return m_Tuner->channels + i;
		}
		read_unlock(&m_Tuner->lock_channel);
	}

	return NULL;
}

static t_channel *dvb_hisi_channel_search_by_pid(t_hi_tuner *m_Tuner, bool use_pmt, unsigned int pid, unsigned int pmt_pid) {
	int i;

	if (!m_Tuner || (!use_pmt && !PID_IS_OK(pid)))
		return NULL;

	for (i = 0; i < MAX_CHANNEL; ++i) {
		read_lock(&m_Tuner->lock_channel);
		if (m_Tuner->channels[i].active) {
			if (use_pmt && m_Tuner->channels[i].pmt_pid == pmt_pid) {
				read_unlock(&m_Tuner->lock_channel);
				return m_Tuner->channels + i;
			} else if (!use_pmt) {
				int j;

				for (j = 0; j < MAX_CHANNEL_PIDS; ++j)
					if (m_Tuner->channels[i].pids[j] == pid) {
						read_unlock(&m_Tuner->lock_channel);
						return m_Tuner->channels + i;
					}
			}
		}
		read_unlock(&m_Tuner->lock_channel);
	}

	return NULL;
}

static t_ca *dvb_hisi_channel_search_by_ca_pid(t_hi_tuner *m_Tuner, unsigned int pid) {
	int i;

	if (!m_Tuner || !PID_IS_OK(pid))
		return NULL;

	for (i = 0; i < MAX_CHANNEL; ++i) {
		read_lock(&m_Tuner->lock_channel);
		if (m_Tuner->channels[i].active) {
			int j;
			for (j = 0; j < MAX_CHANNEL_CA; ++j) {
				if (m_Tuner->channels[i].ca[j].pid == pid) {
					read_unlock(&m_Tuner->lock_channel);
					return m_Tuner->channels[i].ca + j;
				}
			}
		}
		read_unlock(&m_Tuner->lock_channel);
	}

	return NULL;
}

bool dvb_hisi_pid_alloc(t_hi_tuner *m_Tuner, unsigned int pid, int type, enum dmx_ts_pes pes_type) {
	int i;
	t_hi_pid *hPid;

	if (!PID_IS_VALID(pid)) {
		return false;
	} else if ((hPid = dvb_hisi_pid_search(m_Tuner, pid))) {
		atomic_inc(&hPid->count);
		return true;
	}

	for (i = 0; i < MAX_PIDS; ++i) {
		write_lock(&m_Tuner->lock_pid);
		if (!m_Tuner->pids[i].active) {
			DMX_MMZ_BUF_S chBuf;
			HI_UNF_DMX_CHAN_ATTR_S chAttr;

			chAttr.enSecureMode  = HI_UNF_DMX_SECURE_MODE_NONE;
			chAttr.enOutputMode  = HI_UNF_DMX_CHAN_OUTPUT_MODE_PLAY;
			chAttr.enChannelType = HI_UNF_DMX_CHAN_TYPE_POST;
			chAttr.u32BufSize 	 = TS_SIZE * TS_PKT_COUNT;

			if (HI_DRV_DMX_CreateChannel(m_Tuner->m_TunerID, &chAttr, &m_Tuner->pids[i].handler, &chBuf, HI_NULL) != HI_SUCCESS) {
				dprintk("[ERROR] %s Tuner %d: HI_DRV_DMX_CreateChannel for PID 0x%x failed.\n", __FUNCTION__, m_Tuner->m_TunerID, pid);
				m_Tuner->pids[i].handler = 0;
				return false;
			}
			if (HI_DRV_DMX_SetChannelPID(m_Tuner->pids[i].handler, pid) != HI_SUCCESS) {
				dprintk("[ERROR] %s Tuner %d: HI_DRV_DMX_SetChannelPID 0x%x failed.\n", __FUNCTION__, m_Tuner->m_TunerID, pid);
				HI_DRV_DMX_DestroyChannel(m_Tuner->pids[i].handler);
				m_Tuner->pids[i].handler = 0;
				return false;
			}
			if (HI_DRV_DMX_OpenChannel(m_Tuner->pids[i].handler) != HI_SUCCESS) {
				dprintk("[ERROR] %s Tuner %d: HI_DRV_DMX_OpenChannel for PID 0x%x failed.\n", __FUNCTION__, m_Tuner->m_TunerID, pid);
				HI_DRV_DMX_DestroyChannel(m_Tuner->pids[i].handler);
				m_Tuner->pids[i].handler = 0;
				return false;
			}

			m_Tuner->pids[i].active   = true;
			m_Tuner->pids[i].count    = ((atomic_t){(1)});
			m_Tuner->pids[i].pid      = pid;
			m_Tuner->pids[i].type     = type;
			m_Tuner->pids[i].pes_type = pes_type;

			write_unlock(&m_Tuner->lock_pid);
			if (m_Tuner->IsNeedParse) { /** Check if PID exist in a CA channel. **/
				t_channel *channel;

				if ((channel = dvb_hisi_channel_search_by_pid(m_Tuner, false, pid, 0)))
					if (!dvb_hisi_ca_key_insert_pid_by_channel(m_Tuner->m_TunerID, channel, &m_Tuner->pids[i])) {
#ifdef CA_DEBUG
						dprintk("[ERROR] %s Tuner %d: Failed to attach Key to PID 0x%x.\n", __FUNCTION__, m_Tuner->m_TunerID, pid);
#endif
					}
			}

			return true;
		}
		write_unlock(&m_Tuner->lock_pid);
	}

	return false;
}

bool dvb_hisi_pid_dealloc(t_hi_tuner *m_Tuner, unsigned int pid) {
	int i;

	if (!PID_IS_VALID(pid)) {
		return false;
	}

	for (i = 0; i < MAX_PIDS; ++i) {
		if (m_Tuner->pids[i].active && m_Tuner->pids[i].pid == pid) {
			if (atomic_dec_return(&m_Tuner->pids[i].count) == 0) {
				HI_HANDLE hKey;

				if (HI_DRV_DMX_CloseChannel(m_Tuner->pids[i].handler) != HI_SUCCESS)
					dprintk("[ERROR] %s Tuner %d: HI_DRV_DMX_CloseChannel failed\n", __FUNCTION__, m_Tuner->m_TunerID);

				if (HI_DRV_DMX_GetDescramblerKeyHandle(m_Tuner->pids[i].handler, &hKey) == HI_SUCCESS) {
					if (HI_DRV_DMX_DetachDescrambler(hKey, m_Tuner->pids[i].handler) != HI_SUCCESS)
						dprintk("[ERROR] %s: Failed to detach Key from PID 0x%x.\n", __FUNCTION__, pid);

					/* Check if PID exist in Player Demux Port
					 * If exist, will Dettach the KeyHandler.
					 */
					dvb_hisi_ca_key_player_detach(hKey, pid);
				}

				write_lock(&m_Tuner->lock_pid);
				write_lock(&m_Tuner->lock_ts);
				if (HI_DRV_DMX_DestroyChannel(m_Tuner->pids[i].handler) != HI_SUCCESS)
					dprintk("[ERROR] %s Tuner %d: HI_DRV_DMX_DestroyChannel failed\n", __FUNCTION__, m_Tuner->m_TunerID);

				m_Tuner->pids[i].active  = false;
				m_Tuner->pids[i].pid     = 0;
				m_Tuner->pids[i].type    = DMX_TYPE_TS;
				m_Tuner->pids[i].pes_type= DMX_PES_OTHER;
				m_Tuner->pids[i].handler = 0;
				write_unlock(&m_Tuner->lock_pid);
				write_unlock(&m_Tuner->lock_ts);
			}

			return true;
		}
	}

	return false;
}

t_hi_pid *dvb_hisi_pid_search(t_hi_tuner *m_Tuner, unsigned int pid) {
	int i;

	for (i = 0; i < MAX_PIDS; ++i) {
		read_lock(&m_Tuner->lock_pid);
		if (m_Tuner->pids[i].active && m_Tuner->pids[i].pid == pid) {
			read_unlock(&m_Tuner->lock_pid);
			return m_Tuner->pids + i;
		}
		read_unlock(&m_Tuner->lock_pid);
	}

	return NULL;
}

static void dvb_hisi_player_set_mode(DMX_PORT_MODE_E pMode, int pID) {
	int pFromID;
	DMX_PORT_MODE_E pFromMode;

	if (HI_DRV_DMX_GetPortId(PLAYER_DEMUX_PORT, &pFromMode, &pFromID) == HI_SUCCESS)
		if (pFromMode == pMode && pFromID == pID)
			return;

	HI_DRV_DMX_DetachPort(PLAYER_DEMUX_PORT);
	if (pMode == DMX_PORT_MODE_TUNER) {
		if (HI_DRV_DMX_AttachTunerPort(PLAYER_DEMUX_PORT, pID) != HI_SUCCESS)
			dprintk("[ERROR] %s Tuner %d: Failed to set player port.\n",__FUNCTION__, pID);
	} else if (pMode == DMX_PORT_MODE_RAM) {
		if (HI_DRV_DMX_AttachRamPort(PLAYER_DEMUX_PORT, pID) != HI_SUCCESS)
			dprintk("[ERROR] %s Tuner %d: Failed to set player port.\n",__FUNCTION__, pID);
	} else {
		dprintk("[ERROR] %s Tuner %d: Unknown port mode.\n",__FUNCTION__, pID);
	}
}

/* Demux */
static void dvb_hisi_parse_pat(t_hi_tuner *m_Tuner) {
	unsigned int   i = 0;
	unsigned char  *section                 = &m_Tuner->m_ParsePAT.buf[0];
	unsigned char  table_id                 = section[0];
//	unsigned char  section_syntax_indicator = (section[1] & 0x80) >> 7;
	unsigned short section_length           = ((section[1] & 0x0F) << 8) | (section[2] & 0xFF);
	unsigned short transport_stream_id      = ((section[3] & 0xFF) << 8) | (section[4] & 0xFF);
//	unsigned short program_number           = ((section[3] & 0xFF) << 8) | (section[4] & 0xFF);
//	unsigned char  version_number           = (section[5] & 0x3E) >> 1;
//	unsigned char  current_next_indicator   = section[5] & 0x01;
//	unsigned char  section_number           = section[6];
//	unsigned char  last_section_number      = section[7];
//	unsigned int   crc                      = (section[section_length - 1] << 24) | ((section[section_length] & 0xFF) << 16) | ((section[section_length + 1] & 0xFF) << 8) | (section[section_length + 2] & 0xFF);
	unsigned int   data_offset              = 8;
	unsigned int   network_pid              = 0;

	if (table_id != 0x00) {
#ifdef GLOBAL_DEBUG
		dprintk("[DEBUG] %s Tuner %d: Invalid PAT TableID %x.\n", __FUNCTION__, m_Tuner->m_TunerID, table_id);
#endif
		goto process_end;
	} else if (m_Tuner->m_ParsePAT.have_size <= data_offset) {
#ifdef GLOBAL_DEBUG
		dprintk("[DEBUG] %s Tuner %d: Not have all PAT data %d ???\n", __FUNCTION__, m_Tuner->m_TunerID, m_Tuner->m_ParsePAT.have_size);
#endif
		goto process_end;
	}

	for (i = 0; i < ((section_length - 5) / 4) - 1; i += 1) {
		unsigned int program_number = ((section[data_offset] & 0xFF) << 8) | (section[data_offset + 1] & 0xFF);
		unsigned int pid            = ((section[data_offset + 2] & 0x1F) << 8) | (section[data_offset + 3] & 0xFF);

		if (program_number == 0) {
			network_pid = pid;
		} else {
			t_channel *channel;

			/** Check if Channel exist. **/
			if ((channel = dvb_hisi_channel_search_by_pid(m_Tuner, true, 0, pid)))
				goto process_end; /* This PAT Table already processed ??? */

			/** Try alloc Channel. **/
			if (!(channel = dvb_hisi_channel_alloc(m_Tuner))) {
				dprintk("[ERROR] %s Tuner %d: Failed to alloc Channel.\n", __FUNCTION__, m_Tuner->m_TunerID);
				continue;
			}

			channel->ts_id       = transport_stream_id;
			channel->pmt_pid     = pid;
			channel->network_pid = network_pid;
			channel->number      = program_number;
#ifdef IKS_DEBUG
			dprintk("[INFO] %s Tuner %d: Add Channel %d PMT 0x%x TsID 0x%x Network PID 0x%x.\n", __FUNCTION__, m_Tuner->m_TunerID, program_number, pid, transport_stream_id, network_pid);
#endif
		}

		data_offset += 4;
	}

process_end:
	m_Tuner->m_ParsePAT.status    = false;
	m_Tuner->m_ParsePAT.have_size = 0;
	m_Tuner->m_ParsePAT.need_size = 0;
}

static void dvb_hisi_parse_pmt(t_hi_tuner *m_Tuner, t_channel *channel) {
	unsigned int   i = 0, j = 0;
	unsigned char  *section                 = &channel->m_ParsePMT.buf[0];
	unsigned char  table_id                 = section[0];
//	unsigned char  section_syntax_indicator = (section[1] & 0x80) >> 7;
	unsigned short section_length           = ((section[1] & 0x0F) << 8) | (section[2] & 0xFF);
//	unsigned short program_number           = ((section[3] & 0xFF) << 8) | (section[4] & 0xFF);
//	unsigned char  version_number           = (section[5] & 0x3E) >> 1;
//	unsigned char  current_next_indicator   = section[5] & 0x01;
//	unsigned char  section_number           = section[6];
//	unsigned char  last_section_number      = section[7];
//	unsigned short pcr_pid                  = ((section[8] & 0x1F) << 8) | (section[9] & 0xFF);
	unsigned short program_info_length      = ((section[10] & 0x0F) << 8) | (section[11] & 0xFF);
//	unsigned int   crc                      = (section[section_length - 1] << 24) | ((section[section_length] & 0xFF) << 16) | ((section[section_length + 1] & 0xFF) << 8) | (section[section_length + 2] & 0xFF);
	unsigned int   data_offset              = 12;
	int            descriptor_offset        = 0;
	unsigned short len                      = (section_length - 13) - program_info_length;
	unsigned short stream_descriptor_offset = 0;

	if (table_id != 0x02) {
#ifdef GLOBAL_DEBUG
		dprintk("[DEBUG] %s Tuner %d: Channel %d Invalid PMT TableID %x.\n", __FUNCTION__, m_Tuner->m_TunerID, channel->number, table_id);
#endif
		goto process_end;
	} else if (channel->m_ParsePMT.have_size <= data_offset) {
#ifdef GLOBAL_DEBUG
		dprintk("[DEBUG] %s Tuner %d: Channel %d not have all data %d ???\n", __FUNCTION__, m_Tuner->m_TunerID, channel->number, channel->m_ParsePMT.have_size);
#endif
		goto process_end;
	} else if (program_info_length == 0) {
#ifdef GLOBAL_DEBUG
		dprintk("[DEBUG] %s Tuner %d: Channel %d not have CA Descriptions ???\n", __FUNCTION__, m_Tuner->m_TunerID, channel->number);
#endif
		goto process_end;
	}

	/** Parse CA Description **/
	for (j = 0; j < program_info_length; j += descriptor_offset) {
		unsigned char *buf = section + data_offset;
		descriptor_offset = buf[1];

		if (section[data_offset] == 0x09) {
			int k;
#ifdef IKS_DEBUG
			bool IsAddedCA = false;
#endif
			unsigned int ca_system_id = ((buf[2] & 0xFF) << 8) | (buf[3] & 0xFF);
			unsigned int ca_pid       = ((buf[4] & 0x1F) << 8) | (buf[5] & 0xFF);

			for (k = 0; k < MAX_CHANNEL_CA; ++k) {
				if (channel->ca[k].pid == ca_pid) {
#ifdef IKS_DEBUG
					IsAddedCA = true;
#endif
					break;
				} else if (channel->ca[k].pid == -1) {
					if (channel->use_ca == -1)
						channel->use_ca = 0;
					channel->ca[k].pid       = ca_pid;
					channel->ca[k].system_id = ca_system_id;
#ifdef IKS_DEBUG
					IsAddedCA = true;
					dprintk("[DEBUG] %s Tuner %d: Channel %d CA %d PID 0x%x SystemID 0x%x.\n", __FUNCTION__, m_Tuner->m_TunerID, channel->number, k, ca_pid, ca_system_id);
#endif
					break;
				}
			}
#ifdef IKS_DEBUG
			if (!IsAddedCA)
				dprintk("[WARNING] %s Tuner %d: Channel %d Not have slot for CA PID 0x%x.\n", __FUNCTION__, m_Tuner->m_TunerID, channel->number, ca_pid);
#endif
		}

		descriptor_offset += 2;
		data_offset += descriptor_offset;
	}

	data_offset = 12 + program_info_length;
	descriptor_offset = 0;

	for (i = 0; i < len; i += stream_descriptor_offset) {
		t_hi_pid *hPid;
//		unsigned int stream_type    = section[data_offset];
		unsigned int elementary_pid = ((section[data_offset + 1] & 0x1F) << 8) | (section[data_offset + 2] & 0xFF);
		stream_descriptor_offset    = ((section[data_offset + 3] & 0x0F) << 8) | (section[data_offset + 4] & 0xFF);
		data_offset += stream_descriptor_offset + 5;
		stream_descriptor_offset += 5;

		if (elementary_pid == 0x00) {
			goto process_end;
		}

		/** Check if PID already exist in Channel. **/
		for (i = 0; i < MAX_CHANNEL_PIDS; ++i) {
			if (!channel->pids[i]) {
				channel->pids[i] = elementary_pid; /* Add PID. */
				break;
			} else if (channel->pids[i] == elementary_pid) {
				goto process_end;
			}
		}

		/** Check if Pid exist and try attach Key. **/
		if ((hPid = dvb_hisi_pid_search(m_Tuner, elementary_pid)))
			if (!dvb_hisi_ca_key_insert_pid_by_channel(m_Tuner->m_TunerID, channel, hPid)) {
#ifdef CA_DEBUG
				dprintk("[ERROR] %s Tuner %d: Failed to attach Key to PID 0x%x.\n", __FUNCTION__, m_Tuner->m_TunerID, hPid->pid);
#endif
			}

#ifdef IKS_DEBUG
		dprintk("[INFO] %s Tuner %d: Added PID 0x%x for Channel %d.\n", __FUNCTION__, m_Tuner->m_TunerID, elementary_pid, channel->number);
#endif
	}

process_end:
	channel->m_ParsePMT.status    = false;
	channel->m_ParsePMT.have_size = 0;
	channel->m_ParsePMT.need_size = 0;
}

static void dvb_hisi_p2t(t_hi_tuner *m_Tuner, unsigned char *buf, size_t size) {
	BitReader reader;
	unsigned int pid;
	unsigned int sync_byte;
	unsigned int continuity_counter;
	unsigned int transport_priority;
	unsigned int adaptation_field_control;
	unsigned int transport_error_indicator;
	unsigned int payload_unit_start_indicator;
	unsigned int transport_scrambling_control;

	BitReaderInit(&reader, buf, size);

	sync_byte = GetBits(&reader, 8);
	transport_error_indicator = GetBits(&reader, 1);
	payload_unit_start_indicator = GetBits(&reader, 1);
	transport_priority = GetBits(&reader, 1);
	pid = GetBits(&reader, 13);
	transport_scrambling_control = GetBits(&reader, 2);
	adaptation_field_control = GetBits(&reader, 2);
	continuity_counter = GetBits(&reader, 4);

	if (transport_error_indicator != 0) {
#ifdef GLOBAL_DEBUG
		dprintk("[THREAD-ERROR] Tuner %d: Transport Error indicator is %u PID %x\n", m_Tuner->m_TunerID, transport_error_indicator, pid);
#endif
		return;
	} else if (adaptation_field_control == 2 || adaptation_field_control == 3) {
		unsigned int adaptation_field_length = GetBits(&reader, 8);

		if (adaptation_field_length > 0)
			SkipBits(&reader, adaptation_field_length * 8);
	}

	/* Check/Parse Table */
	if (adaptation_field_control == 1 || adaptation_field_control == 3) {
		size_t end;
		size_t beg = 3;
		size_t psize;
		int GenericSyntax = 0;

		if (pid == 0) { /* PAT */
			if (payload_unit_start_indicator) {
				unsigned char *pat;
				unsigned int skip = GetBits(&reader, 8);
				SkipBits(&reader, skip * 8);

				m_Tuner->m_ParsePAT.status   = false;
				m_Tuner->m_ParsePAT.have_size= 0;
				m_Tuner->m_ParsePAT.need_size= 0;
				memset(&m_Tuner->m_ParsePAT.buf[0], 0, sizeof(m_Tuner->m_ParsePAT.buf));

				psize = NumBitsLeft(&reader) / 8;
				pat = GetBitReaderData(&reader);
				end = (((pat[1]<<8)&0x0f) | (pat[2]&0xff)) + 3;
				GenericSyntax = pat[1]&0x80;

				if (GenericSyntax != 0 && end < 3+5+4) {
#ifdef GLOBAL_DEBUG
					dprintk("[THREAD-WARNING] Tuner %d: Failed to get correct length of PAT.\n", m_Tuner->m_TunerID);
#endif
					return;
				} else if (GenericSyntax != 0) {
					beg += 5; // Generic header.
					end -= 4; // CRC.
				}

				if (end < beg) {
					return;
				}

				m_Tuner->m_ParsePAT.status   = true;
				m_Tuner->m_ParsePAT.have_size= (end < psize) ? end : psize;
				m_Tuner->m_ParsePAT.need_size= end;
				memcpy(&m_Tuner->m_ParsePAT.buf[0], pat, m_Tuner->m_ParsePAT.have_size);
			}

			if (m_Tuner->m_ParsePAT.status) {
				if (m_Tuner->m_ParsePAT.have_size < m_Tuner->m_ParsePAT.need_size && !payload_unit_start_indicator) {
					psize = NumBitsLeft(&reader) / 8;
					memcpy(&m_Tuner->m_ParsePAT.buf[m_Tuner->m_ParsePAT.have_size], GetBitReaderData(&reader), psize);
					m_Tuner->m_ParsePAT.have_size += psize;
				}

				if (m_Tuner->m_ParsePAT.have_size >= m_Tuner->m_ParsePAT.need_size) {
					dvb_hisi_parse_pat(m_Tuner);
				}
			}
		} else {
			t_channel *channel;

			if ((channel = dvb_hisi_channel_search_by_pid(m_Tuner, true, 0, pid))) {
				if (payload_unit_start_indicator) {
					unsigned char *pmt;
					unsigned int skip = GetBits(&reader, 8);
					SkipBits(&reader, skip * 8);

					channel->m_ParsePMT.status   = false;
					channel->m_ParsePMT.have_size= 0;
					channel->m_ParsePMT.need_size= 0;
					memset(&channel->m_ParsePMT.buf[0], 0, sizeof(channel->m_ParsePMT.buf));

					psize = NumBitsLeft(&reader) / 8;
					pmt = GetBitReaderData(&reader);
					end = (((pmt[1]<<8)&0x0f) | (pmt[2]&0xff)) + 3;
					GenericSyntax = pmt[1]&0x80;

					if (GenericSyntax != 0 && end < 3+5+4) {
#ifdef GLOBAL_DEBUG
						dprintk("[THREAD-WARNING] Tuner %d: Failed to get correct length of PMT for Channel %d.\n", m_Tuner->m_TunerID, channel->number);
#endif
						return;
					} else if (GenericSyntax != 0) {
						beg += 5; // Generic header.
						end -= 4; // CRC.
					}

					if (end < beg) {
						return;
					}

					channel->m_ParsePMT.status   = true;
					channel->m_ParsePMT.have_size= (end < psize) ? end : psize;
					channel->m_ParsePMT.need_size= end;
					memcpy(&channel->m_ParsePMT.buf[0], pmt, channel->m_ParsePMT.have_size);
				}

				if (channel->m_ParsePMT.status) {
					if (channel->m_ParsePMT.have_size < channel->m_ParsePMT.need_size && !payload_unit_start_indicator) {
						psize = NumBitsLeft(&reader) / 8;
						memcpy(&channel->m_ParsePMT.buf[channel->m_ParsePMT.have_size], GetBitReaderData(&reader), psize);
						channel->m_ParsePMT.have_size += psize;
					}

					if (channel->m_ParsePMT.have_size >= channel->m_ParsePMT.need_size) {
						dvb_hisi_parse_pmt(m_Tuner, channel);
					}
				}
			} else {
				unsigned char *data = GetBitReaderData(&reader);

				if (!data[0] && (data[1] == 0x80 || data[1] == 0x81)) { /* ECM */
					t_ca *ca;
					unsigned char *ecm = &data[1];
					size_t ecm_size = (((ecm[1] & 0x0F) << 8) | ecm[2]) + 3;
					size_t data_size = (NumBitsLeft(&reader) / 8) - 1;

					if (data_size > ecm_size) {
						if ((ca = dvb_hisi_channel_search_by_ca_pid(m_Tuner, pid)) && ca->active) {
							unsigned char *crc = &ecm[ecm_size - ((ca->system_id == 0x1861) ? 9 : 4)];
							int crc32 = htonl(((crc[0] << 24) | (crc[1] << 16) | (crc[2] << 8) | crc[3]) & 0xffffffffL);

							if (atomic_read(&ca->crc32) != crc32) {
								atomic_set(&ca->crc32, crc32);
#ifdef IKS_DEBUG
								dprintk("[ECM] Tuner: %d Pid: %x ECM Type: %x ECM Size: %d Data Size: %d Atto CRC32: %x\n", m_Tuner->m_TunerID, pid, ecm[0], ecm_size, data_size, crc32);
#endif
							}
						}
					}
				}
			}
		}
	}
}

static int dvb_hisi_adapter_thread(void *data) {
	HI_HANDLE *lChannels, *lUpdChannels;
	t_hi_tuner *m_Tuner = (t_hi_tuner *)data;
	m_Tuner->m_ParsePAT.status    = false;
	m_Tuner->m_ParsePAT.have_size = 0;
	m_Tuner->m_ParsePAT.need_size = 0;

	if (!(lChannels = kmalloc(MAX_PIDS * sizeof(HI_HANDLE), GFP_KERNEL))) {
		dprintk("[THREAD-ERROR] Tuner %d: Failed to alloc input handler list.\n", m_Tuner->m_TunerID);
		return HI_FAILURE;
	} else if (!(lUpdChannels = kmalloc(MAX_PIDS * sizeof(HI_HANDLE), GFP_KERNEL))) {
		dprintk("[THREAD-ERROR] Tuner %d: Failed to alloc output handler list.\n", m_Tuner->m_TunerID);
		kfree(lChannels);
		return HI_FAILURE;
	}

	while (!kthread_should_stop()) {
		int i;
		int pids = 0;
		int pu32ChNum = 0;
		int u32AcquiredNum = 0;
		HI_UNF_DMX_CHAN_STATUS_S ChannelStatus;
		DMX_UserMsg_S pktBufList[TS_PKT_COUNT];

		memset(lChannels, 0, MAX_PIDS * sizeof(HI_HANDLE));
		memset(lChannels, 0, MAX_PIDS * sizeof(HI_HANDLE));

		for (i = 0; i < MAX_PIDS; ++i) {
			read_lock(&m_Tuner->lock_pid);
			read_lock(&m_Tuner->lock_ts);
			if (m_Tuner->pids[i].active && m_Tuner->pids[i].handler) {
				lChannels[pids] = m_Tuner->pids[i].handler;
				pids++;
			}
			read_unlock(&m_Tuner->lock_pid);
			read_unlock(&m_Tuner->lock_ts);
		}

		if (pids == 0) {
			msleep_interruptible(50);
			continue;
		}

		if (HI_DRV_DMX_SelectDataHandle(lChannels, pids, lUpdChannels, &pu32ChNum, 0) == HI_SUCCESS) {
			for (i = 0; i < pu32ChNum; i++) {
				read_lock(&m_Tuner->lock_ts);
				if (HI_DRV_DMX_GetChannelStatus(lUpdChannels[i], &ChannelStatus) != HI_SUCCESS ||
					ChannelStatus.enChanStatus == HI_UNF_DMX_CHAN_CLOSE) {
						read_unlock(&m_Tuner->lock_ts);
						continue;
					}

				if (HI_DRV_DMX_AcquireBuf(lUpdChannels[i], TS_PKT_COUNT, &u32AcquiredNum, pktBufList, 500) == HI_SUCCESS) {
					int j;

					read_unlock(&m_Tuner->lock_ts);
					for (j = 0; j < u32AcquiredNum; j++) {
						HI_BOOL IsWrong = HI_FALSE;

						if (pktBufList[j].u32MsgLen != TS_SIZE) {
							dprintk("[THREAD-WARNING] Tuner %d: Data are shorter then TS packet size (188B).\n", m_Tuner->m_TunerID);
							IsWrong = HI_TRUE;
						}

						if ((pktBufList[j].u32BufStartAddr >= m_PoolBuf->PhyAddr) && ((pktBufList[j].u32BufStartAddr - m_PoolBuf->PhyAddr) < m_PoolBuf->Size)) {
							HI_U8* pu8Data = (HI_U8*)(unsigned long)((pktBufList[j].u32BufStartAddr - m_PoolBuf->PhyAddr) + m_PoolBuf->VirAddr);

							if (IsWrong == HI_TRUE) {
								dprintk("[THREAD-WARNING] %s Tuner %d: POS %d Size %u Data: %02x %02x %02x %02x %02x %02x %02x %02x\n",
								__FUNCTION__,
								m_Tuner->m_TunerID,
								j,
								pktBufList[j].u32MsgLen,
								pu8Data[0],
								pu8Data[1],
								pu8Data[2],
								pu8Data[3],
								pu8Data[4],
								pu8Data[5],
								pu8Data[6],
								pu8Data[7]);
							} else {
								/*
								 * Check if need Process TS data.
								 */
								if (m_Tuner->IsNeedParse)
									dvb_hisi_p2t(m_Tuner, pu8Data, TS_SIZE);

								dvb_dmx_swfilter_packets(&m_Tuner->m_Demux, pu8Data, 1);
							}
						} else if (IsWrong == HI_FALSE) {
							dprintk("[THREAD-WARNING] Tuner %d: Problem when try parse buffer (need check?).\n", m_Tuner->m_TunerID);
						}
					}

					read_lock(&m_Tuner->lock_ts);
					if (HI_DRV_DMX_GetChannelStatus(lUpdChannels[i], &ChannelStatus) == HI_SUCCESS &&
						ChannelStatus.enChanStatus != HI_UNF_DMX_CHAN_CLOSE) {
							HI_DRV_DMX_ReleaseBuf(lUpdChannels[i], u32AcquiredNum, pktBufList);
						}
					read_unlock(&m_Tuner->lock_ts);
				} else {
					read_unlock(&m_Tuner->lock_ts);
				}
			}
		} else {
			msleep_interruptible(50);
		}
	}

	kfree(lChannels);
	kfree(lUpdChannels);
	return 0;
}

static int dvb_hisi_start_feed(struct dvb_demux_feed *feed) {
	struct dvb_demux *demux = feed->demux;
	t_hi_tuner *m_Tuner = demux->priv;

	switch (feed->type) {
		case DMX_TYPE_TS:
			if (feed->ts_type & TS_DECODER) {
				switch (feed->pes_type) {
					case DMX_PES_AUDIO:
						m_Tuner->channel_play.audio_pid = feed->pid;

						if (!(m_Tuner->channel_play.status & (1 << DEV_AUDIO) || m_Tuner->channel_play.status & (1 << DEV_VIDEO)))
							dvb_hisi_player_set_mode(DMX_PORT_MODE_TUNER, m_Tuner->m_TunerID);

						m_Tuner->channel_play.status |= (1 << DEV_AUDIO);
					break;
					case DMX_PES_VIDEO:
						m_Tuner->channel_play.video_pid = feed->pid;

						if (!(m_Tuner->channel_play.status & (1 << DEV_AUDIO) || m_Tuner->channel_play.status & (1 << DEV_VIDEO)))
							dvb_hisi_player_set_mode(DMX_PORT_MODE_TUNER, m_Tuner->m_TunerID);

						m_Tuner->channel_play.status |= (1 << DEV_VIDEO);
					break;
					case DMX_PES_TELETEXT:
					case DMX_PES_SUBTITLE:
					break;
					case DMX_PES_PCR:
						m_Tuner->channel_play.pcr_pid = feed->pid;
					break;
					case DMX_PES_OTHER:
					break;
					default:
						return -EINVAL;
				}
			}
		break;
		case DMX_TYPE_SEC:
		break;
		default:
			return -EINVAL;
	}

	if (!dvb_hisi_pid_alloc(m_Tuner, feed->pid, feed->type, feed->pes_type)) {
		if (PID_IS_VALID(feed->pid))
			dprintk("[ERROR] %s Tuner %d: Failed to alloc PID 0x%x\n", __FUNCTION__, m_Tuner->m_TunerID, feed->pid);
		else if (feed->pid == 0x1FFF)
			return HI_SUCCESS;
		return -EBUSY;
	}

	if (atomic_inc_return(&m_Tuner->running_feed_count) == 1) {
#ifdef GLOBAL_DEBUG
		dprintk("[INFO] %s Tuner %d: Starting DMA feed.\n",__FUNCTION__, m_Tuner->m_TunerID);
#endif
		m_Tuner->m_thread = kthread_run(&dvb_hisi_adapter_thread, m_Tuner, "adapter%d", m_Tuner->m_DVBAdapter.num);
	}

	return HI_SUCCESS;
}

static int dvb_hisi_stop_feed(struct dvb_demux_feed *feed) {
	struct dvb_demux *demux = feed->demux;
	t_hi_tuner *m_Tuner = demux->priv;

	switch (feed->type) {
		case DMX_TYPE_TS:
			if (feed->ts_type & TS_DECODER) {
				switch (feed->pes_type)
				{
					case DMX_PES_AUDIO:
						m_Tuner->channel_play.audio_pid = 0;
						m_Tuner->channel_play.status &= ~(1 << DEV_AUDIO);
					break;
					case DMX_PES_VIDEO:
						m_Tuner->channel_play.video_pid = 0;
						m_Tuner->channel_play.status &= ~(1 << DEV_VIDEO);
					break;
					default:
					break;
				}
			}
		break;
		default:
		break;
	}

	if (feed->pid == 0x1FFF) {
		return HI_SUCCESS;
	} else if (!dvb_hisi_pid_dealloc(m_Tuner, feed->pid)) {
		dprintk("[ERROR] %s Tuner %d: Failed to delete PID 0x%x\n", __FUNCTION__, m_Tuner->m_TunerID, feed->pid);
		return HI_FAILURE;
	}

	if (atomic_dec_return(&m_Tuner->running_feed_count) == 0) {
#ifdef GLOBAL_DEBUG
		dprintk("[INFO] %s Tuner %d: Stopping DMA feed.\n",__FUNCTION__, m_Tuner->m_TunerID);
#endif
		if (!IS_ERR_OR_NULL(m_Tuner->m_thread))
			kthread_stop(m_Tuner->m_thread);

		if (m_Tuner->IsNeedParse) {
			int i;

			for (i = 0; i < MAX_CHANNEL; ++i) {
				if (m_Tuner->channels[i].active)
					dvb_hisi_channel_dealloc(m_Tuner, &m_Tuner->channels[i]);
			}
		}
	}

	return HI_SUCCESS;
}

static int dvb_hisi_dmx_set_source(struct dmx_demux* demux, const dmx_source_t *src) {
	return 0;
}

/* DVB-HISI */
static int __init dvb_hisi_init(void) {
	int i, c = 0;
#ifdef ENIGMA2
	char *nim_sockets = kzalloc(sizeof(char) * 2048, GFP_KERNEL);
	char *nim_socket_tmp = "NIM Socket %d:\n\tType: %s\n\tName: %s\n\tHas_Outputs: no\n\tFrontend_Device: %d\n";
#endif

	dprintk("_____________________________________________________________\n");
	dprintk("#               _   _   _   _   _   _   _   _               #\n");
	dprintk("#              / \\ / \\ / \\ / \\ / \\ / \\ / \\ / \\              #\n");
	dprintk("#             ( D | V | B | - | H | I | S | I )             #\n");
	dprintk("#              \\_/ \\_/ \\_/ \\_/ \\_/ \\_/ \\_/ \\_/              #\n");
	dprintk("#                                                           #\n");
	dprintk("# Created by:                                               #\n");
	dprintk("# 	leandrotsampa                                         #\n");
	dprintk("# Contact:                                                  #\n");
	dprintk("# 	leandrotsampa@yahoo.com.br                            #\n");
	dprintk("# Current Version:                                          #\n");
	dprintk("# 	" MOD_VERSION "                            #\n");
	dprintk("#                                                           #\n");
	dprintk("# Supported Boxes:                                          #\n");
	dprintk("# 	0 - Auto-detect           (default)                   #\n");
	dprintk("# 	1 - Atto i-Smart          (DVB-C Quad)                #\n");
	dprintk("# 	2 - Atto Pixel            (DVB-C Triple)              #\n");
	dprintk("# 	3 - Atto Pixel            (DVB-C Triple + ISDB-T)     #\n");
	dprintk("# 	4 - Atto Pixel            (DVB-C Triple + DVB-S)      #\n");
	dprintk("# 	5 - Atto Pixel Premium    (DVB-C Triple + DVB-S Duo)  #\n");
	dprintk("# 	6 - 96Boards Poplar       (DVB-T)                     #\n");
	dprintk("# 	7 - SmartSTB U5-PVR       (DVB-S + DVB-T)             #\n");
	dprintk("# 	8 - Formuler S-Mini       (DVB-S)                     #\n");
	dprintk("\e[4m#___________________________________________________________#\e[24m\n\n");

	if (!dvb_hisi_box_load(box_type)) {
		dprintk("[WARNING] Box not detected.\n");
		return HI_FAILURE;
	}

	if (HI_DRV_DMX_Init() != HI_SUCCESS) {
#ifdef GLOBAL_DEBUG
		dprintk("[ERROR] Demux already Inited. Restarting ... \n");
#endif
		HI_DRV_DMX_DeInit();

		if (HI_DRV_DMX_Init() != HI_SUCCESS) {
			dprintk("[ERROR] Failed to Init Demux.\n");
			return HI_FAILURE;
		}
	}

	if (!(m_PoolBuf = kmalloc(sizeof(DMX_MMZ_BUF_S), GFP_KERNEL))) {
		dprintk("[ERROR] Failed to alloc pool buffer.\n");
		return HI_FAILURE;
	}

	if (HI_DRV_DMX_GetPoolBufAddr(m_PoolBuf) == HI_SUCCESS) {
		switch(m_PoolBuf->Flag) {
			case DMX_MMZ_BUF:
			{
				MMZ_BUFFER_S stMMZBuffer;
				stMMZBuffer.u32StartPhyAddr	= m_PoolBuf->PhyAddr;
				if (HI_DRV_MMZ_Map(&stMMZBuffer) == HI_SUCCESS)
					m_PoolBuf->VirAddr = stMMZBuffer.pu8StartVirAddr;
				else
					dprintk("[ERROR] Failed to map MMZ.\n");
			}
			break;
			case DMX_MMU_BUF:
			{
				SMMU_BUFFER_S stSMMUBuffer;
				stSMMUBuffer.u32StartSmmuAddr = m_PoolBuf->PhyAddr;
				if (HI_DRV_SMMU_Map(&stSMMUBuffer) == HI_SUCCESS)
					m_PoolBuf->VirAddr = stSMMUBuffer.pu8StartVirAddr;
				else
					dprintk("[ERROR] Failed to map SMMU.\n");
			}
			break;
			case DMX_SECURE_BUF:
				m_PoolBuf->VirAddr = (HI_VOID *)(unsigned long)m_PoolBuf->PhyAddr;
			break;
		}
	}
	else
		dprintk("[ERROR] GetPoolBufAddr failed.\n");

	for (i = 0; i < MAX_ADAPTER; i++) {
#ifdef ENIGMA2
		char nim_socket_entry[256];
#endif
		struct dmx_demux *m_DMX;
		struct dvb_demux *m_Demux;
		t_hi_tuner *m_Tuner = kzalloc(sizeof(t_hi_tuner), GFP_KERNEL);

		if (!m_Tuner) {
			dprintk("[ERROR] Failed to alloc. (Tuner: %d)\n", i);
			return -ENOMEM;
		}

		m_Tuners[i] = m_Tuner;
		m_Tuner->m_TunerID = i;

		/* HISILICON: Open and config tuner. */
		if (HI_DRV_TUNER_Open(i) != HI_SUCCESS) {
			dprintk("[ERROR] Tuner %d: Open failed.\n", i);
			goto err_kfree;
		}

		/* Hisilicon Tuner Attrib */
		if (HI_DRV_TUNER_GetDeftAttr(i, &m_Tuner->m_Attributes) != HI_SUCCESS) {
			dprintk("[ERROR] Tuner %d: Failed to get attributes.\n", i);
			goto err_close_tuner;
		}

		if (!dvb_hisi_box_set_tuner_attrib(m_Tuner)) {
			dprintk("[ERROR] Tuner %d: Not found attributes.\n", i);
			goto err_close_tuner;
		}

		if (HI_DRV_TUNER_SetAttr(i, &m_Tuner->m_Attributes) != HI_SUCCESS) {
			dprintk("[ERROR] Tuner %d: Failed to set attributes.\n", i);
			goto err_close_tuner;
		}

		/* Hisilicon Tuner Demux Attrib */
		if (HI_DRV_DMX_TunerPortGetAttr(i, &m_Tuner->m_PortAttrib) != HI_SUCCESS) {
			dprintk("[ERROR] Tuner %d: Failed to get port attributes.\n", i);
			goto err_close_tuner;
		}

		if (!dvb_hisi_box_set_port_attrib(m_Tuner))
			dprintk("[ERROR] Tuner %d: Not found port attributes.\n", i);

		if (HI_DRV_DMX_TunerPortSetAttr(i, &m_Tuner->m_PortAttrib) != HI_SUCCESS)
			dprintk("[ERROR] Tuner %d: Failed to set port attributes.\n", i);

		if (!dvb_hisi_box_set_additional_attrib(m_Tuner))
			dprintk("[ERROR] Tuner %d: Additional attributes failed.\n", i);

		if (HI_DRV_DMX_AttachTunerPort(i, i) != HI_SUCCESS) {
			dprintk("[ERROR] Tuner %d: Failed to attach tuner port.\n", i);
			goto err_close_tuner;
		}

		memset(&m_Tuner->m_Demux, 0, sizeof(m_Tuner->m_Demux));
		m_Demux = &m_Tuner->m_Demux;
		m_Demux->priv = m_Tuner;
		m_Demux->filternum = MAX_DMX_OPEN;
		m_Demux->feednum = MAX_DMX_OPEN;
		m_Demux->start_feed = dvb_hisi_start_feed;
		m_Demux->stop_feed = dvb_hisi_stop_feed;
		m_Demux->dmx.capabilities = DMXDEV_CAP_DUPLEX;

		if (dvb_dmx_init(m_Demux) < 0) {
			dprintk("[ERROR] %s Tuner %i: Failed to Init DMX.\n", __FUNCTION__, i);
			goto err_detach_dmx_port;
		}

		m_DMX = &m_Demux->dmx;
		m_DMX->set_source = dvb_hisi_dmx_set_source;
		m_DMX->priv = m_Tuner;

		m_Tuner->m_HWFrontend.source = DMX_FRONTEND_0 + i;
		m_Tuner->m_MEMFrontend.source = DMX_MEMORY_FE;
		m_Tuner->m_DMXDev.filternum = MAX_DMX_OPEN;
		m_Tuner->m_DMXDev.demux = m_DMX;

		/* create new adapter */
		if (dvb_register_adapter(&m_Tuner->m_DVBAdapter, m_Tuner->m_Name, THIS_MODULE, NULL, adapter_nr) < 0) {
			dprintk("[ERROR] %s Tuner %i: Failed to register adapter.\n", __FUNCTION__, i);
			goto err_dvb_dmx_release;
		}

		if (dvb_dmxdev_init(&m_Tuner->m_DMXDev, &m_Tuner->m_DVBAdapter) < 0) {
			dprintk("[ERROR] %s Tuner %i: Failed to Init DMX Dev.\n", __FUNCTION__, i);
			goto err_dvb_unregister_adapter;
		}

		if (m_DMX->add_frontend(m_DMX, &m_Tuner->m_HWFrontend) < 0) {
			dprintk("[ERROR] %s Tuner %i: Failed to add HW Frontend.\n", __FUNCTION__, i);
			goto err_dvb_dmxdev_release;
		}

		if (m_DMX->add_frontend(m_DMX, &m_Tuner->m_MEMFrontend) < 0) {
			dprintk("[ERROR] %s Tuner %i: Failed to add MEM Frontend.\n", __FUNCTION__, i);
			goto err_remove_hw_frontend;
		}

		if (m_DMX->connect_frontend(m_DMX, &m_Tuner->m_HWFrontend) < 0) {
			dprintk("[ERROR] %s Tuner %i: Failed to connect HW Frontend.\n", __FUNCTION__, i);
			goto err_remove_mem_frontend;
		}

		if (!(dvb_hisi_frontend_load(m_Tuner) && dvb_register_frontend(&m_Tuner->m_DVBAdapter, m_Tuner->m_Frontend) == 0)) {
			dprintk("[ERROR] %s Tuner %i: Failed to register Frontend.\n", __FUNCTION__, i);
			goto err_disconnect_frontend;
		}

		/* Setup for interrupt's */
		rwlock_init(&m_Tuner->lock_channel);
		rwlock_init(&m_Tuner->lock_pid);
		rwlock_init(&m_Tuner->lock_ts);
		m_Tuner->running_feed_count = ((atomic_t){(0)});

		/* Setup TS Parse */
		if (m_Tuner->IsNeedParse) {
			if (dvb_hisi_ca_load(m_Tuner) != HI_SUCCESS)
				dprintk("[ERROR] %s Tuner %i: Failed to load CA Manager.\n", __FUNCTION__, i);

			memset(&m_Tuner->channels, 0, sizeof(t_channel) * MAX_CHANNEL);
		}

#ifdef ENIGMA2
		sprintf(nim_socket_entry, nim_socket_tmp, m_Tuner->m_DVBAdapter.num, m_Tuner->m_TypeName, m_Tuner->m_Name, 0);
		strcat(nim_sockets, nim_socket_entry);
#endif
		c++;
		continue;

err_disconnect_frontend:
		m_DMX->disconnect_frontend(m_DMX);
err_remove_mem_frontend:
		m_DMX->remove_frontend(m_DMX, &m_Tuner->m_MEMFrontend);
err_remove_hw_frontend:
		m_DMX->remove_frontend(m_DMX, &m_Tuner->m_HWFrontend);
err_dvb_dmxdev_release:
		dvb_dmxdev_release(&m_Tuner->m_DMXDev);
err_dvb_unregister_adapter:
		dvb_unregister_adapter(&m_Tuner->m_DVBAdapter);
err_dvb_dmx_release:
		dvb_dmx_release(m_Demux);
err_detach_dmx_port:
		HI_DRV_DMX_DetachPort(i);
err_close_tuner:
		HI_DRV_TUNER_Close(i);
err_kfree:
		kfree(m_Tuner);
		m_Tuners[i] = NULL;
	}

	if (c > 0)
		dprintk("[INFO] Registred %d Hisilicon Tuners.\n", c);
	else
		dprintk("[WARNING] Not found any Hisilicon Tuner.\n");

#ifdef ENIGMA2
	if (!enigma2_proc_load())
		dprintk("[ERROR] Failed to init Enigma2 Proc.\n");
	else if (!enigma2_set_proc_value("bus/nim_sockets", nim_sockets))
		dprintk("[ERROR] Failed to init NimSockets.\n");

	kfree(nim_sockets);
#endif
    return HI_SUCCESS; // Non-zero return means that the module couldn't be loaded.
}

static void __exit dvb_hisi_exit(void) {
	int i;

	for (i = 0; i < MAX_ADAPTER; i++) {
		struct dmx_demux *m_DMX;
		struct dvb_demux *m_Demux;
		t_hi_tuner *m_Tuner = m_Tuners[i];

		if (!m_Tuner)
			continue;

		m_Tuners[i] = NULL;

		m_Demux = &m_Tuner->m_Demux;
		m_DMX = &m_Demux->dmx;

		m_DMX->disconnect_frontend(m_DMX);
		m_DMX->remove_frontend(m_DMX, &m_Tuner->m_MEMFrontend);
		m_DMX->remove_frontend(m_DMX, &m_Tuner->m_HWFrontend);

		dvb_dmxdev_release(&m_Tuner->m_DMXDev);
		dvb_dmx_release(m_Demux);
		dvb_hisi_frontend_unload(m_Tuner);

		if (m_Tuner->IsNeedParse)
			dvb_hisi_ca_unload(m_Tuner);

		dvb_unregister_adapter(&m_Tuner->m_DVBAdapter);

		HI_DRV_DMX_DetachPort(i);
		HI_DRV_TUNER_Close(i);

		kfree(m_Tuner);
	}

	if (m_PoolBuf) {
		switch(m_PoolBuf->Flag)
		{
			case DMX_MMZ_BUF:
			{
				MMZ_BUFFER_S stMMZBuffer;
				stMMZBuffer.pu8StartVirAddr = (HI_U8*)m_PoolBuf->VirAddr;
				HI_DRV_MMZ_Unmap(&stMMZBuffer);
			}
			break;
			case DMX_MMU_BUF:
			{
				SMMU_BUFFER_S stSMMUBuffer;
				stSMMUBuffer.pu8StartVirAddr = (HI_U8*)m_PoolBuf->VirAddr;
				HI_DRV_SMMU_Unmap(&stSMMUBuffer);
			}
			break;
			case DMX_SECURE_BUF:
			break;
		}

		kfree(m_PoolBuf);
	}
#ifdef ENIGMA2
	enigma2_proc_unload();
#endif
	dvb_hisi_box_unload();
}

late_initcall_sync(dvb_hisi_init);
module_exit(dvb_hisi_exit);

MODULE_DESCRIPTION("DVB Frontend module for HiSilicon STB hardware");
MODULE_AUTHOR("Leandro Tavares de Melo <leandrotsampa@yahoo.com.br>");
MODULE_LICENSE("GPL");

module_param_named(boxtype, box_type, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(boxtype, "Manual set for box type (default is 0 (Auto-detect)).");