/*
 *   sigrok - ols.c
 *
 *   Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <poll.h>
#include <glib.h>
#include <errno.h>

#include "sigrok.h"
#include "hwplugin.h"
#include "hwcommon.h"

#define NUM_PROBES				32
#define SERIAL_SPEED			B115200
/* TODO: SERIAL_ bits, parity, stop bit */
#define CLOCK_RATE				100


/* command opcodes */
#define CMD_RESET				0x00
#define CMD_ID					0x02
#define CMD_SET_FLAGS			0x82
#define CMD_SET_DIVIDER		0x80
#define CMD_RUN				0x01
#define CMD_CAPTURE_SIZE		0x81

/* bitmasks for CMD_FLAGS */
#define FLAG_DEMUX				0x01
#define FLAG_FILTER			0x02
#define FLAG_CHANNELGROUP_1	0x04
#define FLAG_CHANNELGROUP_2	0x08
#define FLAG_CHANNELGROUP_3	0x10
#define FLAG_CHANNELGROUP_4	0x20
#define FLAG_CLOCK_EXTERNAL	0x40
#define FLAG_CLOCK_INVERTED	0x80


int capabilities[] = {
	HWCAP_LOGIC_ANALYZER,
	HWCAP_SAMPLERATE,
	HWCAP_CAPTURE_RATIO,
	HWCAP_LIMIT_SECONDS,
	HWCAP_LIMIT_SAMPLES,
	0
};

float supported_sample_rates[] = {
	0.2,
	0.25,
	0.50,
	1,
	2,
	4,
	8,
	10,
	20,
	50,
	70,
	100,
	200,
	0
};

/* list of struct serial_device_instance  */
GSList *serial_devices = NULL;

/* current state of the flag register */
uint8_t flag_reg = 0;

float cur_sample_rate = 0;
int limit_seconds = 0;
int limit_samples = 0;
/* pre/post trigger capture ratio, in percentage. 0 means no pre-trigger data. */
int capture_ratio = 0;



void encode_longcommand(uint8_t command, uint32_t data, char *buf)
{

	buf[0] = command;
	buf[1] = data & 0xff;
	buf[2] = data & 0xff00 >> 8;
	buf[3] = data & 0xff0000 >> 16;
	buf[4] = data & 0xff000000 >> 24;

}


int hw_init(char *deviceinfo)
{
	GSList *ports, *l;
	struct pollfd *fds;
	struct termios term, *prev_termios;
	struct serial_device_instance *dev;
	int devcnt, final_devcnt, num_ports, fd, i;
	char buf[8], **device_names;

	if(deviceinfo)
		ports = g_slist_append(NULL, g_strdup(deviceinfo));
	else
		/* no specific device given, so scan all serial ports */
		ports = list_serial_ports();

	num_ports = g_slist_length(ports);
	fds = g_malloc0(num_ports * sizeof(struct pollfd));
	device_names = g_malloc(num_ports * (sizeof(char *)));
	prev_termios = g_malloc(num_ports * sizeof(struct termios));
	devcnt = 0;
	for(l = ports; l; l = l->next)
	{
		/* The discovery procedure is like this: first send the Reset command (0x00) 5 times,
		 * since the device could be anywhere in a 5-byte command. Then send the ID command
		 * (0x02). If the device responds with 4 bytes ("OLS1" or "SLA1"), we have a match.
		 * Since it may take the device a while to respond at 115Kb/s, we do all the sending
		 * first, then wait for all of them to respond with poll().
		 */
		fd = open(l->data, O_RDWR | O_NONBLOCK);
		if(fd != -1)
		{
			tcgetattr(fd, &prev_termios[devcnt]);
			tcgetattr(fd, &term);
			cfsetispeed(&term, SERIAL_SPEED);
			tcsetattr(fd, TCSADRAIN, &term);
			memset(buf, CMD_RESET, 5);
			buf[5] = CMD_ID;
			if(write(fd, buf, 6) == 6)
			{
				fds[devcnt].fd = fd;
				fds[devcnt].events = POLLIN;
				device_names[devcnt] = l->data;
				devcnt++;
				g_message("probed device %s", (char *) l->data);
			}
			else
			{
				tcsetattr(fd, TCSADRAIN, &prev_termios[devcnt]);
				g_free(l->data);
			}
		}
	}

	/* 2ms should do it, that's enough time for 28 bytes to go over the bus */
	usleep(2000);
	final_devcnt = 0;
	poll(fds, devcnt, 1);
	for(i = 0; i < devcnt; i++)
	{
		if(fds[i].revents == POLLIN)
		{
			if(read(fds[i].fd, buf, 4) == 4)
			{
				if(!strncmp(buf, "1SLO", 4) || !strncmp(buf, "1ALS", 4))
				{
					dev = g_malloc(sizeof(struct serial_device_instance));
					dev->index = final_devcnt;
					dev->status = ST_INACTIVE;
					dev->port = g_strdup(device_names[i]);
					dev->fd = -1;
					if(!strncmp(buf, "1SLO", 4))
						dev->model = g_strdup("OLS v1.0");
					else
						dev->model = g_strdup("Sump v1.0");
					serial_devices = g_slist_append(serial_devices, dev);
					final_devcnt++;
					fds[i].fd = 0;
				}
			}
		}

		if(fds[i].fd != 0)
			tcsetattr(fds[i].fd, TCSADRAIN, &prev_termios[i]);
		close(fds[i].fd);
	}

	g_free(fds);
	g_free(device_names);
	g_free(prev_termios);
	g_slist_free(ports);

	return final_devcnt;
}


int hw_opendev(int device_index)
{
	struct serial_device_instance *sdi;

	if(!(sdi = get_serial_device_instance(serial_devices, device_index)))
		return SIGROK_NOK;

	sdi->fd = open(sdi->port, O_RDWR);
	if(sdi->fd == -1)
		return SIGROK_NOK;
	sdi->status = ST_ACTIVE;

	return SIGROK_OK;
}


void hw_closedev(int device_index)
{
	struct serial_device_instance *sdi;

	if(!(sdi = get_serial_device_instance(serial_devices, device_index)))
		return;

	if(sdi->fd != -1)
	{
		close(sdi->fd);
		sdi->fd = -1;
		sdi->status = ST_INACTIVE;
	}

}


void hw_cleanup(void)
{
	GSList *l;
	struct serial_device_instance *sdi;

	/* properly close all devices */
	for(l = serial_devices; l; l = l->next)
	{
		sdi = l->data;
		if(sdi->fd != -1)
			close(sdi->fd);
		g_free(sdi->model);
		g_free(sdi->port);
		g_free(sdi);
	}
	g_slist_free(serial_devices);
	serial_devices = NULL;

}


char *hw_get_device_info(int device_index, int device_info_id)
{
	char *info;

	info = NULL;
	switch(device_info_id)
	{
	case DI_IDENTIFIER:
		info = g_malloc(16);
		snprintf(info, 16, "unit %d", device_index);
		break;
	case DI_NUM_PROBES:
		info = GINT_TO_POINTER(NUM_PROBES);
		break;
	case DI_SAMPLE_RATES:
		info = (char *) supported_sample_rates;
		break;
	case DI_TRIGGER_TYPES:
		info = (char *) "01";
		break;
	}

	return info;
}


int hw_get_status(int device_index)
{
	struct serial_device_instance *sdi;

	if(!(sdi = get_serial_device_instance(serial_devices, device_index)))
		return ST_NOT_FOUND;

	return sdi->status;
}


int *hw_get_capabilities(void)
{

	return capabilities;
}


int set_configuration_samplerate(struct serial_device_instance *sdi, float rate)
{
	uint32_t divider;
	char buf[5];

	if(rate < 0.00001 || rate > 200)
		return SIGROK_ERR_BADVALUE;

	if(rate  > CLOCK_RATE)
	{
		flag_reg |= FLAG_DEMUX;
		divider = (uint8_t) (CLOCK_RATE * 2 / rate) - 1;
	}
	else
	{
		flag_reg &= FLAG_DEMUX;
		divider = (uint8_t) (CLOCK_RATE / rate) - 1;
	}

	g_message("setting sample rate to %.3f Mhz (divider %d, demux %s)", rate, divider,
			flag_reg & FLAG_DEMUX ? "on" : "off");
	encode_longcommand(CMD_SET_DIVIDER, divider, buf);

	if(write(sdi->fd, buf, 5) != 5)
	{
		g_warning("failed to set rate");
		return SIGROK_NOK;
	}
	cur_sample_rate = rate;

	return SIGROK_OK;
}


int hw_set_configuration(int device_index, int capability, char *value)
{
	struct serial_device_instance *sdi;
	int ret;

	if(!(sdi = get_serial_device_instance(serial_devices, device_index)))
		return SIGROK_NOK;

	if(sdi->status != ST_ACTIVE)
		return SIGROK_NOK;

	if(capability == HWCAP_SAMPLERATE)
		ret = set_configuration_samplerate(sdi, atof(value));
	else if(capability == HWCAP_LIMIT_SECONDS)
	{
		limit_seconds = atoi(value);
		ret = SIGROK_OK;
	}
	else if(capability == HWCAP_LIMIT_SAMPLES)
	{
		limit_samples = atoi(value);
		ret = SIGROK_OK;
	}
	else if(capability == HWCAP_CAPTURE_RATIO)
	{
		capture_ratio = atoi(value);
		if(capture_ratio < 0 || capture_ratio > 100)
		{
			capture_ratio = 0;
			ret = SIGROK_NOK;
		}
		else
			ret = SIGROK_OK;
	}
	else
		ret = SIGROK_NOK;

	return ret;
}


int hw_start_acquisition(int device_index, gpointer session_device_id)
{
	struct serial_device_instance *sdi;
	char buf[5];
	uint32_t rdc;

	if(!(sdi = get_serial_device_instance(serial_devices, device_index)))
		return SIGROK_NOK;

	if(sdi->status != ST_ACTIVE)
		return SIGROK_NOK;

	/* send flag register */
	buf[0] = flag_reg;
	if(write(sdi->fd, buf, 1) != 1)
		return SIGROK_NOK;

	/* send sample limit and pre/post-trigger capture ratio */
	rdc = limit_samples / 4 << 16;
	if(capture_ratio)
		rdc |= (limit_samples - (limit_samples / 100 * capture_ratio)) / 4;
	encode_longcommand(CMD_CAPTURE_SIZE, rdc, buf);
	if(write(sdi->fd, buf, 5) != 5)
		return SIGROK_NOK;

	/* TODO: send trigger configuration */

	return SIGROK_OK;
}


void hw_stop_acquisition(int device_index, gpointer session_device_id)
{

}



struct device_plugin plugin_info = {
	"Openbench Logic Sniffer",
	1,
	hw_init,
	hw_cleanup,

	hw_opendev,
	hw_closedev,
	hw_get_device_info,
	hw_get_status,
	hw_get_capabilities,
	hw_set_configuration,
	hw_start_acquisition,
	hw_stop_acquisition
};
