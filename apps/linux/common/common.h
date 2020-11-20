/*
 * Copyright 2014-2016 Freescale Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *    Neither the name of NXP Semiconductors nor the
 *    names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 @file
 @brief GenAVB common functions
 @details

 Copyright 2015 Freescale Semiconductor, Inc.
 All Rights Reserved.
*/

#ifndef _COMMON_H_
#define _COMMON_H_

#include <poll.h>

#include <genavb/genavb.h>
#include "stats.h"
#include "time.h"

#define K		1024
#define DATA_BUF_SZ	(16*K)
#define EVENT_BUF_SZ	(K)

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_STREAMS	4

#define MEDIA_STREAM_STATE_CONNECTED	(1 << 0)

#define MEDIA_FLAGS_SET_AM824_LABEL_RAW (1 << 0)
#define MEDIA_FLAGS_ALSA 		(1 << 1)
#define MEDIA_FLAGS_FILE 		(1 << 2)

#define AM824_LABEL_RAW 		0x40

#define CFG_TALKER_LATENCY_NS	1000000			// Additional fixed playback latency in ns
#define min(a,b)  ((a)<(b)?(a):(b))
#define max(a,b)  ((a)>(b)?(a):(b))

/**
 * Generic media stream context
 */
struct media_stream {
	int index;
	unsigned int state;
	struct avb_stream_params params;
	struct avb_stream_handle *handle;
	int fd;
	unsigned int batch_size;
	unsigned int flags;

	struct media_thread *thread;

	void *data;						/**< Media application stream private data */
};

/**
 * Generic media control handler
 */
struct media_control {
	void *data;
	struct avb_control_handle *handle;			/** AVB control handle. Can be NULL */
	int (*config_handler)(struct media_stream *);		/**< Media application stream configuration callback. Called when a connect event is received but before the avb stream is created.
									Used to adjust stream create parameters. Can be NULL */
	int (*connect_handler)(struct media_stream *);		/**< Media application stream connect callback. Called once the avb stream has been created. Can be NULL. */
	int (*disconnect_handler)(struct media_stream *);	/**< Media application stream disconnect callback. Called when a disconnect event is received, before destroying the avb stream. Can be NULL. */
};


/**
 * Generic AVDECC listener/talker handler
 */
struct avdecc_controlled {
	void *data;
	struct avb_control_handle *handle;
	int (*aem_set_control_handler)(struct avdecc_controlled *, avb_u16, void *);
};

/**
 * Generic media thread context
 * Must be initialized before calling media_thread_loop()
 */
struct media_thread {
	int (*init_handler)(struct media_thread *);	/**< Media application initialization callback. Called once at the start of media_thread_loop().
								Used for application thread context initialization. Can be NULL. */
	int (*exit_handler)(struct media_thread *);	/**< Media application exit callback. Called once at the end of media_thread_loop().
								Used for application thread context cleanup. Can be NULL.  */
	int (*timeout_handler)(struct media_thread *);	/**< Media application timeout callback. Called at each timeout from media_thread_loop().
								Used for application thread periodic tasks. Can be NULL. */
	int (*signal_handler)(struct media_thread *);	/**< Media application signal callback. Called at each iteration of media_thread_loop().
								Used to handle application thread signals. Can be NULL. */

	int (*data_handler)(struct media_stream *);	/**< Media application data callback. Called each time data can be written/read from the stream file descriptor. */

	struct avb_handle *avb_h;			/**< AVB handle */

	struct media_stream stream[MAX_STREAMS];
	int num_streams;				/**< Number of active streams handled by this thread */
	unsigned int max_supported_streams;			/**< Number of maximum stream index that can be handled by this thread */

	struct pollfd poll_fd[MAX_STREAMS + 2];

	struct media_control ctrl;

	struct avdecc_controlled controlled;

	unsigned int timeout_ms;			/**< media_thread_loop() timeout period. Only used if timeout_handler() is non NULL */

	void *data;					/**< Media application thread private data */
};

/**
 * Enables/disables media stream file descriptor polling in media_thread_loop()
 */
void media_stream_poll_set(const struct media_stream *stream, int enable);

/**
 * Generic media thread main loop handler
 */
int media_thread_loop(struct media_thread *thread);

void print_stream_id(avb_u8 *id);
int listener_file_handler(struct avb_stream_handle *stream_h, int fd, unsigned int batch_size, struct stats *s);
void listener_stream_flush(struct avb_stream_handle *stream_h);
int talker_file_handler(struct avb_stream_handle *stream_h, int fd, unsigned int batch_size, unsigned int flags);
void talker_stream_flush(struct avb_stream_handle *stream_h, struct avb_stream_params *params);
int file_read(int fd, unsigned char *buf, unsigned int len, unsigned int timeout);
int avdecc_controlled_handler(struct avdecc_controlled *controlled, unsigned int events);

#endif /* _COMMON_H_ */

#ifdef __cplusplus
}
#endif