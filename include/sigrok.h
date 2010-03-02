/*
 *   sigrok - sigrok.h
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

#ifndef SIGROK_H_
#define SIGROK_H_

#include <glib.h>
#include <sys/time.h>
#include <stdint.h>


/* returned status/error codes */
#define SIGROK_STATUS_DISABLED		0
#define SIGROK_OK					1
#define SIGROK_NOK					2
#define SIGROK_ERR_BADVALUE		20


enum {
	PROTO_RAW,
};

struct protocol {
	char *name;
	int id;
	int stackindex;
};


enum {
	DF_HEADER,
	DF_END,
	DF_LOGIC8,
	DF_LOGIC16,
	DF_LOGIC24,
	DF_LOGIC32
};

struct datafeed_packet {
	uint16_t type;
	uint16_t length;
	unsigned char *payload;
};

struct datafeed_header {
	int feed_version;
	struct timeval starttime;
	float rate;
	int protocol_id;
	int num_probes;
};


void feed_data_header(int sessionid, struct datafeed_header);
void feed_data(int sessionid, int packet_type, char *datafeed);


#endif /* SIGROK_H_ */