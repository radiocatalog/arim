/***********************************************************************

    ARIM Amateur Radio Instant Messaging program for the ARDOP TNC.

    Copyright (C) 2016, 2017, 2018 Robert Cunnings NW8L

    This file is part of the ARIM messaging program.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "main.h"
#include "arim_proto.h"
#include "cmdproc.h"
#include "ui.h"
#include "util.h"
#include "log.h"
#include "arim_arq.h"
#include "arim_arq_files.h"
#include "arim_arq_msg.h"
#include "arim_arq_auth.h"

static int arq_rpts, arq_count, arq_cmd_size;
static char cached_cmd[MAX_CMD_SIZE];

int arim_arq_send_conn_req(const char *repeats, const char *to_call)
{
    char tcall[TNC_MYCALL_SIZE], buffer[MAX_LOG_LINE_SIZE];
    size_t i, len;

    if (!g_tnc_attached || !arim_is_idle())
        return 0;
    /* force call to uppercase */
    len = strlen(to_call);
    for (i = 0; i < len; i++)
        tcall[i] = toupper(to_call[i]);
    tcall[i] = '\0';
    /* cache remote call and repeat count */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].arq_remote_call,
        sizeof(g_tnc_settings[g_cur_tnc].arq_remote_call), "%s", tcall);
    pthread_mutex_unlock(&mutex_tnc_set);
    if (repeats)
        arq_rpts = atoi(repeats);
    else
        arq_rpts = 10;
    arq_count = 0;
    /* are pilot pings needed first? */
    if (atoi(g_arim_settings.pilot_ping)) {
        snprintf(prev_to_call, sizeof(prev_to_call), "%s", to_call);
        arim_on_event(EV_ARQ_CONNECT_PP, 0);
        return 1;
    }
    /* change state */
    arim_on_event(EV_ARQ_CONNECT, 0);
    snprintf(buffer, sizeof(buffer), "ARQCALL %s %d", tcall, arq_rpts);
    ui_queue_cmd_out(buffer);
    return 1;
}

int arim_arq_send_conn_req_pp()
{
    char buffer[MAX_LOG_LINE_SIZE];

    /* change state */
    arim_on_event(EV_ARQ_CONNECT, 0);
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(buffer, sizeof(buffer), "ARQCALL %s %d",
                g_tnc_settings[g_cur_tnc].arq_remote_call, arq_rpts);
    pthread_mutex_unlock(&mutex_tnc_set);
    ui_queue_cmd_out(buffer);
    return 1;
}

int arim_arq_send_conn_req_ptt(int ptt_true)
{
    char buffer[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    char mycall[TNC_MYCALL_SIZE], tcall[TNC_MYCALL_SIZE];

    if (!ptt_true)
        return 1;
    /* if ptt true in connect wait state then
       print to monitor view and traffic log */
    arim_copy_mycall(mycall, sizeof(mycall));
    arim_copy_remote_call(tcall, sizeof(tcall));
    ++arq_count;
    if (arq_count > arq_rpts) {
        /* no joy, abandon connection attempt */
        return 0;
    }
    snprintf(buffer, sizeof(buffer), "<< [@] %s>%s (Connect request %d of %d)",
                 mycall, tcall, arq_count, arq_rpts);
    ui_queue_traffic_log(buffer);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        snprintf(buffer, sizeof(buffer), "[%s] << [@] %s>%s (Connect request %d of %d)",
                util_timestamp(timestamp, sizeof(timestamp)),
                    mycall, tcall, arq_count, arq_rpts);
    }
    ui_queue_data_in(buffer);
    return 1;
}

int arim_arq_on_target()
{
    char buffer[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    char target_call[TNC_MYCALL_SIZE];

    /* we are the target of an incoming ARQ connect request so
       print to monitor view and traffic log */
    arim_copy_target_call(target_call, sizeof(target_call));
    /* populate remote call with placeholder value for display
       purposes until CONNECTED async response is received */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].arq_remote_call,
        sizeof(g_tnc_settings[g_cur_tnc].arq_remote_call), "%s", "?????");
    pthread_mutex_unlock(&mutex_tnc_set);
    snprintf(buffer, sizeof(buffer), ">> [@] %s>%s (Connect request)",
                g_tnc_settings[g_cur_tnc].arq_remote_call, target_call);
    ui_queue_traffic_log(buffer);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        pthread_mutex_lock(&mutex_tnc_set);
        snprintf(buffer, sizeof(buffer), "[%s] >> [@] %s>%s (Connect request)",
                util_timestamp(timestamp, sizeof(timestamp)),
                    g_tnc_settings[g_cur_tnc].arq_remote_call, target_call);
        pthread_mutex_unlock(&mutex_tnc_set);
    }
    ui_queue_data_in(buffer);
    return 1;
}

int arim_arq_on_connected()
{
    char buffer[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    char remote_call[TNC_MYCALL_SIZE], target_call[TNC_MYCALL_SIZE];

    /* we are connected to a remote station now so
       print to monitor view and traffic log */
    arim_copy_target_call(target_call, sizeof(target_call));
    if (!strlen(target_call))
        arim_copy_mycall(target_call, sizeof(target_call));
    arim_copy_remote_call(remote_call, sizeof(remote_call));
    snprintf(buffer, sizeof(buffer),
                ">> [@] %s>%s (Connected)", remote_call, target_call);
    ui_queue_traffic_log(buffer);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        snprintf(buffer, sizeof(buffer),
                "[%s] >> [@] %s>%s (Connected, Press Spacebar to type, CTRL-X to disconnect)",
                util_timestamp(timestamp, sizeof(timestamp)), remote_call, target_call);
    }
    ui_queue_data_in(buffer);
    snprintf(buffer, sizeof(buffer), "7[@] %-10s ", remote_call);
    ui_queue_heard(buffer);
    show_recents = show_ptable = 0; /* close recents or ping history view if open */
    arq_cmd_size = 0; /* reset ARQ command size */
    arim_arq_auth_set_status(0); /* reset sesson authenticated status */
    return 1;
}

int arim_arq_send_disconn_req()
{
    char buffer[MAX_LOG_LINE_SIZE];
    char remote_call[TNC_MYCALL_SIZE], target_call[TNC_MYCALL_SIZE];

    arim_copy_target_call(target_call, sizeof(target_call));
    if (!strlen(target_call))
        arim_copy_mycall(target_call, sizeof(target_call));
    arim_copy_remote_call(remote_call, sizeof(remote_call));
    snprintf(buffer, sizeof(buffer),
                "%s<%s (Disconnect request)", remote_call, target_call);
    ui_queue_data_out(buffer);
    ui_queue_cmd_out("DISCONNECT");
    return 1;
}

int arim_arq_on_disconnected()
{
    char buffer[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    char remote_call[TNC_MYCALL_SIZE], target_call[TNC_MYCALL_SIZE];

    /* we are disconnected from the remote station now so
       print to monitor view and traffic log */
    arim_copy_target_call(target_call, sizeof(target_call));
    if (!strlen(target_call))
        arim_copy_mycall(target_call, sizeof(target_call));
    arim_copy_remote_call(remote_call, sizeof(remote_call));
    snprintf(buffer, sizeof(buffer),
                ">> [@] %s>%s (Disconnected)",
                remote_call, target_call);
    ui_queue_traffic_log(buffer);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        snprintf(buffer, sizeof(buffer),
                "[%s] >> [@] %s>%s (Disconnected)",
                util_timestamp(timestamp, sizeof(timestamp)), remote_call, target_call);
    }
    ui_queue_data_in(buffer);
    snprintf(buffer, sizeof(buffer), "7[@] %-10s ", remote_call);
    ui_queue_heard(buffer);
    arq_cmd_size = 0; /* reset ARQ command size */
    arim_arq_auth_set_status(0); /* reset sesson authenticated status */
    return 1;
}

int arim_arq_on_conn_timeout()
{
    char buffer[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    char remote_call[TNC_MYCALL_SIZE], target_call[TNC_MYCALL_SIZE];

    /* connection to remote station has timed out,
       print to monitor view and traffic log */
    arim_copy_target_call(target_call, sizeof(target_call));
    if (!strlen(target_call))
        arim_copy_mycall(target_call, sizeof(target_call));
    arim_copy_remote_call(remote_call, sizeof(remote_call));
    snprintf(buffer, sizeof(buffer),
                ">> [@] %s>%s (Link timeout, disconnected)",
                remote_call, target_call);
    ui_queue_traffic_log(buffer);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        snprintf(buffer, sizeof(buffer),
                "[%s] >> [@] %s>%s (Link timeout, disconnected)",
                util_timestamp(timestamp, sizeof(timestamp)), remote_call, target_call);
    }
    ui_queue_data_in(buffer);
    arq_cmd_size = 0; /* reset ARQ command size */
    arim_arq_auth_set_status(0); /* reset sesson authenticated status */
    return 1;
}

size_t arim_arq_send_remote(const char *msg)
{
    char sendcr[TNC_ARQ_SENDCR_SIZE], linebuf[MAX_LOG_LINE_SIZE];

    /* check to see if CR must be sent */
    arim_copy_arq_sendcr(sendcr, sizeof(sendcr));
    if (!strncasecmp(sendcr, "TRUE", 4))
        snprintf(linebuf, sizeof(linebuf), "%s\r\n", msg);
    else
        snprintf(linebuf, sizeof(linebuf), "%s\n", msg);
    ui_queue_data_out(linebuf);
    return strlen(linebuf);
}

size_t arim_arq_on_cmd(const char *cmd, size_t size)
{
    /* called by datathread via arim_arq_on_data() */
    static char buffer[MIN_MSG_BUF_SIZE*4];
    static size_t cnt = 0;
    char *e, *eol, respbuf[MIN_MSG_BUF_SIZE], cmdbuf[MIN_MSG_BUF_SIZE];
    char sendcr[TNC_ARQ_SENDCR_SIZE], linebuf[MAX_LOG_LINE_SIZE];
    char timestamp[MAX_TIMESTAMP_SIZE];
    int state, result, send_cr = 0;

    state = arim_get_state();
    if (cmd && cmd[0] == '/') {
        if (size > sizeof(cmdbuf))
            return 0;
        memcpy(cmdbuf, cmd, size);
        cmdbuf[size] = '\0';
        /* check to see if CR must be sent */
        arim_copy_arq_sendcr(sendcr, sizeof(sendcr));
        if (!strncasecmp(sendcr, "TRUE", 4))
            send_cr = 1;
        /* strip EOL from command */
        eol = cmdbuf;
        while (*eol && *eol != '\n' && *eol != '\r')
            ++eol;
        if (*eol && *eol == '\r' && *(eol + 1) == '\n') {
            *eol++ = '\0';
            *eol++ = '\0';
        } else if (*eol && *eol == '\n') {
            *eol++ = '\0';
        } else {
            eol = 0;
        }
        /* print command to traffic monitor */
        snprintf(linebuf, sizeof(linebuf), ">> [@] %s", cmdbuf);
        ui_queue_traffic_log(linebuf);
        if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
            snprintf(linebuf, sizeof(linebuf), "[%s] >> [@] %s",
                util_timestamp(timestamp, sizeof(timestamp)), cmdbuf);
        }
        ui_queue_data_in(linebuf);
        if (!strncasecmp(cmdbuf, "/FPUT ", 6)) {
            /* remote station sends a file. If in a wait state already,
               abandon that transaction and respond to the /fput to avoid
               deadlock when commands are issued by both parties simultaneously */
            switch (state) {
            case ST_ARQ_FILE_SEND_WAIT:
            case ST_ARQ_FILE_SEND_WAIT_OK:
            case ST_ARQ_AUTH_RCV_A2_WAIT:
            case ST_ARQ_AUTH_RCV_A3_WAIT:
            case ST_ARQ_MSG_SEND_WAIT:
                arim_on_event(EV_ARQ_CANCEL_WAIT, 0);
                state = arim_get_state();
                break;
            }
            switch (state) {
            case ST_ARQ_AUTH_RCV_A4_WAIT:
                /* /FPUT implies remote stn accepted our /A3, auth successful */
                arim_arq_auth_on_ok();
                /* fallthrough intentional */
            case ST_ARQ_CONNECTED:
                arim_arq_files_on_fput(cmdbuf, size, eol, ARQ_SERVER_STN);
                break;
            case ST_ARQ_FILE_RCV_WAIT:
                arim_arq_files_on_fput(cmdbuf, size, eol, ARQ_CLIENT_STN);
                break;
            }
        } else if (!strncasecmp(cmdbuf, "/FGET ", 6)) {
            /* remote station requests a file. If in a wait state already,
               abandon that transaction and respond to the /fget to avoid
               deadlock when commands are issued by both parties simultaneously */
            switch (state) {
            case ST_ARQ_FILE_SEND_WAIT:
            case ST_ARQ_FILE_SEND_WAIT_OK:
            case ST_ARQ_FILE_RCV_WAIT:
            case ST_ARQ_FILE_RCV_WAIT_OK:
            case ST_ARQ_AUTH_RCV_A2_WAIT:
            case ST_ARQ_AUTH_RCV_A3_WAIT:
            case ST_ARQ_MSG_SEND_WAIT:
                arim_on_event(EV_ARQ_CANCEL_WAIT, 0);
                state = arim_get_state();
                break;
            }
            switch (state) {
            case ST_ARQ_AUTH_RCV_A4_WAIT:
                /* /FGET implies remote stn accepted our /A3, auth successful */
                arim_arq_auth_on_ok();
                /* fallthrough intentional */
            case ST_ARQ_CONNECTED:
                arim_arq_files_on_fget(cmdbuf, size, eol);
                break;
            }
        } else if (!strncasecmp(cmdbuf, "/MPUT ", 6)) {
            /* remote station sends a message. If in a wait state already,
               abandon that transaction and respond to the /mput to avoid
               deadlock when commands are issued by both parties simultaneously */
            switch (state) {
            case ST_ARQ_FILE_SEND_WAIT:
            case ST_ARQ_FILE_SEND_WAIT_OK:
            case ST_ARQ_FILE_RCV_WAIT:
            case ST_ARQ_FILE_RCV_WAIT_OK:
            case ST_ARQ_AUTH_RCV_A2_WAIT:
            case ST_ARQ_AUTH_RCV_A3_WAIT:
            case ST_ARQ_MSG_SEND_WAIT:
                arim_on_event(EV_ARQ_CANCEL_WAIT, 0);
                state = arim_get_state();
                break;
            }
            switch (state) {
            case ST_ARQ_CONNECTED:
                arim_arq_msg_on_mput(cmdbuf, size, eol);
                break;
            }
        } else if (!strncasecmp(cmdbuf, "/A1", 3)) {
            /* remote station sends a authentication challenge. This can only be
               in response to a command recently sent by the local station which
               references a authenticated file directory at the remote station. */
            switch (state) {
            case ST_ARQ_CONNECTED:
            case ST_ARQ_FILE_RCV_WAIT:
            case ST_ARQ_FILE_SEND_WAIT_OK:
                arim_arq_auth_on_a1(cmdbuf, size, eol);
                break;
            }
        } else if (!strncasecmp(cmdbuf, "/A2", 3)) {
            /* remote station sends a response to an /A1 authentication challenge
               recently send by the local station. This response is itself a challenge
               and it includes a nonce. */
            if (state == ST_ARQ_AUTH_RCV_A2_WAIT)
                arim_arq_auth_on_a2(cmdbuf, size, eol);
        } else if (!strncasecmp(cmdbuf, "/A3", 3)) {
            /* remote station sends a response to an /A2 authentication challenge
               recently send by the local station. */
            if (state == ST_ARQ_AUTH_RCV_A3_WAIT)
                arim_arq_auth_on_a3(cmdbuf, size, eol);
        } else if (!strncasecmp(cmdbuf, "/AUTH", 5)) {
            /* remote station requests that we send an /A1 authentication challenge */
            if (state == ST_ARQ_CONNECTED)
                arim_arq_auth_on_challenge(cmdbuf, size, eol);
        } else if (!strncasecmp(cmdbuf, "/ERROR", 6)) {
            switch (state) {
            case ST_ARQ_FILE_RCV:
            case ST_ARQ_FILE_RCV_WAIT:
            case ST_ARQ_FILE_SEND:
            case ST_ARQ_FILE_SEND_WAIT_OK:
                arim_on_event(EV_ARQ_FILE_ERROR, 0);
                break;
            case ST_ARQ_MSG_RCV:
            case ST_ARQ_MSG_SEND:
                arim_on_event(EV_ARQ_MSG_ERROR, 0);
                break;
            }
        } else if (!strncasecmp(cmdbuf, "/EAUTH", 6)) {
            switch (state) {
            case ST_ARQ_FILE_RCV_WAIT:
            case ST_ARQ_FILE_SEND_WAIT_OK:
            case ST_ARQ_AUTH_RCV_A2_WAIT:
            case ST_ARQ_AUTH_RCV_A3_WAIT:
            case ST_ARQ_AUTH_RCV_A4_WAIT:
            case ST_ARQ_CONNECTED:
                arim_on_event(EV_ARQ_AUTH_ERROR, 0);
                break;
            }
        } else if (!strncasecmp(cmdbuf, "/OK", 3)) {
            switch (state) {
            case ST_ARQ_FILE_SEND:
            case ST_ARQ_FILE_SEND_WAIT_OK:
                arim_on_event(EV_ARQ_FILE_OK, 0);
                break;
            case ST_ARQ_MSG_SEND:
                arim_on_event(EV_ARQ_MSG_OK, 0);
                arim_arq_msg_on_ok();
                break;
            case ST_ARQ_AUTH_RCV_A4_WAIT:
                arim_on_event(EV_ARQ_AUTH_OK, 0);
                break;
            }
        } else {
            switch (state) {
            case ST_ARQ_AUTH_RCV_A4_WAIT:
                /* receipt of cmd implies remote stn accepted our /A3, auth successful */
                arim_on_event(EV_ARQ_AUTH_OK, 0);
                /* fallthrough intentional */
            case ST_ARQ_CONNECTED:
                /* empty outbound data buffer before handling query or command */
                while (arim_get_buffer_cnt() > 0)
                    sleep(1);
                /* execute query or command, skip leading '/' character */
                result = cmdproc_query(cmdbuf + 1, respbuf, sizeof(respbuf));
                if (result == CMDPROC_OK) {
                    /* success, append response to data out buffer */
                    size = strlen(respbuf);
                    if ((cnt + size) >= sizeof(buffer)) {
                        /* overflow, reset buffer and return */
                        cnt = 0;
                        return cnt;
                    }
                    strncat(&buffer[cnt], respbuf, size);
                    cnt += size;
                } else if (result == CMDPROC_FILE_ERR) {
                    /* file access error */
                    size = strlen("/ERROR File not found");
                    if ((cnt + size) >= sizeof(buffer)) {
                        /* overflow, reset buffer and return */
                        cnt = 0;
                        return cnt;
                    }
                    strncat(&buffer[cnt], "/ERROR File not found", size);
                    cnt += size;
                } else if (result == CMDPROC_DIR_ERR) {
                    /* directory access error */
                    size = strlen("/ERROR Directory not found");
                    if ((cnt + size) >= sizeof(buffer)) {
                        /* overflow, reset buffer and return */
                        cnt = 0;
                        return cnt;
                    }
                    strncat(&buffer[cnt], "/ERROR Directory not found", size);
                    cnt += size;
                } else if (result == CMDPROC_AUTH_REQ) {
                    /* authentication required */
                    cnt = 0;
                    return cnt;
                } else if (result == CMDPROC_AUTH_ERR) {
                    /* authentication error */
                    size = strlen("/EAUTH");
                    if ((cnt + size) >= sizeof(buffer)) {
                        /* overflow, reset buffer and return */
                        cnt = 0;
                        return cnt;
                    }
                    strncat(&buffer[cnt], "/EAUTH", size);
                    cnt += size;
                } else {
                    /* no such query defined */
                    size = strlen("/ERROR Unknown query");
                    if ((cnt + size) >= sizeof(buffer)) {
                        /* overflow, reset buffer and return */
                        cnt = 0;
                        return cnt;
                    }
                    strncat(&buffer[cnt], "/ERROR Unknown query", size);
                    cnt += size;
                }
                break;
            }
        }
     } else if (cnt) {
        /* send data out one line at a time */
        e = buffer;
        if (*e) {
            while (*e && *e != '\n')
                ++e;
            if (*e) {
                *e = '\0';
                ++e;
            }
            if (send_cr)
                snprintf(respbuf, sizeof(respbuf), "%s\r\n", buffer);
            else
                snprintf(respbuf, sizeof(respbuf), "%s\n", buffer);
            ui_queue_data_out(respbuf);
            cnt -= (e - buffer);
            memmove(buffer, e, cnt + 1);
        }
    }
    return cnt;
}

size_t arim_arq_on_resp(const char *resp, size_t size)
{
    static char buffer[MIN_MSG_BUF_SIZE*4];
    static size_t cnt = 0;
    char *e, linebuf[MIN_MSG_BUF_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    size_t i, len;

    if (resp) {
        /* append response to buffer */
        if ((cnt + size) >= sizeof(buffer)) {
            /* overflow, reset buffer and return */
            cnt = 0;
            return cnt;
        }
        strncat(&buffer[cnt], resp, size);
        cnt += size;
     } else if (cnt) {
        e = buffer;
        while (*e && *e != '\r' && *e != '\n')
            ++e;
        if (!*e)
            return cnt;
        if (*e == '\r' && *(e + 1) == '\n') {
            *e = '\0';
            ++e;
        }
        *e = '\0';
        ++e;
        len = strlen(buffer);
        for (i = 0; i < len; i++) {
            if (!isprint(buffer[i]))
                buffer[i] = ' ';
        }
        snprintf(linebuf, sizeof(linebuf), ">> [@] %s", buffer);
        ui_queue_traffic_log(linebuf);
        if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
            snprintf(linebuf, sizeof(linebuf), "[%s] >> [@] %s",
                    util_timestamp(timestamp, sizeof(timestamp)), buffer);
        }
        ui_queue_data_in(linebuf);
        cnt -= (e - buffer);
        memmove(buffer, e, cnt + 1);
    }
    return cnt;
}

int arim_arq_on_data(char *data, size_t size)
{
    /* called by datathread */
    static char cmdbuffer[MAX_CMD_SIZE+2];
    char *s, *e, linebuf[MAX_LOG_LINE_SIZE], remote_call[TNC_MYCALL_SIZE];

    arim_copy_remote_call(remote_call, sizeof(remote_call));
    snprintf(linebuf, sizeof(linebuf), "7[@] %-10s ", remote_call);
    ui_queue_heard(linebuf);
    /* pass to command or response handler */
    if (!arq_cmd_size && data[0] == '/') {
        /* start of command; a complete command is newline terminated */
        s = data;
        e = data + size;
        while (*s != '\n' && s < e)
            ++s;
        if (s == e) {
            /* incomplete, cache and wait for more data */
            if (size < sizeof(cmdbuffer)) {
                memcpy(cmdbuffer, data, size);
                arq_cmd_size = size;
                ui_queue_debug_log("Data thread: incomplete ARQ command, buffering");
            } else {
                ui_queue_debug_log("Data thread: ARQ command too large");
            }
            return 0;
        } else {
            /* complete, process the command */
            ui_queue_debug_log("Data thread: processing ARQ command");
            arim_arq_on_cmd(data, size);
        }
    } else if (arq_cmd_size) {
        if (arq_cmd_size + size < sizeof(cmdbuffer)) {
            memcpy(cmdbuffer + arq_cmd_size, data, size);
            /* a complete command is newline terminated */
            s = cmdbuffer + arq_cmd_size;
            arq_cmd_size += size;
            e = cmdbuffer + arq_cmd_size;
            while (*s != '\n' && s < e)
                ++s;
            if (s == e) {
                ui_queue_debug_log("Data thread: incomplete ARQ command, buffering");
                return 0;
            } else {
                /* complete, process the command */
                ui_queue_debug_log("Data thread: ARQ command completed, processing");
                arim_arq_on_cmd(cmdbuffer, arq_cmd_size);
                arq_cmd_size = 0;
            }
        } else {
            arq_cmd_size = 0;
            ui_queue_debug_log("Data thread: ARQ command too large");
            return 0;
        }
    } else {
        /* not a command */
        arim_arq_on_resp(data, size);
    }
    return 1;
}

void arim_arq_run_cached_cmd()
{
    char cmdbuffer[MAX_CMD_SIZE], linebuf[MAX_LOG_LINE_SIZE];

    snprintf(cmdbuffer, sizeof(cmdbuffer), "%s", cached_cmd);
    cmdproc_cmd(cmdbuffer);
    snprintf(linebuf, sizeof(linebuf), "ARQ: Running cached command '%s'", cached_cmd);
    ui_queue_debug_log(linebuf);
}

void arim_arq_cache_cmd(const char *cmd)
{
    char linebuf[MAX_LOG_LINE_SIZE];

    snprintf(cached_cmd, sizeof(cached_cmd), "%s", cmd);
    snprintf(linebuf, sizeof(linebuf), "ARQ: Caching command '%s'", cached_cmd);
    ui_queue_debug_log(linebuf);
}

