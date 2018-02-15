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

int arim_send_msg(const char *msg, const char *to_call)
{
    char mycall[TNC_MYCALL_SIZE], fecmode[TNC_FECMODE_SIZE];
    unsigned int check;
    size_t len = 0;

    if (!arim_is_idle() || !arim_tnc_is_idle())
        return 0;
    /* store message if needed later for sending after pilot pings
       or to store it in outbox if send fails or is canceled */
    snprintf(prev_msg, sizeof(prev_msg), "%s", msg);
    snprintf(prev_to_call, sizeof(prev_to_call), "%s", to_call);
    if (atoi(g_arim_settings.pilot_ping) && !arim_test_netcall(to_call)) {
        arim_on_event(EV_SEND_MSG_PP, 0);
        return 1;
    }
    arim_copy_mycall(mycall, sizeof(mycall));
    check = ccitt_crc16((unsigned char *)msg, strlen(msg));
    snprintf(msg_buffer, sizeof(msg_buffer), "|M%02d|%s|%s|%04zX|%04X|%s",
                    ARIM_PROTO_VERSION,
                    mycall,
                    to_call,
                    len,
                    check,
                    msg);
    len = strlen(msg_buffer);
    snprintf(msg_buffer, sizeof(msg_buffer), "|M%02d|%s|%s|%04zX|%04X|%s",
                    ARIM_PROTO_VERSION,
                    mycall,
                    to_call,
                    len,
                    check,
                    msg);
    ui_queue_data_out(msg_buffer);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
        sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", len);
    pthread_mutex_unlock(&mutex_tnc_set);
    if (arim_test_netcall(to_call)) {
        /* if sent to netcall no ACK is expected */
        arim_on_event(EV_SEND_NET_MSG, 0);
    } else {
        /* set up for ACK wait and repeats */
        if (!strncasecmp(g_arim_settings.fecmode_downshift, "TRUE", 4))
            fecmode_downshift = 1;
        else
            fecmode_downshift = 0;
        arim_set_send_repeats(atoi(g_arim_settings.send_repeats));
        if (arim_get_send_repeats() && fecmode_downshift) {
            /* cache fecmode so it can be restored after downshifting */
            arim_copy_fecmode(fecmode, sizeof(fecmode));
            snprintf(prev_fecmode, sizeof(prev_fecmode), "%s", fecmode);
        }
        ack_timeout = atoi(g_arim_settings.ack_timeout);
        rcv_nak_cnt = 0;
        arim_on_event(EV_SEND_MSG, 0);
    }
    return 1;
}

int arim_store_msg_prev_out()
{
    return arim_store_out(prev_msg, prev_to_call);
}

int arim_store_msg_prev_sent()
{
    return arim_store_sent(prev_msg, prev_to_call);
}

int arim_send_msg_pp()
{
    char mycall[TNC_MYCALL_SIZE], fecmode[TNC_FECMODE_SIZE];
    unsigned int check;
    size_t len = 0;

    arim_copy_mycall(mycall, sizeof(mycall));
    check = ccitt_crc16((unsigned char *)prev_msg, strlen(prev_msg));
    snprintf(msg_buffer, sizeof(msg_buffer), "|M%02d|%s|%s|%04zX|%04X|%s",
                    ARIM_PROTO_VERSION,
                    mycall,
                    prev_to_call,
                    len,
                    check,
                    prev_msg);
    len = strlen(msg_buffer);
    snprintf(msg_buffer, sizeof(msg_buffer), "|M%02d|%s|%s|%04zX|%04X|%s",
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
    /* set up for ACK wait and repeats */
    if (!strncasecmp(g_arim_settings.fecmode_downshift, "TRUE", 4))
        fecmode_downshift = 1;
    else
        fecmode_downshift = 0;
    arim_set_send_repeats(atoi(g_arim_settings.send_repeats));
    if (arim_get_send_repeats() && fecmode_downshift) {
        /* cache fecmode so it can be restored after downshifting */
        arim_copy_fecmode(fecmode, sizeof(fecmode));
        snprintf(prev_fecmode, sizeof(prev_fecmode), "%s", fecmode);
    }
    ack_timeout = atoi(g_arim_settings.ack_timeout);
    rcv_nak_cnt = 0;
    arim_on_event(EV_SEND_MSG, 0);
    return 1;
}

int arim_recv_msg(const char *fm_call, const char *to_call,
                            unsigned int check, const char *msg)
{
    char buffer[MAX_CMD_SIZE], mycall[TNC_MYCALL_SIZE];
    char timestamp[MAX_TIMESTAMP_SIZE];
    int is_netcall, is_mycall, result = 1;

    /* is this message directed to mycall or netcall? */
    is_mycall = arim_test_mycall(to_call);
    is_netcall = arim_test_netcall(to_call);
    if (is_mycall || is_netcall) {
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
            if (is_netcall) {
                snprintf(buffer, sizeof(buffer), "6[M] %-10s ", fm_call);
            } else
                snprintf(buffer, sizeof(buffer), "2[M] %-10s ", fm_call);
        } else {
            snprintf(buffer, sizeof(buffer), "1[!] %-10s ", fm_call);
        }
    } else {
        snprintf(buffer, sizeof(buffer), "7[M] %-10s ", fm_call);
    }
    ui_queue_heard(buffer);
    /* return ack or nak to sender if sent to mycall but not netcall */
    if (is_mycall) {
        /* force desired upper/lower case as set by user */
        arim_copy_mycall(mycall, sizeof(mycall));
        snprintf(msg_acknak_buffer, sizeof(msg_acknak_buffer), "|%c%02d|%s|%s|",
                        result ? 'A' : 'N',
                        ARIM_PROTO_VERSION,
                        mycall,
                        fm_call);
        arim_on_event(EV_RCV_MSG, 0);
    } else if (is_netcall) {
        arim_on_event(EV_RCV_NET_MSG, 0);
    }
    return result;
}

void arim_recv_ack(const char *fm_call, const char *to_call)
{
    char buffer[MAX_HEARD_SIZE];
    int is_mycall;

    /* is this message directed to mycall? */
    is_mycall = arim_test_mycall(to_call);
    snprintf(buffer, sizeof(buffer), "%d[A] %-10s ", is_mycall ? 2 : 7, fm_call);
    ui_queue_heard(buffer);
    if (is_mycall)
        arim_on_event(EV_RCV_ACK, 0);
}

void arim_recv_nak(const char *fm_call, const char *to_call)
{
    char buffer[MAX_HEARD_SIZE];
    int is_mycall;

    /* is this message directed to mycall? */
    is_mycall = arim_test_mycall(to_call);
    snprintf(buffer, sizeof(buffer), "%d[N] %-10s ", is_mycall ? 1 : 7, fm_call);
    ui_queue_heard(buffer);
    if (is_mycall)
        arim_on_event(EV_RCV_NAK, 0);
}

