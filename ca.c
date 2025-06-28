/*
 * Copyright (C) Leandro Tavares de Melo <leandrotsampa@yahoo.com.br>, All rights reserved.
 */

#include <dvb_hisi.h>

#define BYTEn(x, n) (*((unsigned char*)&(x)+n))
#define BYTE0(x)    BYTEn(x, 0)
#define BYTE1(x)    BYTEn(x, 1)
#define BYTE2(x)    BYTEn(x, 2)
#define BYTE3(x)    BYTEn(x, 3)

#define EXPIRE_TIME         6

/** Structs **/
static struct t_config {
	bool active;
	bool is_open;
	rwlock_t lock_iks;
	rwlock_t lock_key;
	rwlock_t lock_pid;
	struct semaphore wait_iks;

	t_key keys[MAX_KEYS];
	t_pid pids[MAX_PIDS * MAX_ADAPTER];
} CA;

/** Private Functions **/
bool IsIKSEnabled(struct socket *conn) {
	/* IKS Code not shared, so allways disabled. */
	return false;
}

/** Key Functions **/
static bool dvb_hisi_ca_key_check(int DescID, HI_HANDLE hChannel, bool DetachOnFailure) {
	HI_HANDLE hKey;

	read_lock(&CA.lock_key);
	if (HI_DRV_DMX_GetDescramblerKeyHandle(hChannel, &hKey) == HI_SUCCESS) {
		if (hKey == CA.keys[DescID].handler) {
			read_unlock(&CA.lock_key);
			return true;
		}

		if (DetachOnFailure)
			HI_DRV_DMX_DetachDescrambler(hKey, hChannel);
	}

	read_unlock(&CA.lock_key);
	return false;
}

static bool dvb_hisi_ca_key_create(int TunerID, int DescID) {
	t_key *key;
	HI_UNF_DMX_DESCRAMBLER_ATTR_S stAttr;
	stAttr.enCaType = HI_UNF_DMX_CA_NORMAL;
	stAttr.enDescramblerType = HI_UNF_DMX_DESCRAMBLER_TYPE_CSA2;
	stAttr.enEntropyReduction = HI_UNF_DMX_CA_ENTROPY_REDUCTION_CLOSE;

	if (DescID >= MAX_KEYS) {
		dprintk("[ERROR] %s DescID %d: Is Out Of Array.\n", __FUNCTION__, DescID);
		return false;
	} else {
		key = &CA.keys[DescID];
	}

	write_lock(&CA.lock_key);
	if (!key->handler) {
		if (HI_DRV_DMX_CreateDescrambler(TunerID, &stAttr, &key->handler, HI_NULL) != HI_SUCCESS) {
			dprintk("[ERROR] %s DescID %d: Failed to Create Descrambler for Tuner %d.\n", __FUNCTION__, DescID, TunerID);
			write_unlock(&CA.lock_key);
			return false;
		}

		key->TunerID = TunerID;
	} else if (key->TunerID != TunerID) {
		/* Destroy the key in other tuner. */
		if (key->handler && HI_DRV_DMX_DestroyDescrambler(key->handler) != HI_SUCCESS) {
			dprintk("[ERROR] %s DescID %d: Failed to Destroy Descrambler.\n", __FUNCTION__, DescID);
			write_unlock(&CA.lock_key);
			return false;
		}

		/* Re-create key in this new tuner. Maybe need clean key first ??? */
#ifdef IKS_DEBUG
		if (!CA.is_open)
			dprintk("[INFO] %s: Reseting Key Index %d on creation ...\n", __FUNCTION__, DescID);
#endif
		key->TunerID    = -1;
		key->number     = -1;
		key->crc32      = 0;
		key->decrypted  = false;
		key->expire     = EXPIRE_TIME;
		key->timeout    = -1;
		key->handler    = 0;
		memset(key->oddkey, 0, MAX_KEY_SIZE * 2);
		memset(key->evenkey, 0, MAX_KEY_SIZE * 2);

		if (HI_DRV_DMX_CreateDescrambler(TunerID, &stAttr, &key->handler, HI_NULL) != HI_SUCCESS) {
			dprintk("[ERROR] %s DescID %d: Failed to Create Descrambler for Tuner %d.\n", __FUNCTION__, DescID, TunerID);
			write_unlock(&CA.lock_key);
			return false;
		}

		key->TunerID = TunerID;
	} else {
#ifdef IKS_DEBUG
		if (!CA.is_open)
			dprintk("[INFO] %s: Reseting Key Index %d ...\n", __FUNCTION__, DescID);
#endif
		key->number     = -1;
		key->crc32      = 0;
		key->decrypted  = false;
		key->expire     = EXPIRE_TIME;
		key->timeout    = -1;
		memset(key->oddkey, 0, MAX_KEY_SIZE * 2);
		memset(key->evenkey, 0, MAX_KEY_SIZE * 2);
	}

	write_unlock(&CA.lock_key);
	return true;
}

static bool dvb_hisi_ca_key_attach(int DescID, t_hi_pid *hPid) {
	/** Check/Attach Key to Player if necessary. **/
	dvb_hisi_ca_key_player_attach(DescID, hPid->pid);

	if (dvb_hisi_ca_key_check(DescID, hPid->handler, true))
		return true;

	if (HI_DRV_DMX_AttachDescrambler(CA.keys[DescID].handler, hPid->handler) == HI_SUCCESS)
		if (dvb_hisi_ca_key_check(DescID, hPid->handler, true))
			return true;

	return false;
}

static int dvb_hisi_ca_key_alloc(int TunerID, unsigned int number) {
	int i;

	for (i = 0; i < MAX_KEYS; i++) {
		if (CA.keys[i].number == -1 || CA.keys[i].number == number) {
			if (dvb_hisi_ca_key_create(TunerID, i)) {
#ifdef IKS_DEBUG
				dprintk("[INFO] %s: Allocated Key Index %d for Channel %d.\n", __FUNCTION__, i, number);
#endif
				CA.keys[i].number = number;
				up(&CA.wait_iks);
				return i;
			}

#ifdef IKS_DEBUG
			dprintk("[ERROR] %s: Failed to Alloc Key Index %d for Channel %d.\n", __FUNCTION__, i, number);
#endif
			break;
		}
	}

	return -1;
}

bool dvb_hisi_ca_key_dealloc(int TunerID, unsigned int number) {
	int i;

	for (i = 0; i < MAX_KEYS; i++) {
		if (CA.keys[i].TunerID == TunerID && CA.keys[i].number == number) {
			if (dvb_hisi_ca_key_create(TunerID, i)) {
#ifdef IKS_DEBUG
				dprintk("[INFO] %s: Deallocated Key Index %d for Channel %d.\n", __FUNCTION__, i, number);
#endif
				return true;
			}

#ifdef IKS_DEBUG
			dprintk("[ERROR] %s: Failed to Dealloc Key Index %d for Channel %d.\n", __FUNCTION__, i, number);
#endif
			break;
		}
	}

	return false;
}

void dvb_hisi_ca_key_player_attach(int DescID, unsigned int pid) {
	HI_HANDLE hChannel;

	if (!PID_IS_OK(pid)) {
		dprintk("[ERROR] %s DescID %d: The PID 0x%x is invalid.\n", __FUNCTION__, DescID, pid);
		return;
	} else if (DescID >= MAX_KEYS) {
		dprintk("[ERROR] %s DescID %d: Is Out Of Array.\n", __FUNCTION__, DescID);
		return;
	} else if (!CA.keys[DescID].handler) {
		dprintk("[ERROR] %s DescID %d: Not have a Key for Attach.\n", __FUNCTION__, DescID);
		return;
	}

	if (HI_DRV_DMX_GetChannelHandle(PLAYER_DEMUX_PORT, pid, &hChannel) == HI_SUCCESS)
		if (!dvb_hisi_ca_key_check(DescID, hChannel, true))
			if (HI_DRV_DMX_AttachDescrambler(CA.keys[DescID].handler, hChannel) != HI_SUCCESS)
				dprintk("[ERROR] %s DescID %d: Failed to attach Key to Player channel.\n", __FUNCTION__, DescID);
}

void dvb_hisi_ca_key_player_detach(HI_HANDLE key, unsigned int pid) {
	HI_HANDLE hKey;
	HI_HANDLE hChannel;

	if (HI_DRV_DMX_GetChannelHandle(PLAYER_DEMUX_PORT, pid, &hChannel) == HI_SUCCESS)
		if (HI_DRV_DMX_GetDescramblerKeyHandle(hChannel, &hKey) == HI_SUCCESS)
			if (hKey == key) /* Only Dettach if is the same key. */
				if (HI_DRV_DMX_DetachDescrambler(hKey, hChannel) != HI_SUCCESS)
					dprintk("[ERROR] %s: Failed to detach Key from Player channel.\n", __FUNCTION__);
}

static bool dvb_hisi_ca_key_search(t_hi_pid *hPid) {
	int i;

	for (i = 0; i < (MAX_PIDS * MAX_ADAPTER); i++) {
		read_lock(&CA.lock_pid);
		if (CA.pids[i].pid == hPid->pid && CA.pids[i].index != -1) {
			if (!dvb_hisi_ca_key_check(CA.pids[i].index, hPid->handler, true))
				if (HI_DRV_DMX_AttachDescrambler(CA.keys[CA.pids[i].index].handler, hPid->handler) != HI_SUCCESS) {
					dprintk("[ERROR] %s DescID %d: Failed to attach Key to PID 0x%x.\n", __FUNCTION__, CA.pids[i].index, hPid->pid);
					read_unlock(&CA.lock_pid);
					break;
				}

			/** Check/Attach Key to Player if necessary. **/
			dvb_hisi_ca_key_player_attach(CA.pids[i].index, hPid->pid);

			read_unlock(&CA.lock_pid);
			return true;
		}
		read_unlock(&CA.lock_pid);
	}

	return false;
}

static bool dvb_hisi_ca_key_insert_pid(ca_pid_t *pid) {
	int i;

	for (i = 0; i < MAX_ADAPTER; i++) {
		t_hi_pid *hPid;
		t_hi_tuner *m_Tuner;

		if (!(m_Tuner = dvb_hisi_get_tuner(i)))
			continue;

		if (!(hPid = dvb_hisi_pid_search(m_Tuner, pid->pid)))
			continue;

		write_lock(&CA.lock_iks);
		if (!dvb_hisi_ca_key_create(i, pid->index)) {
			write_unlock(&CA.lock_iks);
			break;
		}
		write_unlock(&CA.lock_iks);

		if (dvb_hisi_ca_key_attach(pid->index, hPid))
			return true;

		break;
	}
#ifdef CA_DEBUG
	dprintk("[ERROR] %s Index %d: Failed to attach a Key to PID 0x%x.\n", __FUNCTION__, pid->index, pid->pid);
#endif
	return false;
}

bool dvb_hisi_ca_key_insert_pid_by_channel(int TunerID, t_channel *channel, t_hi_pid *hPid) {
	if (CA.is_open) {
		return dvb_hisi_ca_key_search(hPid);
	} else if (channel && IsIKSEnabled(NULL)) {
		if (channel->index == -1) {
			channel->index = dvb_hisi_ca_key_alloc(TunerID, channel->number);

			if (channel->index == -1) {
				dprintk("[ERROR] %s Tuner %d: Failed to alloc Key for channel %d.\n", __FUNCTION__, TunerID, channel->number);
				return false;
			}
		}

		return dvb_hisi_ca_key_attach(channel->index, hPid);
	}

	return true;
}

static void dvb_hisi_ca_key_set_cw(ca_descr_t *desc) {
	if (!desc)
		dprintk("[ERROR] %s: Invalid description.\n", __FUNCTION__);

#ifdef CA_DEBUG
	dprintk("[INFO] %s: CA_SET_DESCR Index %d\n", __FUNCTION__, desc->index);
#endif

	if (desc->index >= MAX_KEYS) {
		dprintk("[ERROR] %s Index %d: Is Out Of Array.\n", __FUNCTION__, desc->index);
	} else if (desc->index >= 0) {
		t_key *key = &CA.keys[desc->index];

		write_lock(&CA.lock_key);
		if (!key->handler) {
			dprintk("[ERROR] %s Index %d: Key it's not created yet.\n", __FUNCTION__, desc->index);
		} else if (desc->parity) { /* Parity: 0 == even and 1 == odd */
			memcpy(key->oddkey, desc->cw, MAX_KEY_SIZE);
			HI_DRV_DMX_SetDescramblerOddKey(key->handler, key->oddkey);
		} else {
			memcpy(key->evenkey, desc->cw, MAX_KEY_SIZE);
			HI_DRV_DMX_SetDescramblerEvenKey(key->handler, key->evenkey);
		}
		write_unlock(&CA.lock_key);
	}
}

/** CA Functions **/
static void dvb_hisi_ca_pid_change(ca_pid_t *pid) {
	int i;

	if (!pid) {
		dprintk("[ERROR] %s: Invalid PID.\n", __FUNCTION__);
		return;
	}

#ifdef CA_DEBUG
	dprintk("[INFO] %s: CA_SET_PID Index %d Pid 0x%x\n", __FUNCTION__, pid->index, pid->pid);
#endif

	for (i = 0; i < MAX_PIDS * MAX_ADAPTER; i++) {
		write_lock(&CA.lock_pid);
		if (pid->index == -1 && CA.pids[i].pid == pid->pid) { /* Will remove PID */
			CA.pids[i].pid = CA.pids[i].index = -1;
			write_unlock(&CA.lock_pid);
			break;
		} else if (pid->index >= 0 && CA.pids[i].pid <= 0) { /* Will add PID */
			CA.pids[i].pid = pid->pid;
			CA.pids[i].index = pid->index;
			write_unlock(&CA.lock_pid);
			break;
		}
		write_unlock(&CA.lock_pid);
	}

	if (pid->index >= 0)
		dvb_hisi_ca_key_insert_pid(pid);
}

static unsigned int dvb_hisi_ca_poll(struct file *file, poll_table *wait) {
#ifdef CA_DEBUG
	dprintk("[INFO] %s\n", __FUNCTION__);
#endif
	return 0; /* Return mask */
}

static int dvb_hisi_ca_ioctl(struct file *file, unsigned int cmd, void *parg) {
#ifdef CA_DEBUG
	dprintk("[INFO] %s\n", __FUNCTION__);
#endif

	if ((file->f_flags & O_ACCMODE) == O_RDONLY)
		return -EPERM;

	switch (cmd) {
		case CA_RESET:
#ifdef CA_DEBUG
			dprintk("[INFO] %s: CA_RESET\n", __FUNCTION__);
#endif
		break;
		case CA_GET_CAP:
		{
			ca_caps_t *caps = (ca_caps_t *)parg;
#ifdef CA_DEBUG
			dprintk("[INFO] %s: CA_GET_CAP\n", __FUNCTION__);
#endif
			caps->slot_num	= 0;
			caps->slot_type	= 0;
			caps->descr_num	= 1;
			caps->descr_type= CA_ECD;
		}
		break;
		case CA_GET_DESCR_INFO:
		{
			ca_descr_info_t *cinfo = (ca_descr_info_t *)parg;
#ifdef CA_DEBUG
			dprintk("[INFO] %s: CA_GET_DESCR_INFO\n", __FUNCTION__);
#endif
			cinfo->num	= 1;
			cinfo->type	= CA_ECD;
		}
		break;
		case CA_SET_DESCR:
			dvb_hisi_ca_key_set_cw((ca_descr_t *)parg);
		break;
		case CA_SET_PID:
			dvb_hisi_ca_pid_change((ca_pid_t *)parg);
		break;
		default:
#ifdef CA_DEBUG
			dprintk("[INFO] %s: UNKNOWN %u\n", __FUNCTION__, cmd);
#endif
			return -ENOIOCTLCMD;
		break;
	}

	return HI_SUCCESS;
}

static int dvb_hisi_ca_open(struct inode *inode, struct file *file) {
	int err;
#ifdef CA_DEBUG
	dprintk("[INFO] %s\n", __FUNCTION__);
#endif
	if ((err = dvb_generic_open(inode, file)) < 0)
		return err;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY)
		CA.is_open = true;

	return 0;
}

static int dvb_hisi_ca_release(struct inode *inode, struct file *file) {
#ifdef CA_DEBUG
	dprintk("[INFO] %s\n", __FUNCTION__);
#endif
	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		CA.is_open = false;
		up(&CA.wait_iks);
	}

	return dvb_generic_release(inode, file);
}

static const struct file_operations dvb_hisi_dvb_ca_fops = {
	.owner          = THIS_MODULE,
//	.write          = dvb_audio_write,
	.unlocked_ioctl = dvb_generic_ioctl,
	.open           = dvb_hisi_ca_open,
	.release        = dvb_hisi_ca_release,
	.poll           = dvb_hisi_ca_poll,
	.llseek         = noop_llseek,
};

static struct dvb_device dvb_hisi_dvb_ca = {
	.priv         = NULL,
	.users        = 1,
	.writers      = 1,
	.fops         = &dvb_hisi_dvb_ca_fops,
	.kernel_ioctl = dvb_hisi_ca_ioctl,
};

int dvb_hisi_ca_load(t_hi_tuner *m_Tuner) {
	if (!CA.active) {
		int i;
		CA.active = true;
		CA.is_open = false;
		rwlock_init(&CA.lock_iks);
		rwlock_init(&CA.lock_key);
		rwlock_init(&CA.lock_pid);
		sema_init(&CA.wait_iks, 0);

		/** Default Key Config **/
		for (i = 0; i < MAX_KEYS; i++) {
			t_key *key;
			if (!(key = &CA.keys[i]))
				continue;
			key->TunerID    = -1;
			key->number     = -1;
			key->crc32      = 0;
			key->decrypted  = false;
			key->expire     = EXPIRE_TIME;
			key->timeout    = -1;
			key->handler    = 0;
			memset(key->oddkey, 0, MAX_KEY_SIZE * 2);
			memset(key->evenkey, 0, MAX_KEY_SIZE * 2);
		}

		/** Default PID Config **/
		for (i = 0; i < MAX_KEYS; i++) {
			t_pid *pid;
			if (!(pid = &CA.pids[i]))
				continue;
			pid->index = -1;
			pid->pid   = -1;
		}
	}

	return dvb_register_device(&m_Tuner->m_DVBAdapter, &m_Tuner->m_CA, &dvb_hisi_dvb_ca, NULL, DVB_DEVICE_CA);
}

void dvb_hisi_ca_unload(t_hi_tuner *m_Tuner) {
	if (m_Tuner->m_CA)
		dvb_unregister_device(m_Tuner->m_CA);

	if (CA.active) {
		int i;
		CA.active = false;

		for (i = 0; i < MAX_KEYS; i++) {
			t_key *key;
			if (!(key = &CA.keys[i]))
				continue;
			if (key->handler)
				HI_DRV_DMX_DestroyDescrambler(key->handler);
		}
	}
}