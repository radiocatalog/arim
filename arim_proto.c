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
#include "arim.h"
#include "arim_proto.h"
#include "arim_ping.h"
#include "arim_arq.h"
#include "ini.h"
#include "mbox.h"
#include "ui.h"
#include "util.h"

pthread_mutex_t mutex_arim_state = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_send_repeats = PTHREAD_MUTEX_INITIALIZER;

char msg_acknak_buffer[MAX_ACKNAK_SIZE];
char msg_buffer[MIN_MSG_BUF_SIZE];

time_t prev_time;
char prev_fecmode[TNC_FECMODE_SIZE];
char prev_to_call[TNC_MYCALL_SIZE];
char prev_msg[MIN_MSG_BUF_SIZE];
int rcv_nak_cnt = 0, ack_timeout = 30, send_repeats = 0, fecmode_downshift = 0;
static int arim_state = 0;

const char *downshift[] = {
    /* 4FSK family */
    "4FSK.200.50S,4FSK.200.50S",
    "4FSK.500.100S,4FSK.200.50S",
    "4FSK.500.100,4FSK.500.100S",
    "4FSK.2000.600S,4FSK.500.100",
    "4FSK.2000.600,4FSK.2000.600S",
    /* 4PSK family */
    "4PSK.200.100S,4FSK.200.50S",
    "4PSK.200.100,4PSK.200.100S",
    "4PSK.500.100,4PSK.200.100",
    "4PSK.1000.100,4PSK.500.100",
    "4PSK.2000.100,4PSK.1000.100",
    /* 8PSK family */
    "8PSK.200.100,4FSK.200.50S",
    "8PSK.500.100,8PSK.200.100",
    "8PSK.1000.100,8PSK.500.100",
    "8PSK.2000.100,8PSK.1000.100",
    /* 16QAM family */
    "16QAM.200.100,4FSK.200.50S",
    "16QAM.500.100,16QAM.200.100",
    "16QAM.1000.100,16QAM.500.100",
    "16QAM.2000.100,16QAM.1000.100",
    0,
};

const char *states[] = {
    "ST_IDLE",                      /*  0 */
    "ST_SEND_MSG_BUF_WAIT",         /*  1 */
    "ST_SEND_NET_MSG_BUF_WAIT",     /*  2 */
    "ST_SEND_QRY_BUF_WAIT",         /*  3 */
    "ST_SEND_RESP_BUF_WAIT",        /*  4 */
    "ST_SEND_ACKNAK_BUF_WAIT",      /*  5 */
    "ST_SEND_BCN_BUF_WAIT",         /*  6 */
    "ST_SEND_UN_BUF_WAIT",          /*  7 */
    "ST_SEND_ACKNAK_PEND",          /*  8 */
    "ST_SEND_RESP_PEND",            /*  9 */
    "ST_RCV_ACKNAK_WAIT",           /* 10 */
    "ST_RCV_RESP_WAIT",             /* 11 */
    "ST_RCV_FRAME_WAIT",            /* 12 */
    "ST_ARQ_PEND_WAIT",             /* 13 */
    "ST_RCV_PING_ACK_WAIT",         /* 14 */
    "ST_SEND_PING_ACK_PEND",        /* 15 */
    "ST_RCV_MSG_PING_ACK_WAIT",     /* 16 */
    "ST_RCV_QRY_PING_ACK_WAIT",     /* 17 */
    "ST_ARQ_IN_CONNECT_WAIT",       /* 18 */
    "ST_ARQ_OUT_CONNECT_WAIT",      /* 19 */
    "ST_RCV_ARQ_CONN_PP_WAIT",      /* 20 */
    "ST_ARQ_CONNECTED",             /* 21 */
    "ST_ARQ_FILE_SEND_WAIT",        /* 22 */
    "ST_ARQ_FILE_SEND",             /* 23 */
    "ST_ARQ_FILE_RCV_WAIT",         /* 24 */
    "ST_ARQ_FILE_RCV",              /* 25 */
    "ST_ARQ_MSG_RCV",               /* 26 */
    "ST_ARQ_MSG_SEND_WAIT",         /* 27 */
    "ST_ARQ_MSG_SEND",              /* 28 */
};

const char *events[] = {
    "EV_NULL",                   /*  0 */
    "EV_PERIODIC",               /*  1 */
    "EV_CANCEL",                 /*  2 */
    "EV_FRAME_START",            /*  3 */
    "EV_FRAME_END",              /*  4 */
    "EV_FRAME_TO",               /*  5 */
    "EV_SEND_MSG",               /*  6 */
    "EV_SEND_MSG_PP",            /*  7 */
    "EV_SEND_NET_MSG",           /*  8 */
    "EV_RCV_MSG",                /*  9 */
    "EV_RCV_ACK",                /* 10 */
    "EV_RCV_NAK",                /* 11 */
    "EV_RCV_NET_MSG",            /* 12 */
    "EV_SEND_QRY",               /* 13 */
    "EV_SEND_QRY_PP",            /* 14 */
    "EV_RCV_RESP",               /* 15 */
    "EV_RCV_QRY",                /* 16 */
    "EV_SEND_BCN",               /* 17 */
    "EV_SEND_UNPROTO",           /* 18 */
    "EV_SEND_PING",              /* 19 */
    "EV_SEND_PING_ACK",          /* 20 */
    "EV_RCV_PING",               /* 21 */
    "EV_RCV_PING_ACK",           /* 22 */
    "EV_TNC_PTT",                /* 23 */
    "EV_TNC_NEWSTATE",           /* 24 */
    "EV_ARQ_PENDING",            /* 25 */
    "EV_ARQ_CAN_PENDING",        /* 26 */
    "EV_ARQ_CONNECT",            /* 27 */
    "EV_ARQ_CONNECT_PP",         /* 28 */
    "EV_ARQ_CONNECTED",          /* 29 */
    "EV_ARQ_DISCONNECTED",       /* 30 */
    "EV_ARQ_TARGET",             /* 31 */
    "EV_ARQ_REJ_BUSY",           /* 32 */
    "EV_ARQ_REJ_BW",             /* 33 */
    "EV_ARQ_FILE_SEND",          /* 34 */
    "EV_ARQ_FILE_SEND_CMD",      /* 35 */
    "EV_ARQ_FILE_RCV_WAIT",      /* 36 */
    "EV_ARQ_FILE_RCV",           /* 37 */
    "EV_ARQ_FILE_RCV_FRAME",     /* 38 */
    "EV_ARQ_FILE_RCV_DONE",      /* 39 */
    "EV_ARQ_FILE_ERROR",         /* 40 */
    "EV_ARQ_FILE_OK",            /* 41 */
    "EV_ARQ_MSG_RCV",            /* 42 */
    "EV_ARQ_MSG_RCV_FRAME",      /* 43 */
    "EV_ARQ_MSG_RCV_DONE",       /* 44 */
    "EV_ARQ_MSG_ERROR",          /* 45 */
    "EV_ARQ_MSG_OK",             /* 46 */
    "EV_ARQ_MSG_SEND_CMD",       /* 47 */
    "EV_ARQ_MSG_SEND",           /* 48 */
    "EV_ARQ_CANCEL_WAIT",        /* 49 */
};

void arim_copy_mycall(char *call, size_t size)
{
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(call, size, "%s", g_tnc_settings[g_cur_tnc].mycall);
    pthread_mutex_unlock(&mutex_tnc_set);
}

int arim_get_netcall_cnt()
{
    int cnt;

    pthread_mutex_lock(&mutex_tnc_set);
    cnt = g_tnc_settings[g_cur_tnc].netcall_cnt;
    pthread_mutex_unlock(&mutex_tnc_set);
    return cnt;
}

int arim_copy_netcall(char *call, size_t size, int which)
{
    int result = 0;

    pthread_mutex_lock(&mutex_tnc_set);
    if (which >= 0 && which < g_tnc_settings[g_cur_tnc].netcall_cnt) {
        snprintf(call, size, "%s", g_tnc_settings[g_cur_tnc].netcall[which]);
        result = 1;
    }
    pthread_mutex_unlock(&mutex_tnc_set);
    return result;
}

void arim_copy_remote_call(char *call, size_t size)
{
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(call, size, "%s", g_tnc_settings[g_cur_tnc].arq_remote_call);
    pthread_mutex_unlock(&mutex_tnc_set);
}

void arim_copy_target_call(char *call, size_t size)
{
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(call, size, "%s", g_tnc_settings[g_cur_tnc].arq_target_call);
    pthread_mutex_unlock(&mutex_tnc_set);
}

void arim_copy_arq_sendcr(char *val, size_t size)
{
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(val, size, "%s", g_tnc_settings[g_cur_tnc].arq_sendcr);
    pthread_mutex_unlock(&mutex_tnc_set);
}

void arim_copy_arq_bw_hz(char *val, size_t size)
{
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(val, size, "%s", g_tnc_settings[g_cur_tnc].arq_bandwidth_hz);
    pthread_mutex_unlock(&mutex_tnc_set);
}

void arim_copy_listen(char *val, size_t size)
{
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(val, size, "%s", g_tnc_settings[g_cur_tnc].listen);
    pthread_mutex_unlock(&mutex_tnc_set);
}

void arim_copy_tnc_state(char *state, size_t size)
{
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(state, size, "%s", g_tnc_settings[g_cur_tnc].state);
    pthread_mutex_unlock(&mutex_tnc_set);
}

int arim_test_mycall(const char *call)
{
    int result;

    pthread_mutex_lock(&mutex_tnc_set);
    result = strcasecmp(call, g_tnc_settings[g_cur_tnc].mycall);
    pthread_mutex_unlock(&mutex_tnc_set);
    return (result ? 0 : 1);
}

int arim_test_netcall(const char *call)
{
    int i, cnt;

    pthread_mutex_lock(&mutex_tnc_set);
    cnt = g_tnc_settings[g_cur_tnc].netcall_cnt;
    for (i = 0; i < cnt; i++) {
        if (!strcasecmp(call, g_tnc_settings[g_cur_tnc].netcall[i]))
            break;
    }
    pthread_mutex_unlock(&mutex_tnc_set);
    return (i == cnt ? 0 : 1);
}

int arim_check(const char *msg, unsigned int cs_rcvd)
{
    unsigned int cs_msg;

    cs_msg = ccitt_crc16((unsigned char *)msg, strlen(msg));
    return (cs_msg == cs_rcvd ? 1 : 0);
}

int arim_store_out(const char *msg, const char *to_call)
{
    unsigned int check;

    check = ccitt_crc16((unsigned char *)msg, strlen(msg));
    return mbox_add_msg(MBOX_OUTBOX_FNAME, g_arim_settings.mycall, to_call, check, msg);
}

int arim_store_sent(const char *msg, const char *to_call)
{
    unsigned int check;

    check = ccitt_crc16((unsigned char *)msg, strlen(msg));
    return mbox_add_msg(MBOX_SENTBOX_FNAME, g_arim_settings.mycall, to_call, check, msg);
}

void arim_restore_prev_fecmode()
{
    char temp[MAX_CMD_SIZE];

    snprintf(temp, sizeof(temp), "FECMODE %s", prev_fecmode);
    ui_queue_cmd_out(temp);
}

int arim_is_arq_state()
{
    int state;

    pthread_mutex_lock(&mutex_arim_state);
    state = arim_state;
    pthread_mutex_unlock(&mutex_arim_state);
    if (state == ST_ARQ_CONNECTED ||
        state == ST_ARQ_MSG_RCV ||
        state == ST_ARQ_MSG_SEND_WAIT ||
        state == ST_ARQ_MSG_SEND ||
        state == ST_ARQ_FILE_RCV_WAIT ||
        state == ST_ARQ_FILE_RCV ||
        state == ST_ARQ_FILE_SEND_WAIT ||
        state == ST_ARQ_FILE_SEND) {
            return 1;
    }
    return 0;
}

int arim_get_state()
{
    int ret;

    pthread_mutex_lock(&mutex_arim_state);
    ret = arim_state;
    pthread_mutex_unlock(&mutex_arim_state);
    return ret;
}

void arim_set_state(int newstate)
{
    char buffer[TNC_LISTEN_SIZE], cmd[MAX_CMD_SIZE];

    if (newstate == ST_IDLE) {
        ui_queue_cmd_out("PROTOCOLMODE FEC");
        arim_copy_listen(buffer, sizeof(buffer));
        snprintf(cmd, sizeof(cmd), "LISTEN %s", buffer);
        ui_queue_cmd_out(cmd);
    }
    pthread_mutex_lock(&mutex_arim_state);
    arim_state = newstate;
    pthread_mutex_unlock(&mutex_arim_state);
}

void arim_reset_msg_rpt_state()
{
    arim_set_send_repeats(0);
    if (fecmode_downshift)
        arim_restore_prev_fecmode();
    rcv_nak_cnt = 0;
}

int arim_is_idle()
{
    return (arim_get_state() == ST_IDLE) ? 1 : 0;
}

int arim_get_send_repeats()
{
    int ret;

    pthread_mutex_lock(&mutex_send_repeats);
    ret = send_repeats;
    pthread_mutex_unlock(&mutex_send_repeats);
    return ret;
}

void arim_set_send_repeats(int val)
{
    pthread_mutex_lock(&mutex_send_repeats);
    send_repeats = val;
    pthread_mutex_unlock(&mutex_send_repeats);
}

int arim_get_fec_repeats()
{
    int ret;

    pthread_mutex_lock(&mutex_tnc_set);
    ret = atoi(g_tnc_settings[g_cur_tnc].fecrepeats);
    pthread_mutex_unlock(&mutex_tnc_set);
    return ret;
}

int arim_get_buffer_cnt()
{
    int ret;

    pthread_mutex_lock(&mutex_tnc_set);
    ret = atoi(g_tnc_settings[g_cur_tnc].buffer);
    pthread_mutex_unlock(&mutex_tnc_set);
    return ret;
}

int arim_is_receiving()
{
    int ret;

    pthread_mutex_lock(&mutex_tnc_set);
    ret = strncasecmp(g_tnc_settings[g_cur_tnc].state, "FECRCV", 6);
    pthread_mutex_unlock(&mutex_tnc_set);
    return ret ? 0 : 1;
}

void arim_copy_fecmode(char *mode, size_t size)
{
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(mode, size, "%s", g_tnc_settings[g_cur_tnc].fecmode);
    pthread_mutex_unlock(&mutex_tnc_set);
}

void arim_fecmode_downshift()
{
    const char *p;
    char temp[MAX_CMD_SIZE], fecmode[TNC_FECMODE_SIZE];
    size_t len, i = 0;
    int result;

    arim_copy_fecmode(fecmode, sizeof(fecmode));
    len = strlen(fecmode);
    p = downshift[i];
    while (p) {
        result = strncasecmp(downshift[i], fecmode, len);
        if (!result && *(p + len) == ',') {
            p += (len + 1);
            snprintf(temp, sizeof(temp), "FECMODE %s", p);
            ui_queue_cmd_out(temp);
            break;
        }
        p = downshift[++i];
    }
}

void arim_cancel_trans()
{
    if (arim_get_buffer_cnt() > 0)
        ui_queue_cmd_out("ABORT");
}

void arim_on_event(int event, int param)
{
    time_t t;
    int prev_state, next_state;
    char buffer[MAX_LOG_LINE_SIZE];

    prev_state = arim_get_state();

    switch (prev_state) {
    case ST_IDLE:
        switch (event) {
        case EV_FRAME_START:
            ack_timeout = atoi(g_arim_settings.frame_timeout);
            prev_time = time(NULL);
            arim_set_state(ST_RCV_FRAME_WAIT);
            ui_queue_cmd_out("LISTEN FALSE");
            switch (param) {
            case 'M':
                ui_set_status_dirty(STATUS_MSG_START);
                break;
            case 'Q':
                ui_set_status_dirty(STATUS_QRY_START);
                break;
            case 'R':
                ui_set_status_dirty(STATUS_RESP_START);
                break;
            case 'B':
                ui_set_status_dirty(STATUS_BCN_START);
                break;
            default:
                ui_set_status_dirty(STATUS_FRAME_START);
                break;
            }
            break;
        case EV_SEND_BCN:
            arim_set_state(ST_SEND_BCN_BUF_WAIT);
            ui_queue_cmd_out("LISTEN FALSE");
            break;
        case EV_SEND_MSG:
            arim_set_state(ST_SEND_MSG_BUF_WAIT);
            ui_queue_cmd_out("LISTEN FALSE");
            break;
        case EV_SEND_MSG_PP:
            ack_timeout = ARDOP_PINGACK_TIMEOUT;
            prev_time = time(NULL);
            arim_send_ping(g_arim_settings.pilot_ping, prev_to_call, 0);
            arim_set_state(ST_RCV_MSG_PING_ACK_WAIT);
            ui_queue_cmd_out("LISTEN FALSE");
            break;
        case EV_SEND_QRY_PP:
            ack_timeout = ARDOP_PINGACK_TIMEOUT;
            prev_time = time(NULL);
            arim_send_ping(g_arim_settings.pilot_ping, prev_to_call, 0);
            arim_set_state(ST_RCV_QRY_PING_ACK_WAIT);
            ui_queue_cmd_out("LISTEN FALSE");
            break;
        case EV_SEND_NET_MSG:
            arim_set_state(ST_SEND_NET_MSG_BUF_WAIT);
            ui_queue_cmd_out("LISTEN FALSE");
            break;
        case EV_SEND_QRY:
            arim_set_state(ST_SEND_QRY_BUF_WAIT);
            ui_queue_cmd_out("LISTEN FALSE");
            break;
        case EV_SEND_UNPROTO:
            arim_set_state(ST_SEND_UN_BUF_WAIT);
            ui_set_status_dirty(STATUS_REFRESH);
            ui_queue_cmd_out("LISTEN FALSE");
            break;
        case EV_SEND_PING:
            /* a PING command was sent to the TNC */
            ack_timeout = ARDOP_PINGACK_TIMEOUT;
            prev_time = time(NULL);
            arim_set_state(ST_RCV_PING_ACK_WAIT);
            ui_queue_cmd_out("LISTEN FALSE");
            break;
        case EV_ARQ_PENDING:
            /* a PENDING async response was received from the TNC
               signalling arrival of an ARQ connect or ping frame */
            arim_copy_listen(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "TRUE", 4)) {
                /* respond only if ARQ listen is TRUE */
                ui_queue_cmd_out("PROTOCOLMODE ARQ");
                ack_timeout = ARDOP_PINGACK_TIMEOUT;
                prev_time = time(NULL);
                arim_set_state(ST_ARQ_PEND_WAIT);
            }
            break;
        case EV_ARQ_CONNECT:
            /* an ARQ connection attempt is underway */
            ack_timeout = ARDOP_CONNREQ_TIMEOUT;
            prev_time = time(NULL);
            arim_set_state(ST_ARQ_OUT_CONNECT_WAIT);
            ui_queue_cmd_out("LISTEN FALSE");
            ui_queue_cmd_out("PROTOCOLMODE ARQ");
            break;
        case EV_ARQ_CONNECT_PP:
            /* an ARQ connection attempt is underway */
            ack_timeout = ARDOP_PINGACK_TIMEOUT;
            prev_time = time(NULL);
            arim_send_ping(g_arim_settings.pilot_ping, prev_to_call, 0);
            arim_set_state(ST_RCV_ARQ_CONN_PP_WAIT);
            break;
        }
        break;
    case ST_SEND_MSG_BUF_WAIT:
        switch (event) {
        case EV_CANCEL:
            arim_cancel_trans();
            arim_reset_msg_rpt_state();
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_MSG_SEND_CAN);
            break;
        case EV_PERIODIC:
            /* wait until tx buffer is empty before starting ack timer */
            if (!arim_get_buffer_cnt()) {
                prev_time = time(NULL);
                arim_set_state(ST_RCV_ACKNAK_WAIT);
                ui_set_status_dirty(STATUS_WAIT_ACK);
            }
            break;
        }
        break;
    case ST_SEND_NET_MSG_BUF_WAIT:
        switch (event) {
        case EV_CANCEL:
            arim_cancel_trans();
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_MSG_SEND_CAN);
            break;
        case EV_PERIODIC:
            /* wait until tx buffer is empty before starting ack timer */
            if (!arim_get_buffer_cnt()) {
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_NET_MSG_SENT);
            }
            break;
        }
        break;
    case ST_SEND_QRY_BUF_WAIT:
        switch (event) {
        case EV_CANCEL:
            arim_cancel_trans();
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_QRY_SEND_CAN);
            break;
        case EV_PERIODIC:
            /* wait until tx buffer is empty before announcing waiting state */
            if (!arim_get_buffer_cnt()) {
                prev_time = time(NULL);
                arim_set_state(ST_RCV_RESP_WAIT);
                ui_set_status_dirty(STATUS_WAIT_RESP);
            }
            break;
        }
        break;
    case ST_SEND_RESP_BUF_WAIT:
        switch (event) {
        case EV_CANCEL:
            arim_cancel_trans();
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_RESP_SEND_CAN);
            break;
        case EV_PERIODIC:
            /* wait until tx buffer is empty before announcing idle state */
            if (!arim_get_buffer_cnt()) {
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_RESP_SENT);
            }
            break;
        }
        break;
    case ST_SEND_ACKNAK_BUF_WAIT:
        switch (event) {
        case EV_CANCEL:
            arim_cancel_trans();
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ACKNAK_SEND_CAN);
            break;
        case EV_PERIODIC:
            /* wait until tx buffer is empty before announcing idle state */
            if (!arim_get_buffer_cnt()) {
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_ACKNAK_SENT);
            }
            break;
        }
        break;
    case ST_SEND_BCN_BUF_WAIT:
        switch (event) {
        case EV_CANCEL:
            arim_cancel_trans();
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_BCN_SEND_CAN);
            break;
        case EV_PERIODIC:
            /* wait until tx buffer is empty before announcing idle state */
            if (!arim_get_buffer_cnt()) {
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_BEACON_SENT);
            }
            break;
        }
        break;
    case ST_SEND_UN_BUF_WAIT:
        switch (event) {
        case EV_CANCEL:
            arim_cancel_trans();
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_SEND_UNPROTO_CAN);
            break;
        case EV_PERIODIC:
            /* wait until tx buffer is empty before entering idle state */
            if (!arim_get_buffer_cnt()) {
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        }
        break;
    case ST_SEND_RESP_PEND:
        switch (event) {
        case EV_CANCEL:
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_RESP_SEND_CAN);
            break;
        case EV_PERIODIC:
            /* 1 to 2 second delay for sending response to query */
            t = time(NULL);
            if (t > prev_time + 2) {
                ui_queue_data_out(msg_buffer);
                arim_set_state(ST_SEND_RESP_BUF_WAIT);
                ui_set_status_dirty(STATUS_RESP_SEND);
            }
            break;
        }
        break;
    case ST_SEND_ACKNAK_PEND:
        switch (event) {
        case EV_CANCEL:
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ACKNAK_SEND_CAN);
            break;
        case EV_PERIODIC:
            /* 1 to 2 second delay for sending ack/nak */
            t = time(NULL);
            if (t > prev_time + 2) {
                ui_queue_data_out(msg_acknak_buffer);
                arim_set_state(ST_SEND_ACKNAK_BUF_WAIT);
                ui_set_status_dirty(STATUS_ACKNAK_SEND);
            }
            break;
        }
        break;
    case ST_RCV_ACKNAK_WAIT:
        switch (event) {
        case EV_RCV_ACK:
            arim_reset_msg_rpt_state();
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_MSG_ACK_RCVD);
            break;
        case EV_RCV_NAK:
            if (++rcv_nak_cnt > arim_get_send_repeats()) {
                /* timeout, all done */
                arim_reset_msg_rpt_state();
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_MSG_NAK_RCVD);
            } else {
                prev_time = time(NULL);
            }
            break;
        case EV_CANCEL:
            arim_reset_msg_rpt_state();
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_MSG_SEND_CAN);
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out waiting for ack */
            if (t > prev_time + ack_timeout) {
                if (++rcv_nak_cnt > arim_get_send_repeats()) {
                    /* timeout, all done */
                    arim_reset_msg_rpt_state();
                    arim_set_state(ST_IDLE);
                    ui_set_status_dirty(STATUS_ACK_TIMEOUT);
                } else {
                    if (fecmode_downshift)
                        arim_fecmode_downshift();
                    ui_queue_data_out(msg_buffer);
                    prev_time = t;
                    /* prime buffer count because update from TNC not immediate */
                    pthread_mutex_lock(&mutex_tnc_set);
                    snprintf(g_tnc_settings[g_cur_tnc].buffer,
                        sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", strlen(msg_buffer));
                    pthread_mutex_unlock(&mutex_tnc_set);
                    arim_set_state(ST_SEND_MSG_BUF_WAIT);
                    ui_set_status_dirty(STATUS_MSG_REPEAT);
                }
            }
            break;
        }
        break;
    case ST_ARQ_PEND_WAIT:
        switch(event) {
        case EV_RCV_PING:
            /* a PING asynch response was received from the TNC */
            if (arim_proc_ping()) {
                arim_set_state(ST_SEND_PING_ACK_PEND);
                ui_set_status_dirty(STATUS_PING_RCVD);
            } else {
                arim_set_state(ST_IDLE);
            }
            break;
        case EV_ARQ_CAN_PENDING:
            /* a CANCELPENDING asynch response was received from the TNC */
            arim_set_state(ST_IDLE);
            break;
        case EV_ARQ_TARGET:
            /* a TARGET asynch response was received from the TNC */
            if (arim_on_arq_target()) {
                ack_timeout = ARDOP_CONNREQ_TIMEOUT;
                t = time(NULL);
                arim_set_state(ST_ARQ_IN_CONNECT_WAIT);
            } else {
                arim_set_state(ST_IDLE);
            }
            break;
        case EV_ARQ_REJ_BUSY:
            /* a REJECTEDBUSY asynch response was received from the TNC */
            arim_set_state(ST_IDLE);
            break;
        case EV_ARQ_REJ_BW:
            /* a REJECTEDBW asynch response was received from the TNC */
            arim_set_state(ST_IDLE);
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out waiting */
            if (t > prev_time + ack_timeout) {
                /* timeout, all done */
                arim_set_state(ST_IDLE);
            }
            break;
        }
        break;
    case ST_SEND_PING_ACK_PEND:
        switch(event) {
        case EV_SEND_PING_ACK:
            /* a PINGREPLY asynch response was received from the TNC */
            ui_set_status_dirty(STATUS_ACKNAK_SEND);
            arim_set_state(ST_IDLE);
            break;
        case EV_ARQ_CAN_PENDING:
            /* a CANCELPENDING asynch response was received from the TNC.
               This can happen if a ping arrives when ENABLEPINGACK is false
               but LISTEN is true. In this case the ping packet is decoded
               and a PING async response is sent. This is is followed by a
               CANCELPENDING so it must be handled here */
            arim_set_state(ST_IDLE);
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out waiting for ack send */
            if (t > prev_time + ack_timeout) {
                /* timeout, all done */
                arim_set_state(ST_IDLE);
            }
            break;
        }
        break;
    case ST_RCV_PING_ACK_WAIT:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_PING_SEND_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* ping repeat, reload ack timer */
                prev_time = time(NULL);
                arim_recv_ping_ack_ptt(1);
                ui_set_status_dirty(STATUS_PING_SENT);
            }
            break;
        case EV_RCV_PING_ACK:
            /* a PINGACK async response was received from the TNC */
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_PING_ACK_RCVD);
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out waiting for ack */
            if (t > prev_time + ack_timeout) {
                /* timeout, all done */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_PING_ACK_TIMEOUT);
            }
            break;
        }
        break;
    case ST_ARQ_OUT_CONNECT_WAIT:
        /* handle every case encountered in testing to date,
           with a timeout mechanism as additional protection */
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* connection request repeat, reload ack timer */
                prev_time = time(NULL);
                arim_send_arq_conn_req_ptt(1);
                ui_set_status_dirty(STATUS_ARQ_CONN_REQ_SENT);
            }
            break;
        case EV_ARQ_CONNECTED:
            /* a CONNECTED async response was received from the TNC */
            arim_set_state(ST_ARQ_CONNECTED);
            arim_on_arq_connected();
            ui_set_status_dirty(STATUS_ARQ_CONNECTED);
            break;
        case EV_ARQ_DISCONNECTED:
            /* a DISCONNECTED async response was received from the TNC */
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_REFRESH);
            break;
        case EV_TNC_NEWSTATE:
            /* this may be the first or only notice of disconnection in some cases */
            arim_copy_tnc_state(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "DISC", 4))
                arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_REFRESH);
            break;
        case EV_ARQ_REJ_BUSY:
            /* a REJECTEDBUSY asynch response was received from the TNC */
            arim_set_state(ST_IDLE);
            break;
        case EV_ARQ_REJ_BW:
            /* a REJECTEDBW asynch response was received from the TNC */
            arim_set_state(ST_IDLE);
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out waiting for connection ack */
            if (t > prev_time + ack_timeout) {
                /* timeout, all done */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_ARQ_CONN_REQ_TO);
            }
            break;
        }
        break;
    case ST_RCV_ARQ_CONN_PP_WAIT:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* ping repeat, reload ack timer */
                prev_time = time(NULL);
                arim_recv_ping_ack_ptt(1);
                ui_set_status_dirty(STATUS_PING_SENT);
            }
            break;
        case EV_RCV_PING_ACK:
            /* a PINGACK async response was received from the TNC */
            if (param < 0) {
                /* pingack quality below threshold, cancel connection request */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_ARQ_CONN_PP_ACK_BAD);
            } else {
                arim_set_state(ST_IDLE);
                /* connection request send will be scheduled by this call */
                ui_set_status_dirty(STATUS_ARQ_CONN_PP_SEND);
            }
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out waiting for ping ack */
            if (t > prev_time + ack_timeout) {
                /* timeout, all done */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_ARQ_CONN_PP_ACK_TO);
            }
            break;
        }
        break;
    case ST_ARQ_IN_CONNECT_WAIT:
        /* handle every case encountered in testing to date,
           with a timeout mechanism as additional protection */
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_ARQ_CONNECTED:
            /* a CONNECTED async response was received from the TNC */
            ack_timeout = ARDOP_CONNREQ_TIMEOUT;
            prev_time = time(NULL);
            arim_set_state(ST_ARQ_CONNECTED);
            arim_on_arq_connected();
            ui_set_status_dirty(STATUS_ARQ_CONNECTED);
            break;
        case EV_ARQ_DISCONNECTED:
            /* a DISCONNECTED async response was received from the TNC */
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_REFRESH);
            break;
        case EV_TNC_NEWSTATE:
            /* this may be the first or only notice of disconnection in some cases */
            arim_copy_tnc_state(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "DISC", 4))
                arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_REFRESH);
            break;
        case EV_ARQ_REJ_BUSY:
            /* a REJECTEDBUSY asynch response was received from the TNC */
            arim_set_state(ST_IDLE);
            break;
        case EV_ARQ_REJ_BW:
            /* a REJECTEDBW asynch response was received from the TNC */
            arim_set_state(ST_IDLE);
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out waiting */
            if (t > prev_time + ack_timeout) {
                /* timeout, all done */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        }
        break;
    case ST_ARQ_CONNECTED:
        /* handle every case encountered in testing to date */
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* still connected, reload timer */
                prev_time = time(NULL);
            }
            break;
        case EV_ARQ_FILE_SEND_CMD:
            /* start sending file */
            arim_set_state(ST_ARQ_FILE_SEND_WAIT);
            ack_timeout = ARDOP_CONNREQ_TIMEOUT;
            prev_time = time(NULL);
            ui_set_status_dirty(STATUS_ARQ_FILE_SEND);
            break;
        case EV_ARQ_FILE_RCV_WAIT:
            /* wait for incoming /fput command */
            arim_set_state(ST_ARQ_FILE_RCV_WAIT);
            ack_timeout = ARDOP_CONNREQ_TIMEOUT;
            prev_time = time(NULL);
            ui_set_status_dirty(STATUS_ARQ_FILE_RCV_WAIT);
            break;
        case EV_ARQ_FILE_RCV:
            /* start receiving file */
            arim_set_state(ST_ARQ_FILE_RCV);
            ack_timeout = ARDOP_CONNREQ_TIMEOUT;
            prev_time = time(NULL);
            ui_set_status_dirty(STATUS_ARQ_FILE_RCV);
            break;
        case EV_ARQ_MSG_SEND_CMD:
            /* start sending message */
            arim_set_state(ST_ARQ_MSG_SEND_WAIT);
            ack_timeout = ARDOP_CONNREQ_TIMEOUT;
            prev_time = time(NULL);
            ui_set_status_dirty(STATUS_ARQ_MSG_SEND);
            break;
        case EV_ARQ_MSG_RCV:
            /* start receiving message */
            arim_set_state(ST_ARQ_MSG_RCV);
            ack_timeout = ARDOP_CONNREQ_TIMEOUT;
            prev_time = time(NULL);
            ui_set_status_dirty(STATUS_ARQ_MSG_RCV);
            break;
        case EV_ARQ_DISCONNECTED:
            /* a DISCONNECTED async response was received from the TNC */
            arim_set_state(ST_IDLE);
            arim_on_arq_disconnected();
            ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            break;
        case EV_TNC_NEWSTATE:
            arim_copy_tnc_state(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "DISC", 4)) {
                /* this may be the only notice of disconnection in some cases */
                arim_set_state(ST_IDLE);
                arim_on_arq_disconnected();
                ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            } else {
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out */
            if (t > prev_time + ack_timeout) {
                /* timeout, return to idle state */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        }
        break;
    case ST_ARQ_FILE_SEND_WAIT:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* still sending, reload timer */
                prev_time = time(NULL);
            }
            break;
        case EV_ARQ_CANCEL_WAIT:
            /* wait canceled, return to connected state */
            arim_set_state(ST_ARQ_CONNECTED);
            break;
        case EV_ARQ_DISCONNECTED:
            /* a DISCONNECTED async response was received from the TNC */
            arim_set_state(ST_IDLE);
            arim_on_arq_disconnected();
            ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            break;
        case EV_TNC_NEWSTATE:
            arim_copy_tnc_state(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "DISC", 4)) {
                /* this may be the only notice of disconnection in some cases */
                arim_set_state(ST_IDLE);
                arim_on_arq_disconnected();
                ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            } else {
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        case EV_PERIODIC:
            if (!arim_get_buffer_cnt()) {
                /* done sending cmd, will send file next */
                ack_timeout = ARDOP_CONNREQ_TIMEOUT;
                prev_time = time(NULL);
                arim_set_state(ST_ARQ_FILE_SEND);
                arim_on_arq_file_send_cmd();
                ui_set_status_dirty(STATUS_ARQ_FILE_SEND);
            } else {
                t = time(NULL);
                /* see if we timed out */
                if (t > prev_time + ack_timeout) {
                    /* timeout, return to connected state */
                    ack_timeout = ARDOP_CONNREQ_TIMEOUT;
                    prev_time = time(NULL);
                    arim_set_state(ST_ARQ_CONNECTED);
                    ui_set_status_dirty(STATUS_REFRESH);
                }
            }
            break;
        }
        break;
    case ST_ARQ_FILE_SEND:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* still sending, reload timer */
                prev_time = time(NULL);
            }
            break;
        case EV_ARQ_FILE_OK:
            /* success */
            arim_set_state(ST_ARQ_CONNECTED);
            ui_set_status_dirty(STATUS_ARQ_FILE_SEND_ACK);
            break;
        case EV_ARQ_FILE_ERROR:
            /* something went wrong */
            arim_set_state(ST_ARQ_CONNECTED);
            ui_set_status_dirty(STATUS_REFRESH);
            break;
        case EV_ARQ_DISCONNECTED:
            /* a DISCONNECTED async response was received from the TNC */
            arim_set_state(ST_IDLE);
            arim_on_arq_disconnected();
            ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            break;
        case EV_TNC_NEWSTATE:
            arim_copy_tnc_state(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "DISC", 4)) {
                /* this may be the only notice of disconnection in some cases */
                arim_set_state(ST_IDLE);
                arim_on_arq_disconnected();
                ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            } else {
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        case EV_PERIODIC:
            if (!arim_on_arq_file_send_buffer(arim_get_buffer_cnt())) {
                /* done sending file, will wait for ack */
                ui_set_status_dirty(STATUS_ARQ_FILE_SEND_DONE);
            } else {
                t = time(NULL);
                /* see if we timed out */
                if (t > prev_time + ack_timeout) {
                    /* timeout, return to connected state */
                    ack_timeout = ARDOP_CONNREQ_TIMEOUT;
                    prev_time = time(NULL);
                    arim_set_state(ST_ARQ_CONNECTED);
                    ui_set_status_dirty(STATUS_REFRESH);
                }
            }
            break;
        }
        break;
    case ST_ARQ_FILE_RCV_WAIT:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* still sending, reload timer */
                prev_time = time(NULL);
            }
            break;
        case EV_ARQ_CANCEL_WAIT:
            /* wait canceled, return to connected state */
            arim_set_state(ST_ARQ_CONNECTED);
            break;
        case EV_ARQ_FILE_RCV:
            /* start receiving file */
            arim_set_state(ST_ARQ_FILE_RCV);
            ack_timeout = ARDOP_CONNREQ_TIMEOUT;
            prev_time = time(NULL);
            ui_set_status_dirty(STATUS_ARQ_FILE_RCV);
            break;
        case EV_ARQ_FILE_ERROR:
            /* something went wrong */
            arim_set_state(ST_ARQ_CONNECTED);
            ui_set_status_dirty(STATUS_ARQ_FILE_RCV_ERROR);
            break;
        case EV_ARQ_DISCONNECTED:
            /* a DISCONNECTED async response was received from the TNC */
            arim_set_state(ST_IDLE);
            arim_on_arq_disconnected();
            ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            break;
        case EV_TNC_NEWSTATE:
            arim_copy_tnc_state(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "DISC", 4)) {
                /* this may be the only notice of disconnection in some cases */
                arim_set_state(ST_IDLE);
                arim_on_arq_disconnected();
                ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            } else {
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out */
            if (t > prev_time + ack_timeout) {
                /* timeout, return to connected state */
                ack_timeout = ARDOP_CONNREQ_TIMEOUT;
                prev_time = time(NULL);
                arim_set_state(ST_ARQ_CONNECTED);
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        }
        break;
    case ST_ARQ_FILE_RCV:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* still sending, reload timer */
                prev_time = time(NULL);
            }
            break;
        case EV_ARQ_FILE_RCV_FRAME:
            /* still receiving, reload timer */
            prev_time = time(NULL);
            ui_set_status_dirty(STATUS_ARQ_FILE_RCV);
            break;
        case EV_ARQ_FILE_RCV_DONE:
            /* done */
            arim_set_state(ST_ARQ_CONNECTED);
            ui_set_status_dirty(STATUS_ARQ_FILE_RCV_DONE);
            break;
        case EV_ARQ_FILE_ERROR:
            /* done */
            arim_set_state(ST_ARQ_CONNECTED);
            ui_set_status_dirty(STATUS_ARQ_FILE_RCV_ERROR);
            break;
        case EV_ARQ_DISCONNECTED:
            /* a DISCONNECTED async response was received from the TNC */
            arim_set_state(ST_IDLE);
            arim_on_arq_disconnected();
            ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            break;
        case EV_TNC_NEWSTATE:
            arim_copy_tnc_state(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "DISC", 4)) {
                /* this may be the only notice of disconnection in some cases */
                arim_set_state(ST_IDLE);
                arim_on_arq_disconnected();
                ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            } else {
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out */
            if (t > prev_time + ack_timeout) {
                /* timeout, return to connected state */
                ack_timeout = ARDOP_CONNREQ_TIMEOUT;
                prev_time = time(NULL);
                arim_set_state(ST_ARQ_CONNECTED);
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        }
        break;
    case ST_ARQ_MSG_SEND_WAIT:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* still sending, reload timer */
                prev_time = time(NULL);
            }
            break;
        case EV_ARQ_CANCEL_WAIT:
            /* wait canceled, return to connected state */
            arim_set_state(ST_ARQ_CONNECTED);
            break;
        case EV_ARQ_DISCONNECTED:
            /* a DISCONNECTED async response was received from the TNC */
            arim_set_state(ST_IDLE);
            arim_on_arq_disconnected();
            ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            break;
        case EV_TNC_NEWSTATE:
            arim_copy_tnc_state(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "DISC", 4)) {
                /* this may be the only notice of disconnection in some cases */
                arim_set_state(ST_IDLE);
                arim_on_arq_disconnected();
                ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            } else {
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        case EV_PERIODIC:
            if (!arim_get_buffer_cnt()) {
                /* done sending cmd, will send message next */
                ack_timeout = ARDOP_CONNREQ_TIMEOUT;
                prev_time = time(NULL);
                arim_set_state(ST_ARQ_MSG_SEND);
                arim_on_arq_msg_send_cmd();
                ui_set_status_dirty(STATUS_ARQ_MSG_SEND);
            } else {
                t = time(NULL);
                /* see if we timed out */
                if (t > prev_time + ack_timeout) {
                    /* timeout, return to connected state */
                    ack_timeout = ARDOP_CONNREQ_TIMEOUT;
                    prev_time = time(NULL);
                    arim_set_state(ST_ARQ_CONNECTED);
                    ui_set_status_dirty(STATUS_REFRESH);
                }
            }
            break;
        }
        break;
    case ST_ARQ_MSG_SEND:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* still sending, reload timer */
                prev_time = time(NULL);
            }
            break;
        case EV_ARQ_MSG_OK:
            /* success */
            arim_set_state(ST_ARQ_CONNECTED);
            ui_set_status_dirty(STATUS_ARQ_MSG_SEND_ACK);
            break;
        case EV_ARQ_MSG_ERROR:
            /* something went wrong */
            arim_set_state(ST_ARQ_CONNECTED);
            ui_set_status_dirty(STATUS_REFRESH);
            break;
        case EV_ARQ_DISCONNECTED:
            /* a DISCONNECTED async response was received from the TNC */
            arim_set_state(ST_IDLE);
            arim_on_arq_disconnected();
            ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            break;
        case EV_TNC_NEWSTATE:
            arim_copy_tnc_state(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "DISC", 4)) {
                /* this may be the only notice of disconnection in some cases */
                arim_set_state(ST_IDLE);
                arim_on_arq_disconnected();
                ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            } else {
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        case EV_PERIODIC:
            if (!arim_on_arq_msg_send_buffer(arim_get_buffer_cnt())) {
                /* done sending message, will wait for ack */
                ui_set_status_dirty(STATUS_ARQ_MSG_SEND_DONE);
            } else {
                t = time(NULL);
                /* see if we timed out */
                if (t > prev_time + ack_timeout) {
                    /* timeout, return to connected state */
                    ack_timeout = ARDOP_CONNREQ_TIMEOUT;
                    prev_time = time(NULL);
                    arim_set_state(ST_ARQ_CONNECTED);
                    ui_set_status_dirty(STATUS_REFRESH);
                }
            }
            break;
        }
        break;
    case ST_ARQ_MSG_RCV:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARQ_CONN_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* still sending, reload timer */
                prev_time = time(NULL);
            }
            break;
        case EV_ARQ_MSG_RCV_FRAME:
            /* still receiving, reload timer */
            prev_time = time(NULL);
            ui_set_status_dirty(STATUS_ARQ_MSG_RCV);
            break;
        case EV_ARQ_MSG_RCV_DONE:
            /* done */
            arim_set_state(ST_ARQ_CONNECTED);
            ui_set_status_dirty(STATUS_ARQ_MSG_RCV_DONE);
            break;
        case EV_ARQ_MSG_ERROR:
            /* done */
            arim_set_state(ST_ARQ_CONNECTED);
            ui_set_status_dirty(STATUS_ARQ_MSG_RCV_ERROR);
            break;
        case EV_ARQ_DISCONNECTED:
            /* a DISCONNECTED async response was received from the TNC */
            arim_set_state(ST_IDLE);
            arim_on_arq_disconnected();
            ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            break;
        case EV_TNC_NEWSTATE:
            arim_copy_tnc_state(buffer, sizeof(buffer));
            if (!strncasecmp(buffer, "DISC", 4)) {
                /* this may be the only notice of disconnection in some cases */
                arim_set_state(ST_IDLE);
                arim_on_arq_disconnected();
                ui_set_status_dirty(STATUS_ARQ_DISCONNECTED);
            } else {
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out */
            if (t > prev_time + ack_timeout) {
                /* timeout, return to connected state */
                ack_timeout = ARDOP_CONNREQ_TIMEOUT;
                prev_time = time(NULL);
                arim_set_state(ST_ARQ_CONNECTED);
                ui_set_status_dirty(STATUS_REFRESH);
            }
            break;
        }
        break;
    case ST_RCV_MSG_PING_ACK_WAIT:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_MSG_SEND_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* ping repeat, reload ack timer */
                prev_time = time(NULL);
                arim_recv_ping_ack_ptt(1);
                ui_set_status_dirty(STATUS_PING_SENT);
            }
            break;
        case EV_RCV_PING_ACK:
            /* a PINGACK async response was received from the TNC */
            if (param < 0) {
                /* pingack quality below threshold, cancel message send */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_PING_MSG_ACK_BAD);
            } else {
                arim_set_state(ST_IDLE);
                /* message send will be scheduled by this call */
                ui_set_status_dirty(STATUS_PING_MSG_SEND);
            }
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out waiting for ack */
            if (t > prev_time + ack_timeout) {
                /* timeout, all done */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_PING_MSG_ACK_TO);
            }
            break;
        }
        break;
    case ST_RCV_QRY_PING_ACK_WAIT:
        switch (event) {
        case EV_CANCEL:
            ui_queue_cmd_out("ABORT");
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_PING_SEND_CAN);
            break;
        case EV_TNC_PTT:
            /* a PTT async response was received from the TNC */
            if (param) {
                /* ping repeat, reload ack timer */
                prev_time = time(NULL);
                arim_recv_ping_ack_ptt(1);
                ui_set_status_dirty(STATUS_PING_SENT);
            }
            break;
        case EV_RCV_PING_ACK:
            /* a PINGACK async response was received from the TNC */
            if (param < 0) {
                /* pingack quality below threshold, cancel query send */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_PING_QRY_ACK_BAD);
            } else {
                arim_set_state(ST_IDLE);
                /* query send will be scheduled by this call */
                ui_set_status_dirty(STATUS_PING_QRY_SEND);
            }
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out waiting for ack */
            if (t > prev_time + ack_timeout) {
                /* timeout, all done */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_PING_QRY_ACK_TO);
            }
            break;
        }
        break;
    case ST_RCV_RESP_WAIT:
        switch (event) {
        case EV_FRAME_START:
            if (param == 'R')
                ui_set_status_dirty(STATUS_RESP_START);
            break;
        case EV_RCV_RESP:
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_RESP_RCVD);
            break;
        case EV_CANCEL:
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_RESP_WAIT_CAN);
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out waiting for response frame. If TNC is receiving reload
               the timer. The purpose of this state is to block attempts to transmit while
               receiving an incoming response frame. */
            if (arim_is_receiving()) {
                prev_time = t;
            } else if (t > prev_time + ack_timeout) {
                /* timeout, all done */
                arim_set_state(ST_IDLE);
                ui_set_status_dirty(STATUS_RESP_TIMEOUT);
            }
            break;
        }
        break;
    case ST_RCV_FRAME_WAIT:
        switch (event) {
        case EV_FRAME_TO:
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_ARIM_FRAME_TO);
            break;
        case EV_FRAME_END:
            switch (param) {
            case 'M':
                ui_set_status_dirty(STATUS_MSG_END);
                break;
            case 'Q':
                ui_set_status_dirty(STATUS_QRY_END);
                break;
            case 'R':
                ui_set_status_dirty(STATUS_RESP_END);
                break;
            case 'B':
                ui_set_status_dirty(STATUS_BCN_END);
                break;
            default:
                ui_set_status_dirty(STATUS_FRAME_END);
                break;
            }
            arim_set_state(ST_IDLE);
            break;
        case EV_RCV_MSG:
            prev_time = time(NULL);
            arim_set_state(ST_SEND_ACKNAK_PEND);
            ui_set_status_dirty(STATUS_MSG_RCVD);
            break;
        case EV_RCV_QRY:
            prev_time = time(NULL);
            arim_set_state(ST_SEND_RESP_PEND);
            ui_set_status_dirty(STATUS_QUERY_RCVD);
            break;
        case EV_RCV_NET_MSG:
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_NET_MSG_RCVD);
            break;
        case EV_CANCEL:
            arim_set_state(ST_IDLE);
            ui_set_status_dirty(STATUS_FRAME_WAIT_CAN);
            break;
        case EV_PERIODIC:
            t = time(NULL);
            /* see if we timed out while receiving ARIM frame not addressed to this TNC. If
               TNC is receiving reload the timer. The purpose of this state is to block attempts
               to transmit while receiving an incoming ARIM frame. */
            if (arim_is_receiving()) {
                prev_time = t;
            } else if (t > prev_time + ack_timeout) {
                /* timeout, all done */
                arim_set_state(ST_IDLE);
            }
            break;
        }
        break;
    }
    next_state = arim_get_state();
    if (event != EV_PERIODIC || prev_state != next_state) {
        snprintf(buffer, sizeof(buffer),
            "ARIM: Event %s, Param %d, State %s==>%s",
                events[event], param, states[prev_state], states[next_state]);
        arim_queue_debug_log(buffer);
    }
}

