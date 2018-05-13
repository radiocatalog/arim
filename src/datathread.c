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
#include <time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <ctype.h>
#include "main.h"
#include "datathread.h"
#include "arim.h"
#include "arim_proto.h"
#include "arim_arq.h"
#include "arim_arq_files.h"
#include "arim_arq_msg.h"
#include "bufq.h"
#include "ini.h"
#include "log.h"
#include "ui.h"
#include "util.h"

static int arim_data_waiting = 0;
static time_t arim_start_time = 0;
static int mon_timestamp_en;
static size_t num_bytes_in, num_bytes_out;

void datathread_inc_num_bytes_in(size_t num)
{
    pthread_mutex_lock(&mutex_num_bytes);
    num_bytes_in += num;
    pthread_mutex_unlock(&mutex_num_bytes);
}

void datathread_inc_num_bytes_out(size_t num)
{
    pthread_mutex_lock(&mutex_num_bytes);
    num_bytes_out += num;
    pthread_mutex_unlock(&mutex_num_bytes);
}

size_t datathread_get_num_bytes_in()
{
    size_t num;

    pthread_mutex_lock(&mutex_num_bytes);
    num = num_bytes_in;
    pthread_mutex_unlock(&mutex_num_bytes);
    return num;
}

size_t datathread_get_num_bytes_out()
{
    size_t num;

    pthread_mutex_lock(&mutex_num_bytes);
    num = num_bytes_out;
    pthread_mutex_unlock(&mutex_num_bytes);
    return num;
}

void datathread_reset_num_bytes()
{
    pthread_mutex_lock(&mutex_num_bytes);
    num_bytes_out = num_bytes_in = 0;
    pthread_mutex_unlock(&mutex_num_bytes);
}

void datathread_queue_heard(const char *text)
{
    pthread_mutex_lock(&mutex_heard);
    cmdq_push(&g_heard_q, text);
    pthread_mutex_unlock(&mutex_heard);
}

void datathread_queue_traffic_log(const char *text)
{
    char buffer[MIN_MSG_BUF_SIZE];
    char timestamp[MAX_TIMESTAMP_SIZE];

    if (g_traffic_log_enable) {
        snprintf(buffer, sizeof(buffer), "[%s] %s",
                util_timestamp(timestamp, sizeof(timestamp)), text);
        pthread_mutex_lock(&mutex_traffic_log);
        dataq_push(&g_traffic_log_q, buffer);
        pthread_mutex_unlock(&mutex_traffic_log);
    }
}

void datathread_queue_debug_log(const char *text)
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

void datathread_queue_data_in(const char *text)
{
    char buffer[MIN_MSG_BUF_SIZE+MAX_TIMESTAMP_SIZE];
    char timestamp[MAX_TIMESTAMP_SIZE];

    pthread_mutex_lock(&mutex_data_in);
    if (mon_timestamp_en) {
        snprintf(buffer, sizeof(buffer), "[%s] %s",
                util_timestamp(timestamp, sizeof(timestamp)), text);
        dataq_push(&g_data_in_q, buffer);
    } else {
        dataq_push(&g_data_in_q, text);
    }
    pthread_mutex_unlock(&mutex_data_in);
}

void datathread_send_file_out(int sock)
{
    char *p, buffer[MAX_FILE_SIZE+4];
    unsigned char *s;
    FILEQUEUEITEM *item;
    size_t sent, nblk, nrem;
    int i;

    pthread_mutex_lock(&mutex_file_out);
    item = fileq_pop(&g_file_out_q);
    pthread_mutex_unlock(&mutex_file_out);
    if (!item)
        return;
    datathread_queue_debug_log("Data thread: sending file to TNC");
    p = buffer;
    s = item->data;
    nblk = item->size / TNC_DATA_BLOCK_SIZE;
    nrem = item->size % TNC_DATA_BLOCK_SIZE;
    for (i = 0; i < nblk; i++) {
        datathread_queue_debug_log("Data thread: writing block of data to socket");
        *p++ = (TNC_DATA_BLOCK_SIZE >> 8) & 0xFF;
        *p++ = TNC_DATA_BLOCK_SIZE & 0xFF;
        memcpy(p, s, TNC_DATA_BLOCK_SIZE);
        sent = write(sock, buffer, TNC_DATA_BLOCK_SIZE + 2);
        if (sent < 0) {
            datathread_queue_debug_log("Data thread: write to socket failed");
            return;
        }
        s += TNC_DATA_BLOCK_SIZE;
        p = buffer;
        datathread_inc_num_bytes_out(TNC_DATA_BLOCK_SIZE);
        usleep(TNC_DATA_WAIT_TIME); /* give TNC time to process data */
    }
    if (nrem) {
        datathread_queue_debug_log("Data thread: writing remainder of data to socket");
        *p++ = (nrem >> 8) & 0xFF;
        *p++ = nrem & 0xFF;
        memcpy(p, s, nrem);
        sent = write(sock, buffer, nrem + 2);
        if (sent < 0) {
            datathread_queue_debug_log("Data thread: write to socket failed");
            return;
        }
        datathread_inc_num_bytes_out(nrem + 2);
        usleep(TNC_DATA_WAIT_TIME); /* give TNC time to process data */
    }
}

void datathread_send_msg_out(int sock)
{
    char *p, *s, buffer[MIN_MSG_BUF_SIZE];
    MSGQUEUEITEM *item;
    size_t sent, nblk, nrem;
    int i;

    pthread_mutex_lock(&mutex_msg_out);
    item = msgq_pop(&g_msg_out_q);
    pthread_mutex_unlock(&mutex_msg_out);
    if (!item)
        return;
    datathread_queue_debug_log("Data thread: sending message to TNC");
    p = buffer;
    s = item->data;
    nblk = item->size / TNC_DATA_BLOCK_SIZE;
    nrem = item->size % TNC_DATA_BLOCK_SIZE;
    for (i = 0; i < nblk; i++) {
        datathread_queue_debug_log("Data thread: writing block of data to socket");
        *p++ = (TNC_DATA_BLOCK_SIZE >> 8) & 0xFF;
        *p++ = TNC_DATA_BLOCK_SIZE & 0xFF;
        memcpy(p, s, TNC_DATA_BLOCK_SIZE);
        sent = write(sock, buffer, TNC_DATA_BLOCK_SIZE + 2);
        if (sent < 0) {
            datathread_queue_debug_log("Data thread: write to socket failed");
            return;
        }
        s += TNC_DATA_BLOCK_SIZE;
        p = buffer;
        datathread_inc_num_bytes_out(TNC_DATA_BLOCK_SIZE);
        usleep(TNC_DATA_WAIT_TIME); /* give TNC time to process data */
    }
    if (nrem) {
        datathread_queue_debug_log("Data thread: writing remainder of data to socket");
        *p++ = (nrem >> 8) & 0xFF;
        *p++ = nrem & 0xFF;
        memcpy(p, s, nrem);
        sent = write(sock, buffer, nrem + 2);
        if (sent < 0) {
            datathread_queue_debug_log("Data thread: write to socket failed");
            return;
        }
        datathread_inc_num_bytes_out(nrem + 2);
        usleep(TNC_DATA_WAIT_TIME); /* give TNC time to process data */
    }
}

void datathread_send_data_out(int sock)
{
    char *p, *s, *data;
    char buffer[MIN_MSG_BUF_SIZE];
    size_t len, sent, nblk, nrem;
    int i, state;

    pthread_mutex_lock(&mutex_data_out);
    data = dataq_pop(&g_data_out_q);
    pthread_mutex_unlock(&mutex_data_out);
    if (!data)
        return;
    datathread_queue_debug_log("Data thread: sending data to TNC");
    len = strlen(data);
    p = buffer;
    s = data;
    nblk = len / TNC_DATA_BLOCK_SIZE;
    nrem = len % TNC_DATA_BLOCK_SIZE;
    for (i = 0; i < nblk; i++) {
        datathread_queue_debug_log("Data thread: writing block of data to socket");
        *p++ = (TNC_DATA_BLOCK_SIZE >> 8) & 0xFF;
        *p++ = TNC_DATA_BLOCK_SIZE & 0xFF;
        memcpy(p, s, TNC_DATA_BLOCK_SIZE);
        sent = write(sock, buffer, TNC_DATA_BLOCK_SIZE + 2);
        if (sent < 0) {
            datathread_queue_debug_log("Data thread: write to socket failed");
            return;
        }
        s += TNC_DATA_BLOCK_SIZE;
        p = buffer;
        datathread_inc_num_bytes_out(TNC_DATA_BLOCK_SIZE);
        usleep(TNC_DATA_WAIT_TIME); /* give TNC time to process data */
    }
    if (nrem) {
        datathread_queue_debug_log("Data thread: writing remainder of data to socket");
        *p++ = (nrem >> 8) & 0xFF;
        *p++ = nrem & 0xFF;
        memcpy(p, s, nrem);
        sent = write(sock, buffer, nrem + 2);
        if (sent < 0) {
            datathread_queue_debug_log("Data thread: write to socket failed");
            return;
        }
        datathread_inc_num_bytes_out(nrem + 2);
        usleep(TNC_DATA_WAIT_TIME); /* give TNC time to process data */
    }
    state = arim_get_state();
    if (arim_test_frame(data, len))
        snprintf(buffer, sizeof(buffer), "<< [%c] %s", data[1], data);
    else if (state == ST_ARQ_CONNECTED          ||
             state == ST_ARQ_FILE_RCV           ||
             state == ST_ARQ_FILE_RCV_WAIT      ||
             state == ST_ARQ_FILE_RCV_WAIT_OK   ||
             state == ST_ARQ_FILE_SEND_WAIT     ||
             state == ST_ARQ_FILE_SEND_WAIT_OK  ||
             state == ST_ARQ_FILE_SEND          ||
             state == ST_ARQ_FLIST_RCV          ||
             state == ST_ARQ_FLIST_RCV_WAIT     ||
             state == ST_ARQ_FLIST_SEND_WAIT    ||
             state == ST_ARQ_FLIST_SEND         ||
             state == ST_ARQ_AUTH_SEND_A1       ||
             state == ST_ARQ_AUTH_SEND_A2       ||
             state == ST_ARQ_AUTH_SEND_A3       ||
             state == ST_ARQ_AUTH_RCV_A2_WAIT   ||
             state == ST_ARQ_AUTH_RCV_A3_WAIT   ||
             state == ST_ARQ_AUTH_RCV_A4_WAIT   ||
             state == ST_ARQ_MSG_SEND_WAIT      ||
             state == ST_ARQ_MSG_SEND)
        snprintf(buffer, sizeof(buffer), "<< [@] %s", data);
    else
        snprintf(buffer, sizeof(buffer), "<< [U] %s", data);
    datathread_queue_data_in(buffer);
    datathread_queue_traffic_log(buffer);
    if (state != ST_ARQ_CONNECTED           &&
        state != ST_ARQ_FILE_RCV            &&
        state != ST_ARQ_FILE_RCV_WAIT       &&
        state != ST_ARQ_FILE_RCV_WAIT_OK    &&
        state != ST_ARQ_FILE_SEND_WAIT      &&
        state != ST_ARQ_FILE_SEND_WAIT_OK   &&
        state != ST_ARQ_FILE_SEND           &&
        state != ST_ARQ_FLIST_RCV           &&
        state != ST_ARQ_FLIST_RCV_WAIT      &&
        state != ST_ARQ_FLIST_SEND_WAIT     &&
        state != ST_ARQ_FLIST_SEND          &&
        state != ST_ARQ_AUTH_SEND_A1        &&
        state != ST_ARQ_AUTH_SEND_A2        &&
        state != ST_ARQ_AUTH_SEND_A3        &&
        state != ST_ARQ_AUTH_RCV_A2_WAIT    &&
        state != ST_ARQ_AUTH_RCV_A3_WAIT    &&
        state != ST_ARQ_AUTH_RCV_A4_WAIT    &&
        state != ST_ARQ_MSG_SEND_WAIT       &&
        state != ST_ARQ_MSG_SEND)
        ui_queue_cmd_out("FECSEND TRUE");
}

void datathread_on_fec(char *data, size_t size)
{
    char inbuffer[MIN_MSG_BUF_SIZE];

    snprintf(inbuffer, size + 8, ">> [U] %s", data);
    datathread_queue_data_in(inbuffer);
    datathread_queue_traffic_log(inbuffer);
    datathread_queue_debug_log("Data thread: received ARDOP FEC frame from TNC");
}

void datathread_on_idf(char *data, size_t size)
{
    char *s, *e, inbuffer[MIN_MSG_BUF_SIZE];

    snprintf(inbuffer, size + 8, ">> [I] %s", data);
    s = strstr(inbuffer, "ID:");
    if (s) {
        e = s;
        while (isprint(*e))
            ++e;
        *e = '\0';
        datathread_queue_data_in(inbuffer);
        datathread_queue_traffic_log(inbuffer);
        s += 3;
        while (*s && *s == ' ')
            ++s;
        e = s;
        while (*e && *e != ' ')
            ++e;
        *e = '\0';
        snprintf(inbuffer, sizeof(inbuffer), "8[I] %-10s ", s);
        datathread_queue_heard(inbuffer);
        datathread_queue_debug_log("Data thread: received ARDOP IDF frame from TNC");
    } else {
        /* this sent by tnc to host when SENDID invoked */
        snprintf(inbuffer, size + 8, "<< [I] %s", data);
        datathread_queue_data_in(inbuffer);
        datathread_queue_traffic_log(inbuffer);
    }
}

void datathread_on_arq(char *data, size_t size)
{
    char *s, *e, inbuffer[MIN_MSG_BUF_SIZE], remote_call[TNC_MYCALL_SIZE];
    int state;

    datathread_queue_debug_log("Data thread: received ARDOP ARQ frame from TNC");
    state = arim_get_state();
    switch(state) {
    case ST_ARQ_FLIST_RCV:
        arim_copy_remote_call(remote_call, sizeof(remote_call));
        snprintf(inbuffer, sizeof(inbuffer), "9[@] %-10s ", remote_call);
        datathread_queue_heard(inbuffer);
        arim_arq_files_flist_on_rcv_frame(data, size);
        break;
    case ST_ARQ_FILE_RCV:
        arim_copy_remote_call(remote_call, sizeof(remote_call));
        snprintf(inbuffer, sizeof(inbuffer), "9[@] %-10s ", remote_call);
        datathread_queue_heard(inbuffer);
        arim_arq_files_on_rcv_frame(data, size);
        break;
    case ST_ARQ_MSG_RCV:
        arim_copy_remote_call(remote_call, sizeof(remote_call));
        snprintf(inbuffer, sizeof(inbuffer), "9[@] %-10s ", remote_call);
        datathread_queue_heard(inbuffer);
        arim_arq_msg_on_rcv_frame(data, size);
        break;
    case ST_ARQ_CONNECTED:
    case ST_ARQ_FILE_RCV_WAIT:
    case ST_ARQ_FILE_RCV_WAIT_OK:
    case ST_ARQ_FILE_SEND_WAIT:
    case ST_ARQ_FILE_SEND_WAIT_OK:
    case ST_ARQ_FILE_SEND:
    case ST_ARQ_FLIST_RCV_WAIT:
    case ST_ARQ_FLIST_SEND_WAIT:
    case ST_ARQ_FLIST_SEND:
    case ST_ARQ_MSG_SEND_WAIT:
    case ST_ARQ_MSG_SEND:
    case ST_ARQ_AUTH_RCV_A2_WAIT:
    case ST_ARQ_AUTH_RCV_A3_WAIT:
    case ST_ARQ_AUTH_RCV_A4_WAIT:
    case ST_ARQ_AUTH_SEND_A1:
    case ST_ARQ_AUTH_SEND_A2:
    case ST_ARQ_AUTH_SEND_A3:
        arim_arq_on_data(data, size);
        break;
    default:
        snprintf(inbuffer, size + 8, ">> [@] %s", data);
        s = strstr(inbuffer, ":");
        if (s) {
            e = s;
            while (isprint(*e))
                ++e;
            *e = '\0';
            datathread_queue_data_in(inbuffer);
            datathread_queue_traffic_log(inbuffer);
            ++s;
            while (*s && *s == ' ')
                ++s;
            e = s;
            while (*e && *e != ' ')
                ++e;
            *e = '\0';
            snprintf(inbuffer, sizeof(inbuffer), "9[@] %-10s ", s);
            datathread_queue_heard(inbuffer);
        }
        break;
    }
}

void datathread_on_err(char *data, size_t size)
{
    char *p, inbuffer[MIN_MSG_BUF_SIZE];

    snprintf(inbuffer, size + 8, ">> [E] %s", data);
    p = inbuffer;
    while (isprint(*p))
        ++p;
    *p = '\0';
    datathread_queue_data_in(inbuffer);
    datathread_queue_traffic_log(inbuffer);
    datathread_queue_debug_log("Data thread: received ARDOP ERR frame from TNC");
}

//#define VIEW_DATA_IN
size_t datathread_handle_data(unsigned char *data, size_t size)
{
    static unsigned char buffer[MIN_MSG_BUF_SIZE];
    static size_t cnt = 0;
    static int arim_frame_type = 0;
    int is_new_frame, is_arim_frame, datasize = 0;

#ifdef VIEW_DATA_IN
char buf[MIN_MSG_BUF_SIZE];
#endif

    if ((cnt + size) > sizeof(buffer)) {
        /* too much data, can't be a valid ARIM payload */
        cnt = 0;
        return cnt;
    }
    datathread_inc_num_bytes_in(size);
    memcpy(buffer + cnt, data, size);
    cnt += size;
    /* extract data size */
    if (cnt < 5) /* not enough data yet, wait for more */
        return cnt;
    /* is this a valid frame? */
    if ((buffer[2] == 'A' && buffer[3] == 'R' && buffer[4] == 'Q') ||
        (buffer[2] == 'F' && buffer[3] == 'E' && buffer[4] == 'C') ||
        (buffer[2] == 'E' && buffer[3] == 'R' && buffer[4] == 'R') ||
        (buffer[2] == 'I' && buffer[3] == 'D' && buffer[4] == 'F')) {
        /* yes, extract payload size */
        datasize = buffer[0] << 8;
        datasize += buffer[1];
    }
    if (datasize <= 0) {
        /* invalid frame or bad payload size */
        cnt = 0;
        return cnt;
    }
    if (datasize <= (cnt - 2)) {
        /* got all data, dispatch on frame type */
#ifdef VIEW_DATA_IN
snprintf(buf, datasize + 7, "[%04X]%s", datasize, buffer + 2);
datathread_queue_debug_log(buf);
sleep(1);
#endif
        if (buffer[2] == 'F') { /* FEC frame */
            is_arim_frame = arim_test_frame((char *)&buffer[5], datasize - 3);
            is_new_frame = (!arim_data_waiting && is_arim_frame);
            if (is_new_frame) {
                arim_frame_type = is_arim_frame;
                arim_on_event(EV_FRAME_START, arim_frame_type);
                datathread_queue_debug_log("Data thread: received start of ARIM frame");
            }
            if (arim_data_waiting || is_new_frame)
                arim_data_waiting = arim_on_data((char *)&buffer[5], datasize - 3);
            else
                datathread_on_fec((char *)&buffer[5], datasize - 3);
            /* clear start time if done, otherwise update with current time */
            if (!arim_data_waiting) {
                arim_start_time = 0;
                arim_on_event(EV_FRAME_END, arim_frame_type);
            } else {
                arim_start_time = time(NULL);
            }
        }
        else if (buffer[2] == 'I') /* IDF frame */
            datathread_on_idf((char *)&buffer[5], datasize - 3);
        else if (buffer[2] == 'A') /* ARQ frame */
            datathread_on_arq((char *)&buffer[5], datasize - 3);
        else if (buffer[2] == 'E') /* ERR frame */
            datathread_on_err((char *)&buffer[5], datasize - 3);
        /* reset buffer */
        cnt = 0;
    }
    return cnt;
}

void *datathread_func(void *data)
{
    unsigned char buffer[MIN_MSG_BUF_SIZE];
    struct addrinfo hints, *res = NULL;
    fd_set datareadfds, dataerrorfds;
    struct timeval timeout;
    ssize_t rsize;
    int result, portnum, datasock, arim_timeout;
    time_t cur_time;

    memset(&hints, 0, sizeof hints);
    datathread_queue_debug_log("Data thread: initializing");
    hints.ai_family = AF_UNSPEC;  /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    portnum = atoi(g_tnc_settings[g_cur_tnc].port) + 1;
    snprintf((char *)buffer, sizeof(buffer), "%d", portnum);
    getaddrinfo(g_tnc_settings[g_cur_tnc].ipaddr, (char *)buffer, &hints, &res);
    if (!res)
    {
        datathread_queue_debug_log("Data thread: failed to resolve IP address");
        g_datathread_stop = 1;
        pthread_exit(data);
    }
    datasock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (connect(datasock, res->ai_addr, res->ai_addrlen) == -1) {
        datathread_queue_debug_log("Data thread: failed to open TCP socket");
        g_datathread_stop = 1;
        pthread_exit(data);
    }
    freeaddrinfo(res);
    g_datathread_ready = 1;
    /* timeout specified in secs */
    arim_timeout = atoi(g_arim_settings.frame_timeout);
    arim_reset();
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4))
        mon_timestamp_en = 1;
    else
        mon_timestamp_en = 0;

    while (1) {
        FD_ZERO(&datareadfds);
        FD_ZERO(&dataerrorfds);
        FD_SET(datasock, &datareadfds);
        FD_SET(datasock, &dataerrorfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        result = select(datasock + 1, &datareadfds, (fd_set *)0, &dataerrorfds, &timeout);
        switch (result) {
        case 0:
            /* select timeout */
            arim_on_event(EV_PERIODIC, 0);
            datathread_send_data_out(datasock);
            datathread_send_file_out(datasock);
            datathread_send_msg_out(datasock);
            if (arim_data_waiting) {
                cur_time = time(NULL);
                if (cur_time - arim_start_time > arim_timeout) {
                    /* timeout, reset arim state */
                    arim_reset();
                    arim_data_waiting = arim_start_time = 0;
                    datathread_queue_debug_log("Data thread: ARIM frame time out");
                    arim_on_event(EV_FRAME_TO, 0);
                }
            }
            /* pump outbound and inbound arq line queues */
            arim_arq_on_cmd(NULL, 0);
            arim_arq_on_resp(NULL, 0);
            break;
        case -1:
            datathread_queue_debug_log("Data thread: Socket select error (-1)");
            break;
        default:
            if (FD_ISSET(datasock, &datareadfds)) {
                rsize = read(datasock, buffer, sizeof(buffer) - 1);
                if (rsize != -1)
                    datathread_handle_data(buffer, rsize);
            }
            if (FD_ISSET(datasock, &dataerrorfds)) {
                datathread_queue_debug_log("Data thread: Socket select error (FD_ISSET)");
                break;
            }
        }
        if (g_datathread_stop) {
            break;
        }
    }
    datathread_queue_debug_log("Data thread: terminating");
    sleep(2);
    close(datasock);
    return data;
}

