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

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "main.h"
#include "arim_beacon.h"
#include "arim_ping.h"
#include "arim_proto.h"
#include "bufq.h"
#include "ini.h"
#include "log.h"
#include "ui.h"
#include "util.h"

void cmdthread_queue_debug_log(const char *text)
{
    char buffer[MAX_CMD_SIZE];
    char timestamp[MAX_TIMESTAMP_SIZE];

    if (g_debug_log_enable) {
        snprintf(buffer, sizeof(buffer), "[%s] %s",
                util_timestamp_usec(timestamp, sizeof(timestamp)), text);
        pthread_mutex_lock(&mutex_debug_log);
        cmdq_push(&g_debug_log_q, buffer);
        pthread_mutex_unlock(&mutex_debug_log);
    }
}

void cmdthread_queue_cmd_in(const char *text)
{
    pthread_mutex_lock(&mutex_cmd_in);
    cmdq_push(&g_cmd_in_q, text);
    pthread_mutex_unlock(&mutex_cmd_in);
}

void cmdthread_queue_cmd_out(const char *text)
{
    char inbuffer[MAX_CMD_SIZE];

    snprintf(inbuffer, sizeof(inbuffer), "%s\r", text);
    pthread_mutex_lock(&mutex_cmd_out);
    cmdq_push(&g_cmd_out_q, text);
    pthread_mutex_unlock(&mutex_cmd_out);
}

void cmdthread_next_cmd_out(int sock)
{
    char *cmd, inbuffer[MAX_CMD_SIZE];
    int sent;

    pthread_mutex_lock(&mutex_cmd_out);
    cmd = cmdq_pop(&g_cmd_out_q);
    pthread_mutex_unlock(&mutex_cmd_out);
    if (cmd) {
        snprintf(inbuffer, sizeof(inbuffer), "%s\r", cmd);
        sent = write(sock, inbuffer, strlen(inbuffer));
        if (sent < 0) {
            cmdthread_queue_debug_log("Cmd thread: write to socket failed");
        } else {
            snprintf(inbuffer, sizeof(inbuffer), "<< %s", cmd);
            cmdthread_queue_cmd_in(inbuffer);
            cmdthread_queue_debug_log(inbuffer);
        }
    }
}

size_t cmdthread_proc_response(char *response, size_t size, int sock)
{
    static char buffer[MAX_CMD_SIZE*3];
    char inbuffer[MAX_CMD_SIZE];
    static size_t cnt = 0;
    int quit = 0;
    char *end, *start, *val;

    if ((cnt + size) > sizeof(buffer)) {
        cnt = 0;
        return cnt;
    }
    memcpy(buffer + cnt, response, size);
    cnt += size;

    do {
        start = end = buffer;
        while (end < (buffer + cnt) && *end != '\r')
            ++end;
        if (end < (buffer + cnt) && *end == '\r') {
            *end = '\0';
            snprintf(inbuffer, sizeof(inbuffer), ">> %s", start);
            cmdthread_queue_cmd_in(inbuffer);
            cmdthread_queue_debug_log(inbuffer);
            /* process certain responses */
            val = start;
            while (*val && *val != ' ')
                ++val;
            if (*val)
                ++val;
            if (*val && !strncasecmp(val, "NOW ", 4)) {
                val += 4;
                while (*val && *val == ' ')
                    ++val;
            }
            if (!strncasecmp(start, "BUFFER", 6)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].buffer,
                    sizeof(g_tnc_settings[g_cur_tnc].buffer), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
            } else if (!strncasecmp(start, "NEWSTATE", 8)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].state,
                    sizeof(g_tnc_settings[g_cur_tnc].state), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
                arim_on_event(EV_TNC_NEWSTATE, 0);
            } else if (!strncasecmp(start, "CANCELPENDING", 13)) {
                arim_on_event(EV_ARQ_CAN_PENDING, 0);
            } else if (!strncasecmp(start, "PENDING", 7)) {
                arim_on_event(EV_ARQ_PENDING, 0);
            } else if (!strncasecmp(start, "DISCONNECTED", 12)) {
                arim_on_event(EV_ARQ_DISCONNECTED, 0);
            } else if (!strncasecmp(start, "CONNECTED", 9)) {
                /* parse remote callsign and ARQ bandwidth */
                end = val;
                while (*end && *end != ' ')
                    ++end;
                *end = '\0';
                    ++end;
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].arq_remote_call,
                    sizeof(g_tnc_settings[g_cur_tnc].arq_remote_call), "%s", val);
                if (*end) {
                    while (*end && *end == ' ')
                        ++end;
                    snprintf(g_tnc_settings[g_cur_tnc].arq_bandwidth_hz,
                        sizeof(g_tnc_settings[g_cur_tnc].arq_bandwidth_hz), "%s", end);
                }
                pthread_mutex_unlock(&mutex_tnc_set);
                arim_on_event(EV_ARQ_CONNECTED, 0);
            } else if (!strncasecmp(start, "TARGET", 6)) {
                /* parse target callsign */
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].arq_target_call,
                    sizeof(g_tnc_settings[g_cur_tnc].arq_target_call), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
                arim_on_event(EV_ARQ_TARGET, 0);
            } else if (!strncasecmp(start, "REJECTEDBUSY", 12)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].arq_remote_call,
                    sizeof(g_tnc_settings[g_cur_tnc].arq_remote_call), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
                arim_on_event(EV_ARQ_REJ_BUSY, 0);
            } else if (!strncasecmp(start, "REJECTEDBW", 10)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].arq_remote_call,
                    sizeof(g_tnc_settings[g_cur_tnc].arq_remote_call), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
                arim_on_event(EV_ARQ_REJ_BW, 0);
            } else if (!strncasecmp(start, "LISTEN", 6)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].tmp_listen,
                    sizeof(g_tnc_settings[g_cur_tnc].tmp_listen), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
                ui_set_title_dirty(TITLE_TNC_ATTACHED);
            } else if (!strncasecmp(start, "ENABLEPINGACK", 13)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].en_pingack,
                    sizeof(g_tnc_settings[g_cur_tnc].en_pingack), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
                ui_set_title_dirty(TITLE_TNC_ATTACHED);
            } else if (!strncasecmp(start, "PINGACK", 7)) {
                arim_recv_ping_ack(start);
            } else if (!strncasecmp(start, "PINGREPLY", 9)) {
                arim_send_ping_ack();
            } else if (!strncasecmp(start, "PING", 4)) {
                arim_recv_ping(start);
            } else if (!strncasecmp(start, "PTT", 3)) {
                if (!strncasecmp(val, "TRUE", 4)) {
                    arim_on_event(EV_TNC_PTT, 1);
                    arim_beacon_reset_btimer();
                } else {
                    arim_on_event(EV_TNC_PTT, 0);
                }
            } else if (!strncasecmp(start, "STATE", 5)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].state,
                    sizeof(g_tnc_settings[g_cur_tnc].state), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
            } else if (!strncasecmp(start, "FECMODE", 7)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].fecmode,
                    sizeof(g_tnc_settings[g_cur_tnc].fecmode), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
                ui_set_status_dirty(STATUS_REFRESH);
            } else if (!strncasecmp(start, "FECREPEATS", 10)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].fecrepeats,
                     sizeof(g_tnc_settings[g_cur_tnc].fecrepeats), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
                ui_set_status_dirty(STATUS_REFRESH);
            } else if (!strncasecmp(start, "FECID", 5)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].fecid,
                     sizeof(g_tnc_settings[g_cur_tnc].fecid), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
            } else if (!strncasecmp(start, "MYCALL", 6)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].mycall,
                     sizeof(g_tnc_settings[g_cur_tnc].mycall), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
                arim_beacon_set(-1);
            } else if (!strncasecmp(start, "GRIDSQUARE", 10)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].gridsq,
                    sizeof(g_tnc_settings[g_cur_tnc].gridsq), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
                arim_beacon_set(-1);
            } else if (!strncasecmp(start, "SQUELCH", 7)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].squelch,
                    sizeof(g_tnc_settings[g_cur_tnc].squelch), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
            } else if (!strncasecmp(start, "BUSYDET", 7)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].busydet,
                    sizeof(g_tnc_settings[g_cur_tnc].busydet), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
            } else if (!strncasecmp(start, "BUSY", 4)) {
                if (!strncasecmp(val, "TRUE", 4)) {
                    pthread_mutex_lock(&mutex_tnc_set);
                    snprintf(g_tnc_settings[g_cur_tnc].busy,
                        sizeof(g_tnc_settings[g_cur_tnc].busy), "%s", "TRUE");
                    pthread_mutex_unlock(&mutex_tnc_set);
                    cmdthread_queue_debug_log("Cmd thread: TNC is BUSY");
                } else {
                    pthread_mutex_lock(&mutex_tnc_set);
                    snprintf(g_tnc_settings[g_cur_tnc].busy,
                        sizeof(g_tnc_settings[g_cur_tnc].busy), "%s", "FALSE");
                    pthread_mutex_unlock(&mutex_tnc_set);
                    cmdthread_queue_debug_log("Cmd thread: TNC is not BUSY");
                }
            } else if (!strncasecmp(start, "LEADER", 6)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].leader,
                    sizeof(g_tnc_settings[g_cur_tnc].leader), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
            } else if (!strncasecmp(start, "TRAILER", 7)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].trailer,
                    sizeof(g_tnc_settings[g_cur_tnc].trailer), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
            } else if (!strncasecmp(start, "ARQBW", 5)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].arq_bandwidth,
                    sizeof(g_tnc_settings[g_cur_tnc].arq_bandwidth), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
            } else if (!strncasecmp(start, "VERSION", 7)) {
                pthread_mutex_lock(&mutex_tnc_set);
                snprintf(g_tnc_settings[g_cur_tnc].version,
                    sizeof(g_tnc_settings[g_cur_tnc].version), "%s", val);
                pthread_mutex_unlock(&mutex_tnc_set);
            }
            cnt -= (end - buffer + 1);
            memmove(buffer, end + 1, cnt);
        } else {
            quit = 1;
        }
    } while (!quit);
    return cnt;
}

void *cmdthread_func(void *data)
{
    int cmdsock;
    char buffer[MAX_CMD_SIZE];
    struct addrinfo hints, *res = NULL;
    fd_set cmdreadfds, cmderrorfds;
    struct timeval timeout;
    ssize_t rsize;
    int i, result;

    cmdthread_queue_debug_log("Cmd thread: initializing");
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;  /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(g_tnc_settings[g_cur_tnc].ipaddr, g_tnc_settings[g_cur_tnc].port, &hints, &res);
    if (!res)
    {
        cmdthread_queue_debug_log("Cmd thread: failed to resolve IP address");
        g_cmdthread_stop = 1;
        pthread_exit(data);
    }
    cmdsock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (connect(cmdsock, res->ai_addr, res->ai_addrlen) == -1) {
        cmdthread_queue_debug_log("Cmd thread: failed to open TCP socket");
        g_cmdthread_stop = 1;
        pthread_exit(data);
    }
    freeaddrinfo(res);
    g_cmdthread_ready = 1;
    snprintf(g_tnc_settings[g_cur_tnc].busy,
        sizeof(g_tnc_settings[g_cur_tnc].busy), "%s", "FALSE");

    cmdthread_queue_cmd_out("INITIALIZE");
    snprintf(buffer, sizeof(buffer), "MYCALL %s", g_tnc_settings[g_cur_tnc].mycall);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "GRIDSQUARE %s", g_tnc_settings[g_cur_tnc].gridsq);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "FECMODE %s", g_tnc_settings[g_cur_tnc].fecmode);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "FECID %s", g_tnc_settings[g_cur_tnc].fecid);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "FECREPEATS %s", g_tnc_settings[g_cur_tnc].fecrepeats);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "SQUELCH %s", g_tnc_settings[g_cur_tnc].squelch);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "BUSYDET %s", g_tnc_settings[g_cur_tnc].busydet);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "LEADER %s", g_tnc_settings[g_cur_tnc].leader);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "TRAILER %s", g_tnc_settings[g_cur_tnc].trailer);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "ENABLEPINGACK %s", g_tnc_settings[g_cur_tnc].en_pingack);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "ARQBW %s", g_tnc_settings[g_cur_tnc].arq_bandwidth);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "ARQTIMEOUT %s", g_tnc_settings[g_cur_tnc].arq_timeout);
    cmdthread_queue_cmd_out(buffer);
    snprintf(buffer, sizeof(buffer), "LISTEN %s", g_tnc_settings[g_cur_tnc].listen);
    cmdthread_queue_cmd_out(buffer);
    cmdthread_queue_cmd_out("PROTOCOLMODE FEC");
    cmdthread_queue_cmd_out("AUTOBREAK TRUE");
    cmdthread_queue_cmd_out("MONITOR TRUE");
    cmdthread_queue_cmd_out("CWID FALSE");
    cmdthread_queue_cmd_out("VERSION");
    cmdthread_queue_cmd_out("STATE");
    /* send TNC initialization commands from config file */
    for (i = 0; i < g_tnc_settings[g_cur_tnc].tnc_init_cmds_cnt; i++)
        cmdthread_queue_cmd_out(g_tnc_settings[g_cur_tnc].tnc_init_cmds[i]);

    while (1) {
        FD_ZERO(&cmdreadfds);
        FD_ZERO(&cmderrorfds);
        FD_SET(cmdsock, &cmdreadfds);
        FD_SET(cmdsock, &cmderrorfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        result = select(cmdsock + 1, &cmdreadfds, (fd_set *)0, &cmderrorfds, &timeout);
        switch (result) {
        case 0:
            cmdthread_next_cmd_out(cmdsock);
            break;
        case -1:
            cmdthread_queue_debug_log("Cmd thread: Socket select error (-1)");
            break;
        default:
            if (FD_ISSET(cmdsock, &cmdreadfds)) {
                rsize = read(cmdsock, buffer, sizeof(buffer) - 1);
                if (rsize != -1)
                    cmdthread_proc_response(buffer, rsize, cmdsock);
            }
            if (FD_ISSET(cmdsock, &cmderrorfds)) {
                cmdthread_queue_debug_log("Cmd thread: Socket select error (FD_ISSET)");
                break;
            }
        }
        if (g_cmdthread_stop) {
            break;
        }
    }
    snprintf(g_tnc_settings[g_cur_tnc].busy,
        sizeof(g_tnc_settings[g_cur_tnc].busy), "%s", "FALSE");
    cmdthread_queue_debug_log("Cmd thread: terminating");
    sleep(2);
    close(cmdsock);
    return data;
}

