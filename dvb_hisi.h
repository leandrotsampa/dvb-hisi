/*
 * Copyright (C) Leandro Tavares de Melo <leandrotsampa@yahoo.com.br>, All rights reserved.
 */

#ifndef _DVB_HISI_H
#define _DVB_HISI_H

#include <linux/module.h>	// included for all kernel modules
#include <linux/kernel.h>	// included for KERN_INFO
#include <linux/init.h>		// included for __init and __exit macros
#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/fs.h>		// for basic filesystem
#include <linux/fcntl.h>
#include <linux/proc_fs.h>	// for the proc filesystem
#include <linux/seq_file.h>	// for sequence files
#include <linux/slab.h>
#include <linux/stat.h>		// for acces rights defines
#include <linux/string.h>
#include <linux/kallsyms.h>
#include <linux/kthread.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/version.h>
#include <linux/dvb/ca.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>

#include <asm/uaccess.h> // needed for acces kernel/user memory
#include <net/sock.h>    // needed for socket structs

#include <demux.h>
#include <dmxdev.h>
#include <dvb_demux.h>
#include <dvb_frontend.h>
#include <dvb_net.h>
#include <dvbdev.h>
#include <stdbool.h>

/* Hisilicon */
#include <hi_drv_ao.h>
#include <hi_drv_demux.h>
#include <hi_drv_descrambler.h>
#include <hi_drv_file.h>
#include <hi_drv_gpio.h>
#include <hi_drv_hdmi.h>
#include <hi_drv_i2c.h>
#include <hi_drv_mce.h>
#include <hi_drv_mmz.h>
#include <hi_drv_tuner.h>
#include <hi_drv_vdec.h>
#include <hi_unf_sound.h>
#include <hi_unf_disp.h>
#include <hi_drv_disp.h>
#include <hi_drv_win.h>

#include <hi_unf_descrambler.h>
#include <drv_hdmi_intf.h>

#define TAGDEBUG "[dvb-hisi] "
#define dprintk(x...) do { printk(TAGDEBUG x); } while (0)

#define PID_IS_OK(x)      (x > 0 && x < 0x1FFF)  // Ignore PID 0
#define PID_IS_VALID(x)   (x >= 0 && x < 0x1FFF) // Include PID 0
#define TS_SIZE           188
#define TS_PKT_COUNT      40
#ifndef MAX_ADAPTER
#define MAX_ADAPTER       4
#endif

#define MAX_DMX_OPEN      128 /* Max number open() request can be done on demux device. */
#define MAX_PIDS          64  /* Max PIDs by Tuner (Hisilicon only support 96 for all) */
#define MAX_CHANNEL       64  /* Max channels from PAT Table */
#define MAX_CHANNEL_CA    8   /* Max CA System from Channel */
#define MAX_CHANNEL_PIDS  32  /* Max PIDs from Channel */
#define MAX_KEYS          32  /* Max supported by Hisilicon Descrambler */
#define MAX_KEY_SIZE      8
#define PLAYER_TS_PORT    0
#define PLAYER_DEMUX_PORT 4
#define DEV_AUDIO         1
#define DEV_VIDEO         2

typedef struct {
	char *name;          /* Official Name of Host */
	struct in_addr addr; /* IPv4 Address */
} t_host;

typedef struct {
	int index;
	unsigned int pid;
} t_pid;

typedef struct {
	unsigned int pid;
	unsigned int system_id;
	bool         active;   // Used for add pid in case of no CS enabled
	atomic_t     crc32;    // Atto ECM CRC32
} t_ca;

typedef struct {
	int           TunerID;
	unsigned int  number;
	int           crc32; // Atto ECM CRC32
	bool          decrypted;
	int           expire;// Time in seconds to check timeout
	unsigned long timeout;

	HI_U8 oddkey[MAX_KEY_SIZE * 2];
	HI_U8 evenkey[MAX_KEY_SIZE * 2];

	HI_HANDLE handler;
} t_key;

typedef struct {
	bool status;

	size_t have_size;
	size_t need_size;
	unsigned char buf[TS_SIZE * 3];
} t_tsparse;

typedef struct {
	bool active;
	unsigned int ts_id;
	unsigned int pmt_pid;
	unsigned int network_pid;
	unsigned int number;

	/** CA System **/
	int index;
	int use_ca;
	t_ca ca[MAX_CHANNEL_CA];

	/** For Parse PMT **/
	t_tsparse m_ParsePMT;

	/** Channel PID List **/
	unsigned int pids[MAX_CHANNEL_PIDS];
} t_channel;

typedef struct {
	int status;

	unsigned int audio_pid;
	unsigned int video_pid;
	unsigned int pcr_pid;
} t_channel_play;

typedef struct {
	bool     active;
	atomic_t count;

	unsigned int pid;
	int type;
	enum dmx_ts_pes pes_type;
	HI_HANDLE handler;
} t_hi_pid;

typedef struct {
	int m_TunerID;
	char *m_Name;
	char *m_TypeName;
	bool IsNeedParse;
	struct dvb_demux m_Demux;
	struct dvb_adapter m_DVBAdapter;
	struct dmx_frontend m_HWFrontend;
	struct dmx_frontend m_MEMFrontend;
	struct dmxdev m_DMXDev;
	struct dvb_device *m_Audio;
	struct dvb_device *m_Video;
	struct dvb_device *m_CA;

	/** For channel processing **/
	struct dvb_frontend *m_Frontend;
	HI_UNF_TUNER_ATTR_S m_Attributes;
	HI_UNF_DMX_PORT_ATTR_S m_PortAttrib;

	rwlock_t lock_channel;
	rwlock_t lock_pid;
	rwlock_t lock_ts;

	t_hi_pid pids[MAX_PIDS];
	t_channel channels[MAX_CHANNEL];
	t_channel_play channel_play;

	/** For Parse PAT **/
	t_tsparse m_ParsePAT;

	struct   task_struct *m_thread;
	atomic_t running_feed_count;
} t_hi_tuner;

/** Global Functions **/
t_hi_tuner *dvb_hisi_get_tuner(int TunerID);
t_channel *dvb_hisi_channel_search(t_hi_tuner *m_Tuner, unsigned int number);
bool dvb_hisi_pid_alloc(t_hi_tuner *m_Tuner, unsigned int pid, int type, enum dmx_ts_pes pes_type);
bool dvb_hisi_pid_dealloc(t_hi_tuner *m_Tuner, unsigned int pid);
t_hi_pid *dvb_hisi_pid_search(t_hi_tuner *m_Tuner, unsigned int pid);

/** Box Detector Functions **/
bool dvb_hisi_box_set_tuner_attrib(t_hi_tuner *m_Tuner);
bool dvb_hisi_box_set_additional_attrib(t_hi_tuner *m_Tuner);
bool dvb_hisi_box_set_port_attrib(t_hi_tuner *m_Tuner);
bool dvb_hisi_box_load(int box_type);
void dvb_hisi_box_unload(void);

/** Frontend Functions **/
bool dvb_hisi_frontend_load(t_hi_tuner *m_Tuner);
int dvb_hisi_frontend_unload(t_hi_tuner *m_Tuner);

/** CA / Key Functions **/
bool dvb_hisi_ca_key_dealloc(int TunerID, unsigned int number);
void dvb_hisi_ca_key_player_attach(int DescID, unsigned int pid);
void dvb_hisi_ca_key_player_detach(HI_HANDLE key, unsigned int pid);
bool dvb_hisi_ca_key_insert_pid_by_channel(int TunerID, t_channel *channel, t_hi_pid *hPid);
int dvb_hisi_ca_load(t_hi_tuner *m_Tuner);
void dvb_hisi_ca_unload(t_hi_tuner *m_Tuner);

/** Enigma2 Proc **/
bool enigma2_set_proc_value(char *name, char *value);
bool enigma2_proc_load(void);
void enigma2_proc_unload(void);

/** Socket Functions **/
int socket_read(struct socket *sock, void *buf, size_t length, unsigned long flags);
int socket_write(struct socket *sock, void *buf, size_t length, unsigned long flags);
t_host *gethostbyname(unsigned char *host);

#endif
