/****************************************************************************
 * Copyright (C) 2014 FIX94
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

/* Wii U Pro Controller driver for Wii/vWii homebrew.
 * Integrates with libogc wiiuse/bte stack via linker wrapping. */

#include <gccore.h>
#include <ogc/conf.h>
#include <bte/bte.h>
#include <wiiuse/wiiuse.h>
#include <stdio.h>
#include <string.h>
#include "wupc_structs.h"
#include "wupc.h"

#ifndef bswap16
#define bswap16(x) __builtin_bswap16(x)
#endif

struct wiimote_listen_t;
struct wiimote_t;
typedef struct wiimote_t *(*wiiuse_assign_cb)(struct wiimote_listen_t *wml, u8 err);

extern int __real_wiiuse_accept(struct wiimote_listen_t *wml, struct bd_addr *bdaddr, const u8 *name, wiiuse_assign_cb assign_cb);
extern int __real_wiiuse_connect(struct wiimote_listen_t *wml, struct bd_addr *bdaddr, const u8 *name, wiiuse_assign_cb assign_cb);

static vu32 __WUPC_ChannelsUsed = 0;

static conf_pads __WUPC_Devices;
static struct WUPCStat *__WUPC_Connected[WUPC_CHAN_MAX];
static struct WUPCStat __WUPC_Status[CONF_PAD_MAX_REGISTERED];
static struct WUPCData __WUPC_PadData[WUPC_CHAN_MAX];
static struct WUPCButtons __WUPC_PadButtons[WUPC_CHAN_MAX];

static u32 __WUPC_Inited = 0;

static const u8 __WUPC_LEDState[] = { 0x10, 0x20, 0x40, 0x80 };

#define TRANSFER_CALIBRATE 0
#define TRANSFER_DONE      1

static void __WUPC_SetLED(struct bte_pcb *sock, u32 state)
{
	u8 buf[2];
	buf[0] = 0x11;
	buf[1] = __WUPC_LEDState[state];
	bte_senddata(sock, buf, 2);
}

static s32 __WUPC_HandleData(void *arg, void *buffer, u16 len)
{
	struct WUPCStat *stat = (struct WUPCStat*)arg;
	u32 chan = stat->channel;
	if (chan >= WUPC_CHAN_MAX) return ERR_OK;

	if (*(u8*)buffer == 0x3D && len == 22)
	{
		if (stat->transferstate == TRANSFER_CALIBRATE)
		{
			stat->xAxisLmid = bswap16(*(u16*)(((u8*)buffer)+1));
			stat->xAxisRmid = bswap16(*(u16*)(((u8*)buffer)+3));
			stat->yAxisLmid = bswap16(*(u16*)(((u8*)buffer)+5));
			stat->yAxisRmid = bswap16(*(u16*)(((u8*)buffer)+7));
			stat->transferstate = TRANSFER_DONE;
		}
		__WUPC_PadData[chan].xAxisL = bswap16(*(u16*)(((u8*)buffer)+1)) - stat->xAxisLmid;
		__WUPC_PadData[chan].xAxisR = bswap16(*(u16*)(((u8*)buffer)+3)) - stat->xAxisRmid;
		__WUPC_PadData[chan].yAxisL = bswap16(*(u16*)(((u8*)buffer)+5)) - stat->yAxisLmid;
		__WUPC_PadData[chan].yAxisR = bswap16(*(u16*)(((u8*)buffer)+7)) - stat->yAxisRmid;
		__WUPC_PadData[chan].button = ~(*(u16*)(((u8*)buffer)+9)) << 16;
		u8 extradata = ~(*(((u8*)buffer)+11));
		__WUPC_PadData[chan].battery = (extradata >> 4) & 0x7;
		__WUPC_PadData[chan].extra = extradata & 0xF;
	}
	return ERR_OK;
}

static s32 __WUPC_HandleConnect(void *arg, struct bte_pcb *pcb, u8 err)
{
	struct WUPCStat *stat = (struct WUPCStat*)arg;

	if (err != ERR_OK || __WUPC_ChannelsUsed >= WUPC_CHAN_MAX)
	{
		bte_disconnect(pcb);
		return err;
	}

	stat->channel = __WUPC_ChannelsUsed;
	stat->rumble = 0;

	__WUPC_SetLED(pcb, __WUPC_ChannelsUsed);

	u8 buf[3];
	buf[0] = 0x12;
	buf[1] = 0x00;
	buf[2] = 0x3D;
	bte_senddata(pcb, buf, 3);
	stat->transferstate = TRANSFER_CALIBRATE;

	__WUPC_Connected[__WUPC_ChannelsUsed] = stat;
	__WUPC_ChannelsUsed++;
	return err;
}

static s32 __WUPC_HandleAccept_step2(void *arg, struct bte_pcb *pcb, u8 err)
{
	if (err != ERR_OK) {
		bte_disconnect(pcb);
		return ERR_OK;
	}
	return bte_listenasync_step2(pcb, __WUPC_HandleConnect);
}

static s32 __WUPC_HandleConnect_step2(void *arg, struct bte_pcb *pcb, u8 err)
{
	if (err != ERR_OK) {
		bte_disconnect(pcb);
		return ERR_OK;
	}
	return bte_connectasync_step2(pcb, __WUPC_HandleConnect);
}

static s32 __WUPC_HandleDisconnect(void *arg, struct bte_pcb *pcb __attribute__((unused)), u8 err)
{
	struct WUPCStat *stat = (struct WUPCStat*)arg;
	if (__WUPC_ChannelsUsed) __WUPC_ChannelsUsed--;

	u32 i;
	for (i = stat->channel; i < WUPC_CHAN_MAX - 1; ++i)
		__WUPC_Connected[i] = __WUPC_Connected[i+1];
	__WUPC_Connected[WUPC_CHAN_MAX - 1] = NULL;

	for (i = 0; i < CONF_PAD_MAX_REGISTERED; ++i)
	{
		if (__WUPC_Status[i].channel > stat->channel && __WUPC_Status[i].channel != WUPC_CHAN_MAX)
		{
			__WUPC_Status[i].channel--;
			__WUPC_SetLED(__WUPC_Status[i].sock, __WUPC_Status[i].channel);
		}
	}
	stat->channel = WUPC_CHAN_MAX;
	return err;
}

static int __WUPC_IsProController(const struct bd_addr *_bdaddr, const u8 *name)
{
	if (name && strstr((const char *)name, "-UC") != NULL)
		return 1;

	/* Check registered devices in SYSCONF */
	u8 got_addr[] = { _bdaddr->addr[5], _bdaddr->addr[4], _bdaddr->addr[3], _bdaddr->addr[2], _bdaddr->addr[1], _bdaddr->addr[0] };
	u32 i;
	for (i = 0; i < __WUPC_Devices.num_registered; ++i)
	{
		if (strstr(__WUPC_Devices.registered[i].name, "-UC") != NULL)
		{
			if (memcmp(__WUPC_Devices.registered[i].bdaddr, got_addr, 6) == 0)
				return 1;
		}
	}
	return 0;
}

static struct WUPCStat *__WUPC_FindStatusSlot(const struct bd_addr *_bdaddr)
{
	u8 got_addr[] = { _bdaddr->addr[5], _bdaddr->addr[4], _bdaddr->addr[3], _bdaddr->addr[2], _bdaddr->addr[1], _bdaddr->addr[0] };
	u32 i;
	for (i = 0; i < __WUPC_Devices.num_registered; ++i)
	{
		if (memcmp(__WUPC_Devices.registered[i].bdaddr, got_addr, 6) == 0)
			return &__WUPC_Status[i];
	}
	/* Return first unused slot if not found in registered list */
	for (i = 0; i < CONF_PAD_MAX_REGISTERED; ++i)
	{
		if (__WUPC_Status[i].channel == WUPC_CHAN_MAX)
			return &__WUPC_Status[i];
	}
	return &__WUPC_Status[0];
}

static int __WUPC_RegisterPad(struct WUPCStat *stat, struct bd_addr *_bdaddr, int is_accept)
{
	stat->channel = WUPC_CHAN_MAX;
	stat->bdaddr = *_bdaddr;

	if (stat->sock == NULL)
	{
		stat->sock = bte_new();
		if (stat->sock == NULL)
			return ERR_OK;
	}

	bte_arg(stat->sock, stat);
	bte_received(stat->sock, __WUPC_HandleData);
	bte_disconnected(stat->sock, __WUPC_HandleDisconnect);

	if (is_accept)
		bte_listenasync(stat->sock, _bdaddr, __WUPC_HandleAccept_step2);
	else
		bte_connectasync(stat->sock, _bdaddr, __WUPC_HandleConnect_step2);

	return ERR_OK;
}

int __wrap_wiiuse_accept(struct wiimote_listen_t *wml, struct bd_addr *bdaddr, const u8 *name, wiiuse_assign_cb assign_cb)
{
	if (__WUPC_Inited && __WUPC_IsProController(bdaddr, name))
	{
		struct WUPCStat *stat = __WUPC_FindStatusSlot(bdaddr);
		__WUPC_RegisterPad(stat, bdaddr, 1);
		return 1;
	}
	return __real_wiiuse_accept(wml, bdaddr, name, assign_cb);
}

int __wrap_wiiuse_connect(struct wiimote_listen_t *wml, struct bd_addr *bdaddr, const u8 *name, wiiuse_assign_cb assign_cb)
{
	if (__WUPC_Inited && __WUPC_IsProController(bdaddr, name))
	{
		struct WUPCStat *stat = __WUPC_FindStatusSlot(bdaddr);
		__WUPC_RegisterPad(stat, bdaddr, 0);
		return 1;
	}
	return __real_wiiuse_connect(wml, bdaddr, name, assign_cb);
}

void WUPC_Init(void)
{
	if (__WUPC_Inited == 1)
		return;
	if (CONF_GetPadDevices(&__WUPC_Devices) < 0)
		return;

	u32 i;
	for (i = 0; i < WUPC_CHAN_MAX; ++i)
		__WUPC_Connected[i] = NULL;

	for (i = 0; i < CONF_PAD_MAX_REGISTERED; ++i)
	{
		__WUPC_Status[i].channel = WUPC_CHAN_MAX;
		__WUPC_Status[i].sock = NULL;
	}

	__WUPC_ChannelsUsed = 0;
	__WUPC_Inited = 1;
}

void WUPC_Disconnect(u8 chan)
{
	if (chan >= WUPC_CHAN_MAX || __WUPC_Connected[chan] == NULL) return;
	bte_disconnect(__WUPC_Connected[chan]->sock);
}

void WUPC_Shutdown(void)
{
	if (__WUPC_Inited == 0)
		return;

	__WUPC_Inited = 0;

	u32 i;
	for (i = 0; i < CONF_PAD_MAX_REGISTERED; ++i)
	{
		if (__WUPC_Status[i].channel != WUPC_CHAN_MAX && __WUPC_Status[i].sock != NULL)
			bte_disconnect(__WUPC_Status[i].sock);
	}
}

struct WUPCData *WUPC_Data(u8 chan)
{
	if (chan >= WUPC_CHAN_MAX || __WUPC_Connected[chan] == NULL) return NULL;
	return &__WUPC_PadData[chan];
}

void WUPC_Rumble(u8 chan, bool rumble)
{
	if (chan >= WUPC_CHAN_MAX || __WUPC_Connected[chan] == NULL) return;

	u8 buf[2];
	buf[0] = 0x11;
	buf[1] = __WUPC_LEDState[chan] | (rumble ? 1 : 0);
	bte_senddata(__WUPC_Connected[chan]->sock, buf, 2);
}

u32 WUPC_UpdateButtonStats(void)
{
	u32 newstate, oldstate;
	u32 i, ret = 0;
	for (i = 0; i < WUPC_CHAN_MAX; ++i)
	{
		if (__WUPC_Connected[i] == NULL)
			continue;
		newstate = __WUPC_PadData[i].button;
		oldstate = __WUPC_PadButtons[i].state;
		__WUPC_PadButtons[i].state = newstate;
		__WUPC_PadButtons[i].up = oldstate & ~newstate;
		__WUPC_PadButtons[i].down = newstate & (newstate ^ oldstate);
		ret |= (1 << i);
	}
	return ret;
}

u32 WUPC_ButtonsUp(u8 chan)
{
	if (chan >= WUPC_CHAN_MAX || __WUPC_Connected[chan] == NULL) return 0;
	return __WUPC_PadButtons[chan].up;
}

u32 WUPC_ButtonsDown(u8 chan)
{
	if (chan >= WUPC_CHAN_MAX || __WUPC_Connected[chan] == NULL) return 0;
	return __WUPC_PadButtons[chan].down;
}

u32 WUPC_ButtonsHeld(u8 chan)
{
	if (chan >= WUPC_CHAN_MAX || __WUPC_Connected[chan] == NULL) return 0;
	return __WUPC_PadButtons[chan].state;
}

s16 WUPC_lStickX(u8 chan)
{
	if (chan >= WUPC_CHAN_MAX || __WUPC_Connected[chan] == NULL) return 0;
	return __WUPC_PadData[chan].xAxisL;
}

s16 WUPC_lStickY(u8 chan)
{
	if (chan >= WUPC_CHAN_MAX || __WUPC_Connected[chan] == NULL) return 0;
	return __WUPC_PadData[chan].yAxisL;
}

s16 WUPC_rStickX(u8 chan)
{
	if (chan >= WUPC_CHAN_MAX || __WUPC_Connected[chan] == NULL) return 0;
	return __WUPC_PadData[chan].xAxisR;
}

s16 WUPC_rStickY(u8 chan)
{
	if (chan >= WUPC_CHAN_MAX || __WUPC_Connected[chan] == NULL) return 0;
	return __WUPC_PadData[chan].yAxisR;
}
