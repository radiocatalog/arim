/***********************************************************************

    ARIM Amateur Radio Instant Messaging program for the ARDOP TNC.

    Copyright (C) 2016, 2017 Robert Cunnings NW8L

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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "main.h"
#include "arim_proto.h"
#include "cmdproc.h"
#include "ini.h"
#include "mbox.h"
#include "ui.h"
#include "util.h"
#include "bufq.h"

int arim_send_query(const char *query, const char *to_call)
{
    char mycall[TNC_MYCALL_SIZE];
    unsigned int check;
    size_t len = 0;

    if (!g_tnc_attached || !arim_is_idle())
            return 0;
    if (atoi(g_arim_settings.pilot_ping)) {
        snprintf(prev_msg, sizeof(prev_msg), "%s", query);
        snprintf(prev_to_call, sizeof(prev_to_call), "%s", to_call);
        arim_on_event(EV_SEND_QRY_PP, 0);
        return 1;
    }
    arim_copy_mycall(mycall, sizeof(mycall));
    check = ccitt_crc16((unsigned char *)query, strlen(query));
    snprintf(msg_buffer, sizeof(msg_buffer), "|Q%02d|%s|%s|%04zX|%04X|%s",
                    ARIM_PROTO_VERSION,
                    mycall,
                    to_call,
                    len,
                    check,
                    query);
    len = strlen(msg_buffer);
    snprintf(msg_buffer, sizeof(msg_buffer), "|Q%02d|%s|%s|%04zX|%04X|%s",
                    ARIM_PROTO_VERSION,
                    mycall,
                    to_call,
                    len,
                    check,
                    query);
    ui_queue_data_out(msg_buffer);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
        sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", len);
    pthread_mutex_unlock(&mutex_tnc_set);
    ack_timeout = atoi(g_arim_settings.ack_timeout);
    arim_on_event(EV_SEND_QRY, 0);
    return 1;
}

int arim_send_query_pp()
{
    char mycall[TNC_MYCALL_SIZE];
    unsigned int check;
    size_t len = 0;

    arim_copy_mycall(mycall, sizeof(mycall));
    check = ccitt_crc16((unsigned char *)prev_msg, strlen(prev_msg));
    snprintf(msg_buffer, sizeof(msg_buffer), "|Q%02d|%s|%s|%04zX|%04X|%s",
                    ARIM_PROTO_VERSION,
                    mycall,
                    prev_to_call,
                    len,
                    check,
                    prev_msg);
    len = strlen(msg_buffer);
    snprintf(msg_buffer, sizeof(msg_buffer), "|Q%02d|%s|%s|%04zX|%04X|%s",
                    ARIM_PROTO_VERSION,
                    mycall,
                    prev_to_call,
                    len,
                    check,
                    prev_msg);
    ui_queue_data_out(msg_buffer);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
        sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", len);
    pthread_mutex_unlock(&mutex_tnc_set);
    ack_timeout = atoi(g_arim_settings.ack_timeout);
    arim_on_event(EV_SEND_QRY, 0);
    return 1;
}

int arim_recv_response(const char *fm_call, const char *to_call,
                            unsigned int check, const char *msg)
{
    char buffer[MAX_MBOX_HDR_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    int is_mycall, result = 1;

    /* is this message directed to mycall? */
    is_mycall = arim_test_mycall(to_call);
    if (is_mycall) {
        /* verify good checksum */
        result = arim_check(msg, check);
        if (result) {
            /* good checksum, store message into mbox */
            snprintf(buffer, sizeof(buffer), "From %-10s %s To %-10s %04X",
                    fm_call, util_date_timestamp(timestamp, sizeof(timestamp)), to_call, check);
            pthread_mutex_lock(&mutex_recents);
            cmdq_push(&g_recents_q, buffer);
            pthread_mutex_unlock(&mutex_recents);
            mbox_add_msg(MBOX_INBOX_FNAME, fm_call, to_call, check, msg);
            snprintf(buffer, sizeof(buffer), "3[R] %-10s ", fm_call);
        } else {
            snprintf(buffer, sizeof(buffer), "1[!] %-10s ", fm_call);
        }
        /* all done */
        arim_on_event(EV_RCV_RESP, 0);
    } else {
        snprintf(buffer, sizeof(buffer), "7[R] %-10s ", fm_call);
    }
    ui_queue_heard(buffer);
    return result;
}

int arim_recv_query(const char *fm_call, const char *to_call,
                            unsigned int check, const char *query)
{
    char buffer[MAX_HEARD_SIZE], respbuf[MIN_MSG_BUF_SIZE];
    char mycall[TNC_MYCALL_SIZE];
    int is_mycall, result = 1;
    size_t len = 0;

    /* is this message directed to mycall? */
    is_mycall = arim_test_mycall(to_call);
    if (is_mycall) {
        /* if so, verify good checksum and return response to sender */
        result = arim_check(query, check);
        if (result) {
            arim_copy_mycall(mycall, sizeof(mycall));
            cmdproc_query(query, respbuf, sizeof(respbuf));
            check = ccitt_crc16((unsigned char *)respbuf, strlen(respbuf));
            snprintf(msg_buffer, sizeof(msg_buffer), "|R%02d|%s|%s|%04zX|%04X|%s",
                            ARIM_PROTO_VERSION,
                            mycall,
                            fm_call,
                            len,
                            check,
                            respbuf);
            len = strlen(msg_buffer);
            snprintf(msg_buffer, sizeof(msg_buffer), "|R%02d|%s|%s|%04zX|%04X|%s",
                            ARIM_PROTO_VERSION,
                            mycall,
                            fm_call,
                            len,
                            check,
                            respbuf);
            arim_on_event(EV_RCV_QRY, 0);
            snprintf(buffer, sizeof(buffer), "3[Q] %-10s ", fm_call);
        } else {
            snprintf(buffer, sizeof(buffer), "1[!] %-10s ", fm_call);
        }
    } else {
        snprintf(buffer, sizeof(buffer), "7[Q] %-10s ", fm_call);
    }
    ui_queue_heard(buffer);
    return result;
}

