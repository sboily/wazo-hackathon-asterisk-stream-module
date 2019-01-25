/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) <2019>, Wazo Authors
 *
 * Wazo Authors <dev@wazo.io>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 *
 * Please follow coding guidelines
 * https://wiki.asterisk.org/wiki/display/AST/Coding+Guidelines
 */

/*! \file
 *
 * \brief Asterisk websocket stream ressource
 *
 * \author\verbatim Wazo Authors <dev@wazo.io> \endverbatim
 *
 * This is a resource to stream audio channel via the Asterisk websocket
 * \ingroup resources
 */


/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

/*! \requirements
 *
 * asterisk - http://asterisk.org
 *
 * Build:
 *
 * make
 * make install
 * make samples
 * 
 */


#include <stdio.h>
#include <semaphore.h>

#include "asterisk.h"
#include "asterisk/module.h"
#include "asterisk/data_buffer.h"
#include "asterisk/http_websocket.h"
#include "asterisk/framehook.h"
#include "asterisk/channel.h"
#include "asterisk/format_cache.h"
#include "asterisk/translate.h"


/*** DOCUMENTATION
	<configInfo name="res_ari_stream_websocket" language="en_US">
		<synopsis>Websocket Stream Audio Channel</synopsis>
	</configInfo>
 ***/


#define WS_MAXIMUM_FRAME_SIZE 8192
/* If defined, enable buffering ( frame before sending it to the websocket */
//#define WS_BUFFER 1


struct transport_read_data {
	struct ws_transport *transport;
	char *payload;
	uint64_t payload_len;
};

#define FSIZE (8192-sizeof(int))
struct frame_data {
	char data[FSIZE];
	size_t datalen;
};

#define FRAME_BUFFER_SIZE 128
struct context {
	ast_mutex_t lock;
	int running;

	struct ast_trans_pvt *trans_pvt;

	struct ast_data_buffer *frame_buffer;

	unsigned int sent;
	unsigned int dropped;
};

static void sleepms(unsigned int v) {
	usleep(v / 1000);
}

static int is_running(struct context *c) {
	ast_mutex_lock(&c->lock);
	int v = c->running;
	ast_mutex_unlock(&c->lock);
	return v;
}

static void frame_data_free(void *data) {
	ast_free(data);
	return;
}

static void hook_destroy_cb(void *framedata) {
	struct context *c = (struct context *)framedata;

	ast_mutex_lock(&c->lock);
	c->running = 0;
	ast_mutex_unlock(&c->lock);
}

static struct ast_frame *hook_event_cb(struct ast_channel *chan, struct ast_frame *frame, enum ast_framehook_event event, void *data)
{
	struct context *c = (struct context *)data;
	if (!frame) {
		return frame;
	}

	if (event != AST_FRAMEHOOK_EVENT_READ) {
		return frame;
	}

	if (frame->frametype != AST_FRAME_VOICE)  {
		return frame;
	}

	if (c->trans_pvt == NULL) {
		c->trans_pvt = ast_translator_build_path(ast_format_slin16, frame->subclass.format);
		if (!c->trans_pvt) {
			ast_log(LOG_ERROR, "translator_build_path error\n");
			return frame;
		}
	}

	if (is_running(c) == 0) {
		return frame;
	}

	ast_mutex_lock(&c->lock);
	if (ast_data_buffer_count(c->frame_buffer) == ast_data_buffer_max(c->frame_buffer)) {
		c->dropped++;
		ast_mutex_unlock(&c->lock);
		return frame;
	}
	ast_mutex_unlock(&c->lock);

	struct ast_frame *outframe;
	if (!(outframe = ast_translate(c->trans_pvt, frame, 0))) {
		return frame;
	}

	if (outframe->datalen > (int)FSIZE) {
		ast_mutex_lock(&c->lock);
		c->dropped++;
		ast_mutex_unlock(&c->lock);
		return frame;
	}
	
	struct frame_data *fdata = ast_malloc(sizeof(struct frame_data));
	memcpy(&fdata->data[0], outframe->data.ptr, outframe->datalen);
	fdata->datalen = outframe->datalen;
	
	ast_mutex_lock(&c->lock);
	ast_data_buffer_put(c->frame_buffer, 0, fdata);
	ast_mutex_unlock(&c->lock);

	return frame;
}

static void websocket_cb(struct ast_websocket *session, struct ast_variable *parameters, struct ast_variable *headers)
{
	/*
	if (ast_websocket_set_nonblock(session)) {
		ast_websocket_unref(session);
		return;
	}
	*/

	int write_timeout = AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT;
	if (ast_websocket_set_timeout(session, write_timeout)) {
		ast_websocket_unref(session);
		return;
	}

	char *channel_id= NULL;
	struct ast_variable *h = headers;
	for(;;) {
		if (strcmp(h->name,"Channel-ID")==0) {
			channel_id = (char*)h->value;
			break;
		}
		if (h->next == NULL) {
			break;
		}
		h = h->next;
	}

	if (channel_id == NULL) {
		ast_log(LOG_ERROR, "Channel-ID not set in header\n");
		ast_websocket_unref(session);
		return;
	}


	struct ast_channel *channel;
	channel = ast_channel_get_by_name(channel_id);
	if (channel == NULL) {
		ast_log(LOG_ERROR, "Channel not found\n");
		ast_websocket_unref(session);
		return;
	}

	struct context *c = ast_calloc(1, sizeof(struct context));
	ast_mutex_init(&c->lock);
	c->frame_buffer = ast_data_buffer_alloc(frame_data_free, FRAME_BUFFER_SIZE);

	struct ast_framehook_interface interface = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = hook_event_cb,
		.destroy_cb = hook_destroy_cb,
		.data = c,
	};

	int id = ast_framehook_attach(channel, &interface);
	ast_log(LOG_NOTICE, "channel %s : new call (interface id = 0x%x)\n", channel_id, id);

#ifdef WS_BUFFER
	void *buffer = ast_malloc(WS_MAXIMUM_FRAME_SIZE);
	size_t buffer_len;
#endif
	c->running = 1;
	while (1) {
		if (is_running(c) == 0) {
			ast_log(LOG_NOTICE, "channel %s : end call\n", channel_id);
			break;
		}

		ast_mutex_lock(&c->lock);
		size_t count = ast_data_buffer_count(c->frame_buffer);
		ast_mutex_unlock(&c->lock);
		if (count == 0) {
			sleepms(50);
			continue;
		}

		ast_mutex_lock(&c->lock);
		struct frame_data *outframe = ast_data_buffer_remove_head(c->frame_buffer);
		ast_mutex_unlock(&c->lock);
		
#ifdef WS_BUFFER
		if ((outframe->datalen + buffer_len) > WS_MAXIMUM_FRAME_SIZE) {
			if (ast_websocket_write(session, AST_WEBSOCKET_OPCODE_BINARY, buffer, buffer_len) < 0) {
				ast_log(LOG_NOTICE, "channel %s : end sesssion\n", channel_id);
				ast_free(outframe);
				break;
			}
			buffer_len = 0;
		}
		memcpy(&buffer[buffer_len], &outframe->data[0], outframe->datalen);
		buffer_len += outframe->datalen;
#else
		if (ast_websocket_write(session, AST_WEBSOCKET_OPCODE_BINARY, &outframe->data[0], outframe->datalen) < 0) {
			ast_log(LOG_NOTICE, "channel %s : end sesssion\n", channel_id);
			ast_free(outframe);
			break;
		}
#endif
		ast_free(outframe);

		ast_mutex_lock(&c->lock);
		c->sent++;
		ast_mutex_unlock(&c->lock);
	}

#ifdef WS_BUFFER
	if (buffer_len > 0) {
		if (ast_websocket_write(session, AST_WEBSOCKET_OPCODE_BINARY, buffer, buffer_len) < 0) {
			ast_log(LOG_NOTICE, "channel %s : write error\n", channel_id);
		}
	}
	ast_free(buffer);
#endif
	
	ast_mutex_lock(&c->lock);
	c->running = 0;
	ast_mutex_unlock(&c->lock);
	
	ast_framehook_detach(channel, id);

	ast_data_buffer_free(c->frame_buffer);
	ast_translator_free_path(c->trans_pvt);
	ast_free(c);

	ast_channel_unref(channel);
	ast_websocket_unref(session);
}

static int reload_module(void)
{
	return 0;
}

static int unload_module(void)
{
	ast_websocket_remove_protocol("stream-channel", websocket_cb);
	return 0;
}

static int load_module(void)
{
	if (ast_websocket_add_protocol("stream-channel", websocket_cb)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Asterisk Websocket Channel Stream Module",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
);
