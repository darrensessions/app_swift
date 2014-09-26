/*
 * app_swift -- A Cepstral Swift TTS engine interface
 *
 * Copyright (C) 2006 - 2012, Darren Sessions 
 * Portions Copyright (C) 2012, Cepstral LLC.
 * Asterisk 11 additions/several fixes by Jeremy Kister 2013.01.24
 *
 * All rights reserved.
 * 
 *
 * This program is free software, distributed under the 
 * terms of the GNU General Public License Version 2. See 
 * the LICENSE file at the top of the source tree for more
 * information.
 *
 */

/*!
 * \file
 * \author Darren Sessions <darrensessions@me.com>
 * \brief Cepstral Swift text-to-speech engine interface
 *
 * \ingroup applications
 * \note This module requires the Cepstral engine to be installed in
 * it's default location (/opt/swift)
 */

/*** MODULEINFO
        <defaultenabled>no</defaultenabled>
        <depend>swift</depend>
 ***/

#include "asterisk.h"
ASTERISK_FILE_VERSION(__FILE__, "$Revision: 303000 $")

#include <swift.h>
#if defined _SWIFT_VER_6
#include <swift_asterisk_interface.h>
#endif

#include <math.h>

#include "asterisk/astobj.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/file.h"

/*** DOCUMENTATION
        <application name="Swift" language="en_US">
                <synopsis>
                        Speak text through Swift text-to-speech engine (without writing files)
                        and optionally listen for DTMF.
                </synopsis>
                <syntax>
                        <parameter name="'text'" required="true"/>
                        <parameter name="options">
                                <optionlist>
                                        <option name="timeout">
                                                <para>Timeout in milliseconds.</para>
                                        </option>
                                        <option name="digits">
                                                <para>Maxiumum digits.</para>
                                        </option>
                                </optionlist>
                        </parameter>
                </syntax>
                <description>
                <para>This application streams tts audio from the Cepstral swift engine and
                will alternatively read DTMF into the ${SWIFT_DTMF} variable if the timeout
                and digits options are used.  You may change the voice dynamically by 
                setting the channel variable SWIFT_VOICE.</para>
                </description>
        </application>
 ***/

static char *app = "Swift";

#if (defined _AST_VER_1_4 || defined _AST_VER_1_6)
static char *synopsis = "Speak text through the Cepstral Swift text-to-speech engine.";
#endif

#if (defined _AST_VER_1_4 || defined _AST_VER_1_6)
static char *descrip = 
"This application streams tts audio from the Cepstral swift engine and\n"
"will alternatively read DTMF into the ${SWIFT_DTMF} variable if the timeout\n"
"and digits options are used.  You may change the voice dynamically by\n"
"setting the channel variable SWIFT_VOICE.\n\n"
" Syntax: Swift(text[|timeout in ms][|maximum digits])\n";
#endif

const int framesize = 20;

#define AST_MODULE "app_swift"
#define SWIFT_CONFIG_FILE "swift.conf"
#define dtmf_codes 12

static unsigned int cfg_buffer_size;
static int cfg_goto_exten;
static int samplerate;
static char cfg_voice[20];

struct stuff {
	ASTOBJ_COMPONENTS(struct stuff);
	int generating_done;
	char *q;
	char *pq_r;  /* queue read position */
	char *pq_w;  /* queue write position */
	int qc;
	int immediate_exit;
};

struct dtmf_lookup {
	long ast_res;
	char* dtmf_res;
};

static struct dtmf_lookup ast_dtmf_table[dtmf_codes] = {
	{35, "#"},
	{42, "*"},
	{48, "0"},
	{49, "1"},
	{50, "2"},
	{51, "3"},
	{52, "4"},
	{53, "5"},
	{54, "6"},
	{55, "7"},
	{56, "8"},
	{57, "9"}
};

static void swift_init_stuff(struct stuff *ps)
{
	ASTOBJ_INIT(ps);
	ps->generating_done = 0;
	ps->q = malloc(cfg_buffer_size);
	ps->pq_r = ps->q;
	ps->pq_w = ps->q;
	ps->qc = 0;
	ps->immediate_exit = 0;
}

static int swift_generator_running(struct stuff *ps)
{
	int r;
	ASTOBJ_RDLOCK(ps);
	r = !ps->immediate_exit && (!ps->generating_done || ps->qc);
	ASTOBJ_UNLOCK(ps);
	return r;
}

static int swift_bytes_available(struct stuff *ps)
{
	int r;
	ASTOBJ_RDLOCK(ps);
	r = ps->qc;
	ASTOBJ_UNLOCK(ps);
	return r;
}

static swift_result_t swift_cb(swift_event *event, swift_event_t type, void *udata)
{
	void *buf;
	int len, spacefree;
	unsigned long sleepfor;
	swift_event_t rv = SWIFT_SUCCESS;
	struct stuff *ps = udata;

	if (type == SWIFT_EVENT_AUDIO) {
		rv = swift_event_get_audio(event, &buf, &len);

		if (!SWIFT_FAILED(rv) && len > 0) {
			ast_log(LOG_DEBUG, "audio callback\n");
			ASTOBJ_WRLOCK(ps);

			/* Sleep while waiting for some queue space to become available */
			while (len + ps->qc > cfg_buffer_size && !ps->immediate_exit) {
				/* Each byte is 125us of time, so assume queue space will become available
				   at that rate and guess when we'll have enough space available.
				   + another (125 usec/sample * framesize samples) (1 frame) for fudge */
				sleepfor = ((unsigned long)(len - (cfg_buffer_size - ps->qc)) * 125UL) + (125UL * (unsigned long)framesize);
				/* ast_log(LOG_DEBUG, "generator: %d bytes to write but only %d space avail, sleeping %ldus\n", len, cfg_buffer_size - ps->qc, sleepfor); */
				ASTOBJ_UNLOCK(ps);
				usleep(sleepfor);
				ASTOBJ_WRLOCK(ps);
			}
			if (ps->immediate_exit) {
				ASTOBJ_UNLOCK(ps);
				return SWIFT_SUCCESS;
			}

			spacefree = cfg_buffer_size - ((uintptr_t) ps->pq_w - (uintptr_t)ps->q);

			if (len > spacefree) {
				ast_log(LOG_DEBUG, "audio fancy write; %d bytes but only %d avail to end %d totalavail\n", len, spacefree, cfg_buffer_size - ps->qc);

				/* write #1 to end of mem */
				memcpy(ps->pq_w, buf, spacefree);
				ps->pq_w = ps->q;
				ps->qc += spacefree;

				/* write #2 and beg of mem */
				memcpy(ps->pq_w, buf + spacefree, len - spacefree);
				ps->pq_w += len - spacefree;
				ps->qc += len - spacefree;
			} else {
				ast_log(LOG_DEBUG, "audio easy write, %d avail to end %d totalavail\n", spacefree, cfg_buffer_size - ps->qc);
				memcpy(ps->pq_w, buf, len);
				ps->pq_w += len;
				ps->qc += len;
			}

			ASTOBJ_UNLOCK(ps);
		} else {
			ast_log(LOG_DEBUG, "got audio callback but get_audio call failed\n");
		}
	} else if (type == SWIFT_EVENT_END) {
		ast_log(LOG_DEBUG, "got END callback; done generating audio\n");
		ASTOBJ_WRLOCK(ps);
		ps->generating_done = 1;
		ASTOBJ_UNLOCK(ps);
#if defined _SWIFT_VER_6
	} else if (type == SWIFT_EVENT_ERROR) {
		/* 
		 * Error events are used to communicate to app_swift that there are no more swift_ports available.
		 * So check to make sure that is the cause of the error signal, then terminate. 
		 * Termination may not be the best behavior, but any queuing should be managed on the Asterisk side.
		 */
		swift_result_t error_code;

		if ((swift_event_get_error(event, &error_code, NULL)==SWIFT_SUCCESS) && (error_code == SWIFT_PORT_UNAVAILABLE)) {
			ast_log(LOG_WARNING, "Received SWIFT_EVENT_ERROR with code: SWIFT_PORT_UNAVAILABLE.  There are no ports available for simultaneous synthesis.  All licensed ports are already in use.\n");
			ASTOBJ_WRLOCK(ps);
			ps->generating_done = 1;
			ASTOBJ_UNLOCK(ps);
		}
#endif
	} else {
		ast_log(LOG_DEBUG, "UNKNOWN callback\n");
	}
	return rv;
}

static int dtmf_conv(int dtmf)
{
	char *res = (char *) malloc(100);
	int dtmf_search_counter = 0, dtmf_search_match = 0;

	memset(res, 0, 100);

	while ((dtmf_search_counter < dtmf_codes) && (dtmf_search_match == 0)) {
		if (dtmf == ast_dtmf_table[dtmf_search_counter].ast_res) {
			dtmf_search_match += 1;
			sprintf(res, "%s", ast_dtmf_table[dtmf_search_counter].dtmf_res);
		}
		dtmf_search_counter = dtmf_search_counter + 1;
	}
	return *res;
}

static char *listen_for_dtmf(struct ast_channel *chan, int timeout, int max_digits)
{
	char *dtmf_conversion = (char *) malloc(100);
	char cnv[2];
	int dtmf = 0, i = 0;

	memset(dtmf_conversion, 0, 100);
	memset(cnv, 0, 2);

	while (i < max_digits) {
		dtmf = ast_waitfordigit(chan, timeout);

		if (!dtmf) {
			break;
		}

		sprintf(cnv, "%c", dtmf_conv(dtmf));
		strcat(dtmf_conversion, cnv);
		i += 1;
	}
	return strdup(dtmf_conversion);
}

#if (defined _AST_VER_1_4 || defined _AST_VER_1_6)
static int app_exec(struct ast_channel *chan, void *data)
#elif (defined _AST_VER_1_8 || defined _AST_VER_10 || defined _AST_VER_11)
static int app_exec(struct ast_channel *chan, const char *data)
#endif
{
	int res = 0, max_digits = 0, timeout = 0, alreadyran = 0;
	int ms, len, availatend;
	char *argv[3], *text = NULL, *rc = NULL;
	char tmp_exten[2], results[20];
	struct ast_module_user *u;
	struct ast_frame *f;
	struct timeval next;
	struct stuff *ps;
	char *parse;
#if (defined _AST_VER_10)
	struct ast_format old_writeformat;
#else
	int old_writeformat = 0;
#endif
	parse = ast_strdupa(data);

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(text);
		AST_APP_ARG(timeout);
		AST_APP_ARG(max_digits);
	);

	AST_STANDARD_APP_ARGS(args, parse);

	struct myframe {
		struct ast_frame f;
		unsigned char offset[AST_FRIENDLY_OFFSET];
		unsigned char frdata[framesize];
	} myf;

	swift_engine *engine;
	swift_port *port = NULL;
	swift_voice *voice;
	swift_params *params;
	swift_result_t sresult;
	swift_background_t tts_stream;
	unsigned int event_mask;
	const char *vvoice = NULL;

	memset(results, 0 ,20);
	memset(tmp_exten, 0, 2);
	memset(argv, 0, 3);

	u = ast_module_user_add(chan);

	if (!ast_strlen_zero(args.timeout)) {
		timeout = strtol(args.timeout, NULL, 0);
	}
	if (!ast_strlen_zero(args.max_digits)) {
		max_digits = strtol(args.max_digits, NULL, 0);
	}
	text = args.text;

	if (ast_strlen_zero(text)) {
		ast_log(LOG_WARNING, "%s requires text to speak!\n", app);
		return -1;
	}else{
		ast_log(LOG_DEBUG, "Text to Speak : %s\n", text);
	}
	if (timeout > 0) {
		ast_log(LOG_DEBUG, "Timeout : %d\n", timeout);
	}
	if (max_digits > 0) {
		ast_log(LOG_DEBUG, "Max Digits : %d\n", max_digits);
	}

	ps = malloc(sizeof(struct stuff));
	swift_init_stuff(ps);

	/* Setup synthesis */

	if ((engine = swift_engine_open(NULL)) == NULL) {
		ast_log(LOG_ERROR, "Failed to open Swift Engine.\n");
		goto exception;
	}

	params = swift_params_new(NULL);
	swift_params_set_string(params, "audio/encoding", "ulaw");
	swift_params_set_string(params, "audio/sampling-rate", "8000");
	swift_params_set_string(params, "audio/output-format", "raw");
	swift_params_set_string(params, "tts/text-encoding", "utf-8");

	/* Additional swift parameters
	 *
	 * swift_params_set_float(params, "speech/pitch/shift", 1.0);
	 * swift_params_set_int(params, "speech/rate", 150);
	 * swift_params_set_int(params, "audio/volume", 110);
	 * swift_params_set_int(params, "audio/deadair", 0);
	 */

	if ((port = swift_port_open(engine, params)) == NULL) {
		ast_log(LOG_ERROR, "Failed to open Swift Port.\n");
		goto exception;
	}

#if defined _SWIFT_VER_6
	if (port!=NULL) {
		/* 
		 * This registers a chan with swift, otherwise through repeated DTMF+synth requests
		 * a single call could consume all available concurrent synthesis ports.
                 */
		swift_register_ast_chan(port, chan);
	}
#endif

	/* allow exten => x,n,Set(SWIFT_VOICE=Callie) */
	if ((vvoice = pbx_builtin_getvar_helper(chan, "SWIFT_VOICE"))) {
		ast_copy_string(cfg_voice, vvoice, sizeof(cfg_voice));
		ast_log(LOG_DEBUG, "Config voice is %s via SWIFT_VOICE\n", cfg_voice);
	}

	if ((voice = swift_port_set_voice_by_name(port, cfg_voice)) == NULL) {
		ast_log(LOG_ERROR, "Failed to set voice.\n");
		goto exception;
	}


#if defined _SWIFT_VER_6
	event_mask = SWIFT_EVENT_AUDIO | SWIFT_EVENT_END | SWIFT_EVENT_ERROR;
#elif defined _SWIFT_VER_5
	event_mask = SWIFT_EVENT_AUDIO | SWIFT_EVENT_END;
#endif

	swift_port_set_callback(port, &swift_cb, event_mask, ps);

	if (SWIFT_FAILED(swift_port_speak_text(port, text, 0, NULL, &tts_stream, NULL))) {
		ast_log(LOG_ERROR, "Failed to speak.\n");
		goto exception;
	}
#if (defined _AST_VER_11)
	if (ast_channel_state(chan) != AST_STATE_UP) {
#else
	if (chan->_state != AST_STATE_UP) {
#endif
		ast_answer(chan);
	}

	ast_stopstream(chan);

#if (defined _AST_VER_1_4 || defined _AST_VER_1_6 || defined _AST_VER_1_8)
	old_writeformat = chan->writeformat;

	if (ast_set_write_format(chan, AST_FORMAT_ULAW) < 0) {
#elif defined _AST_VER_10
	ast_format_copy(&old_writeformat, &chan->writeformat);

	if (ast_set_write_format_by_id(chan, AST_FORMAT_ULAW) < 0) {
#elif (defined _AST_VER_11)
	ast_format_copy(&old_writeformat, ast_channel_writeformat(chan));

	if (ast_set_write_format_by_id(chan, AST_FORMAT_ULAW) < 0) {
#endif
		ast_log(LOG_WARNING, "Unable to set write format.\n");
		goto exception;
	}

	res = 0;

	/* Wait 100ms first for synthesis to start crankin'; if that's not
	 * enough the
	 */

	next = ast_tvadd(ast_tvnow(), ast_tv(0, 100000));

	while (swift_generator_running(ps)) {
		ms = ast_tvdiff_ms(next, ast_tvnow());

		if (ms <= 0) {
			if (swift_bytes_available(ps) > 0) {
				ASTOBJ_WRLOCK(ps);
				len = fmin(framesize, ps->qc);
				availatend = cfg_buffer_size - (ps->pq_r - ps->q);

				if (len > availatend) {
					/* read #1: to end of q buf */
					memcpy(myf.frdata, ps->pq_r, availatend);
					ps->qc -= availatend;

					/* read #2: reset to start of q buf and get rest */
					ps->pq_r = ps->q;
					memcpy(myf.frdata + availatend, ps->pq_r, len - availatend);
					ps->qc -= len - availatend;
					ps->pq_r += len - availatend;
				} else {
					ast_log(LOG_DEBUG, "Easy read; %d bytes and %d at end, %d free\n", len, availatend, cfg_buffer_size - ps->qc);
					memcpy(myf.frdata, ps->pq_r, len);
					ps->qc -= len;
					ps->pq_r += len;
				}

				myf.f.frametype = AST_FRAME_VOICE;
#if (defined _AST_VER_1_6 || defined _AST_VER_1_4)
				myf.f.subclass = AST_FORMAT_ULAW;
#elif defined _AST_VER_1_8 
				myf.f.subclass.codec = AST_FORMAT_ULAW;
#elif (defined _AST_VER_10 || defined _AST_VER_11)
				ast_format_set(&myf.f.subclass.format, AST_FORMAT_ULAW, 0);
#endif
				myf.f.datalen = len;
				myf.f.samples = len;
#if defined _AST_VER_1_4
				myf.f.data = myf.frdata;
#else
				myf.f.data.ptr = myf.frdata;
#endif
				myf.f.mallocd = 0;
				myf.f.offset = AST_FRIENDLY_OFFSET;
				myf.f.src = __PRETTY_FUNCTION__;
				myf.f.delivery.tv_sec = 0;
				myf.f.delivery.tv_usec = 0;

				if (ast_write(chan, &myf.f) < 0) {
					ast_log(LOG_DEBUG, "ast_write failed\n");
				}

				ast_log(LOG_DEBUG, "wrote a frame of %d\n", len);

				if (ps->qc < 0) {
					ast_log(LOG_DEBUG, "queue claims to contain negative bytes. Huh? qc < 0\n");
				}

				ASTOBJ_UNLOCK(ps);
				next = ast_tvadd(next, ast_samp2tv(myf.f.samples, samplerate));
			} else {
				next = ast_tvadd(next, ast_samp2tv(framesize / 2, samplerate));
				ast_log(LOG_DEBUG, "Whoops, writer starved for audio\n");
			}
		} else {
			ms = ast_waitfor(chan, ms);

			if (ms < 0) {
				ast_log(LOG_DEBUG, "Hangup detected\n");
				res = -1;
				ASTOBJ_WRLOCK(ps);
				ps->immediate_exit = 1;
				ASTOBJ_UNLOCK(ps);
			} else if (ms) {
				f = ast_read(chan);

				if (!f) {
					ast_log(LOG_DEBUG, "Null frame == hangup() detected\n");
					res = -1;
					ASTOBJ_WRLOCK(ps);
					ps->immediate_exit = 1;
					ASTOBJ_UNLOCK(ps);
				} else {
					if (f->frametype == AST_FRAME_DTMF && timeout > 0 && max_digits > 0) {
#if (defined _AST_VER_1_6 || defined _AST_VER_1_4)
						char originalDTMF = f->subclass;
#elif (defined _AST_VER_1_8 || defined _AST_VER_10 || defined _AST_VER_11)
						char originalDTMF = f->subclass.integer;
#endif
						alreadyran = 1;
						res = 0;
						ASTOBJ_WRLOCK(ps);
						ps->immediate_exit = 1;
						ASTOBJ_UNLOCK(ps);

						if (max_digits > 1) {
							rc = listen_for_dtmf(chan, timeout, max_digits - 1);
						}

						if (rc) {
							sprintf(results, "%c%s", originalDTMF, rc);
						} else {
							sprintf(results, "%c", originalDTMF);
						}

						ast_log(LOG_NOTICE, "DTMF = %s\n", results);
						pbx_builtin_setvar_helper(chan, "SWIFT_DTMF", results);
					}

					if (f!=NULL) {
						ast_frfree(f);
					}
				}
			}
		}

		ASTOBJ_RDLOCK(ps);

		if (ps->immediate_exit && !ps->generating_done) {
			if (SWIFT_FAILED(sresult = swift_port_stop(port, tts_stream, SWIFT_EVENT_NOW))) {
				ast_log(LOG_NOTICE, "Early top of swift port failed\n");
			}
		}

		ASTOBJ_UNLOCK(ps);
	}
	if (alreadyran == 0 && timeout > 0 && max_digits > 0) {
		rc = listen_for_dtmf(chan, timeout, max_digits);

		if (rc != NULL) {
			sprintf(results, "%s", rc);
			ast_log(LOG_NOTICE, "DTMF = %s\n", results);
			pbx_builtin_setvar_helper(chan, "SWIFT_DTMF", results);
		}
	}
	if (max_digits >= 1 && results != NULL) {
		if (cfg_goto_exten) {
#if (defined _AST_VER_11 ||)
			ast_log(LOG_NOTICE, "GoTo(%s|%s|%d) : ", ast_channel_context(chan), results, 1);
#else
			ast_log(LOG_NOTICE, "GoTo(%s|%s|%d) : ", chan->context, results, 1);
#endif

#if (defined _AST_VER_1_6 || defined _AST_VER_1_4)
			if (ast_exists_extension (chan, chan->context, results, 1, chan->cid.cid_num)) {
#elif (defined _AST_VER_1_8 || defined _AST_VER_10)
			 if (ast_exists_extension (chan, chan->context, results, 1, chan->caller.id.number.str)) {
#elif (defined _AST_VER_11)
			 if (ast_exists_extension (chan, ast_channel_context(chan), results, 1, ast_channel_caller(chan)->id.number.str)) {
#endif
				ast_log(LOG_NOTICE, "OK\n");
#if (defined _AST_VER_11)
				ast_channel_exten_set(chan, results);
				ast_channel_priority_set(chan, 0);
#else
				ast_copy_string(chan->exten, results, sizeof(chan->exten) - 1);
				chan->priority = 0;
#endif
			} else {
				ast_log(LOG_NOTICE, "FAILED\n");
			}
		}
	}

	exception:

	if (port != NULL) {
		swift_port_close(port);
	}
	if (engine != NULL) {
		swift_engine_close(engine);
	}
	if (ps && ps->q) {
		ast_free(ps->q);
		ps->q = NULL;
	}
	if (ps) {
		ast_free(ps);
		ps = NULL;
	}
#if (defined _AST_VER_1_6 || defined _AST_VER_1_4 || defined _AST_VER_1_8)
	if (!res && old_writeformat) {
		ast_set_write_format(chan, old_writeformat);
	}
#elif (defined _AST_VER_10 || defined _AST_VER_11)
	if (!res) {
		ast_set_write_format(chan, &old_writeformat);
	}
#endif
	ast_module_user_remove(u);
	return res;
}


static int unload_module(void)
{
	int res;
	res = ast_unregister_application(app);
	ast_module_user_hangup_all();
	return res;
}


static int load_module(void)
{
	int res = 0;
	const char *val = NULL;
	struct ast_config *cfg;
#if  (defined _AST_VER_1_6 || defined _AST_VER_1_8 || defined _AST_VER_10 || defined _AST_VER_11)
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };
#endif

	/* Set some defaults */
	cfg_buffer_size = 65535;
	cfg_goto_exten = 0;
	samplerate = 8000; /* G711a/G711u  */

	ast_copy_string(cfg_voice, "Allison-8kHz", sizeof(cfg_voice));


#if (defined _AST_VER_1_6 || defined _AST_VER_1_4)
	res = ast_register_application(app, app_exec, synopsis, descrip) ?
#elif (defined _AST_VER_1_8 || defined _AST_VER_10 || defined _AST_VER_11)
	res = ast_register_application_xml(app, app_exec) ?
#endif
		AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;

#if defined _AST_VER_1_4
	cfg = ast_config_load(SWIFT_CONFIG_FILE);
#elif (defined _AST_VER_1_6 || defined _AST_VER_1_8 || defined _AST_VER_10 || defined _AST_VER_11)
	cfg = ast_config_load(SWIFT_CONFIG_FILE, config_flags);
#endif

	if (cfg) {
		if ((val = ast_variable_retrieve(cfg, "general", "buffer_size"))) {
			cfg_buffer_size = atoi(val);
			ast_log(LOG_DEBUG, "Config buffer_size is %d\n", cfg_buffer_size);
		}
		if ((val = ast_variable_retrieve(cfg, "general", "goto_exten"))) {
			if (!strcmp(val, "yes")) {
				cfg_goto_exten = 1;
			} else {
				cfg_goto_exten = 0;
				ast_log(LOG_DEBUG, "Config goto_exten is %d\n", cfg_goto_exten);
			}
		}
		if ((val = ast_variable_retrieve(cfg, "general", "voice"))) {
			ast_copy_string(cfg_voice, val, sizeof(cfg_voice));
			ast_log(LOG_DEBUG, "Config voice is %s\n", cfg_voice);
		}

		ast_config_destroy(cfg);
	} else {
		ast_log(LOG_NOTICE, "Failed to load config\n");
	}

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Cepstral Swift TTS Application");
