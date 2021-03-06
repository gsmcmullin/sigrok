/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <sigrok.h>
#include <sigrok-internal.h>

/* demo.c */
extern GIOChannel channels[2];

struct source {
	int fd;
	int events;
	int timeout;
	sr_receive_data_callback cb;
	void *user_data;
};

/* There can only be one session at a time. */
struct sr_session *session;
int num_sources = 0;

struct source *sources = NULL;
int source_timeout = -1;


struct sr_session *sr_session_new(void)
{
	session = calloc(1, sizeof(struct sr_session));

	return session;
}

void sr_session_destroy(void)
{

	g_slist_free(session->devices);

	/* TODO: Loop over protocol decoders and free them. */

	g_free(session);
}

void sr_session_device_clear(void)
{
	g_slist_free(session->devices);
	session->devices = NULL;
}

int sr_session_device_add(struct sr_device *device)
{
	int ret;

	if (device->plugin && device->plugin->opendev) {
		ret = device->plugin->opendev(device->plugin_index);
		if (ret != SR_OK)
			return ret;
	}

	session->devices = g_slist_append(session->devices, device);

	return SR_OK;
}

#if 0
void sr_session_pa_clear(void)
{
	/*
	 * The protocols are pointers to the global set of PA plugins,
	 * so don't free them.
	 */
	g_slist_free(session->analyzers);
	session->analyzers = NULL;
}

void sr_session_pa_add(struct sr_analyzer *an)
{
	session->analyzers = g_slist_append(session->analyzers, an);
}
#endif

void sr_session_datafeed_callback_clear(void)
{
	g_slist_free(session->datafeed_callbacks);
	session->datafeed_callbacks = NULL;
}

void sr_session_datafeed_callback_add(sr_datafeed_callback callback)
{
	session->datafeed_callbacks =
	    g_slist_append(session->datafeed_callbacks, callback);
}

static void sr_session_run_poll()
{
	GPollFD *fds, my_gpollfd;
	int ret, i;

	fds = NULL;
	while (session->running) {
		if (fds)
			free(fds);

		/* Construct g_poll()'s array. */
		fds = malloc(sizeof(GPollFD) * num_sources);
		for (i = 0; i < num_sources; i++) {
#ifdef _WIN32
			g_io_channel_win32_make_pollfd(&channels[0],
					sources[i].events, &my_gpollfd);
#else
			my_gpollfd.fd = sources[i].fd;
			my_gpollfd.events = sources[i].events;
			fds[i] = my_gpollfd;
#endif
		}

		ret = g_poll(fds, num_sources, source_timeout);

		for (i = 0; i < num_sources; i++) {
			if (fds[i].revents > 0 || (ret == 0
				&& source_timeout == sources[i].timeout)) {
				/*
				 * Invoke the source's callback on an event,
				 * or if the poll timeout out and this source
				 * asked for that timeout.
				 */
				if (!sources[i].cb(fds[i].fd, fds[i].revents,
						  sources[i].user_data))
					sr_session_source_remove(sources[i].fd);
			}
		}
	}
	free(fds);

}

int sr_session_start(void)
{
	struct sr_device *device;
	GSList *l;
	int ret;

	sr_info("session: starting");
	for (l = session->devices; l; l = l->next) {
		device = l->data;
		if ((ret = device->plugin->start_acquisition(
				device->plugin_index, device)) != SR_OK)
			break;
	}

	return ret;
}

void sr_session_run(void)
{

	sr_info("session: running");
	session->running = TRUE;

	/* do we have real sources? */
	if (num_sources == 1 && sources[0].fd == -1)
		/* dummy source, freewheel over it */
		while (session->running)
			sources[0].cb(-1, 0, sources[0].user_data);
	else
		/* real sources, use g_poll() main loop */
		sr_session_run_poll();

}

void sr_session_halt(void)
{

	sr_info("session: halting");
	session->running = FALSE;

}

void sr_session_stop(void)
{
	struct sr_device *device;
	GSList *l;

	sr_info("session: stopping");
	session->running = FALSE;
	for (l = session->devices; l; l = l->next) {
		device = l->data;
		if (device->plugin && device->plugin->stop_acquisition)
			device->plugin->stop_acquisition(device->plugin_index, device);
	}

}

static void datafeed_dump(struct sr_datafeed_packet *packet)
{
	struct sr_datafeed_logic *logic;

	switch (packet->type) {
	case SR_DF_HEADER:
		sr_dbg("bus: received SR_DF_HEADER");
		break;
	case SR_DF_TRIGGER:
		sr_dbg("bus: received SR_DF_TRIGGER at %lu ms",
				packet->timeoffset / 1000000);
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		sr_dbg("bus: received SR_DF_LOGIC at %f ms duration %f ms, %"PRIu64" bytes",
				packet->timeoffset / 1000000.0, packet->duration / 1000000.0,
				logic->length);
		break;
	case SR_DF_END:
		sr_dbg("bus: received SR_DF_END");
		break;
	default:
		sr_dbg("bus: received unknown packet type %d", packet->type);
	}

}

void sr_session_bus(struct sr_device *device, struct sr_datafeed_packet *packet)
{
	GSList *l;
	sr_datafeed_callback cb;

	/*
	 * TODO: Send packet through PD pipe, and send the output of that to
	 * the callbacks as well.
	 */
	for (l = session->datafeed_callbacks; l; l = l->next) {
		cb = l->data;
		datafeed_dump(packet);
		cb(device, packet);
	}
}

void sr_session_source_add(int fd, int events, int timeout,
	        sr_receive_data_callback callback, void *user_data)
{
	struct source *new_sources, *s;

	new_sources = calloc(1, sizeof(struct source) * (num_sources + 1));

	if (sources) {
		memcpy(new_sources, sources,
		       sizeof(struct source) * num_sources);
		free(sources);
	}

	s = &new_sources[num_sources++];
	s->fd = fd;
	s->events = events;
	s->timeout = timeout;
	s->cb = callback;
	s->user_data = user_data;
	sources = new_sources;

	if (timeout != source_timeout && timeout > 0
	    && (source_timeout == -1 || timeout < source_timeout))
		source_timeout = timeout;
}

void sr_session_source_remove(int fd)
{
	struct source *new_sources;
	int old, new;

	if (!sources)
		return;

	new_sources = calloc(1, sizeof(struct source) * num_sources);
	for (old = 0, new = 0; old < num_sources; old++)
		if (sources[old].fd != fd)
			memcpy(&new_sources[new++], &sources[old],
			       sizeof(struct source));

	if (old != new) {
		free(sources);
		sources = new_sources;
		num_sources--;
	} else {
		/* Target fd was not found. */
		free(new_sources);
	}
}

