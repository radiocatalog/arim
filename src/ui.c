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
#include <curses.h>
#include <time.h>
#include <ctype.h>
#include "main.h"
#include "arim_proto.h"
#include "arim_beacon.h"
#include "arim_message.h"
#include "arim_query.h"
#include "arim_arq.h"
#include "arim_arq_auth.h"
#include "bufq.h"
#include "cmdproc.h"
#include "ini.h"
#include "log.h"
#include "ui.h"
#include "ui_dialog.h"
#include "ui_fec_menu.h"
#include "ui_files.h"
#include "ui_help_menu.h"
#include "ui_msg.h"
#include "util.h"

#define MAX_DATA_BUF_LEN        512
#define MAX_DATA_ROW_SIZE       512
#define DATA_BUF_SCROLLING_TIME 300
#define DATA_WIN_SCROLL_LEGEND  "Scrolling: UP DOWN PAGEUP PAGEDOWN HOME END, 'c' to cancel. "
#define MAX_CMD_HIST            15+1

WINDOW *main_win;
WINDOW *ui_list_win;
WINDOW *ui_list_box;
WINDOW *ui_recents_win;
WINDOW *ui_ptable_win;
WINDOW *tnc_data_win;
WINDOW *tnc_data_box;
WINDOW *tnc_cmd_win;
WINDOW *tnc_cmd_box;
WINDOW *prompt_box;
WINDOW *prompt_win;

int win_change_timer;
int status_timer;
int title_dirty;
int status_dirty;
int show_recents;
int show_ptable;
int show_titles;
int show_cmds;
int last_time_heard, prev_last_time_heard = -1;
int mon_timestamp;
int color_code;

int ui_list_box_y, ui_list_box_x, ui_list_box_w, ui_list_box_h;
int tnc_data_box_y, tnc_data_box_x, tnc_data_box_w, tnc_data_box_h;
int tnc_cmd_box_y, tnc_cmd_box_x, tnc_cmd_box_w, tnc_cmd_box_h;
int prompt_box_y, prompt_box_x, prompt_box_w, prompt_box_h;

int prompt_row, prompt_col;
int status_row, status_col;
int cmd_row, cur_cmd_row, cmd_col;
int data_row, cur_data_row, data_col;
int list_row, cur_list_row, list_col;
int recents_row, cur_recents_row, recents_col;
int ptable_row, cur_ptable_row, ptable_col;
int max_cmd_rows;
int max_data_rows;
int max_list_rows;
int max_recents_rows;
int max_ptable_rows;

int num_new_msgs;
int num_new_files;

char data_buf[MAX_DATA_BUF_LEN+1][MAX_DATA_ROW_SIZE];
int data_buf_start, data_buf_end, data_buf_top, data_buf_cnt;
int data_buf_scroll_timer;

char recents_list[MAX_RECENTS_LIST_LEN+1][MAX_MBOX_HDR_SIZE];
int recents_list_cnt;
int recents_start_line;
int refresh_recents;

typedef struct hl {
    char htext[MAX_HEARD_SIZE];
    time_t htime;
} HL_ENTRY;
HL_ENTRY heard_list[MAX_HEARD_LIST_LEN+1];
int heard_list_cnt;

typedef struct pt {
    char call[16];
    char in_sn[8];
    char in_qual[8];
    char out_sn[8];
    char out_qual[8];
    time_t in_time;
    time_t out_time;
} PT_ENTRY;
PT_ENTRY ptable_list[MAX_PTABLE_LIST_LEN+1];
int ptable_list_cnt;
int ptable_start_line;
int refresh_ptable;

void ui_queue_traffic_log(const char *text)
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

void ui_queue_debug_log(const char *text)
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

void ui_queue_heard(const char *text)
{
    pthread_mutex_lock(&mutex_heard);
    cmdq_push(&g_heard_q, text);
    pthread_mutex_unlock(&mutex_heard);
}

void ui_queue_ptable(const char *text)
{
    pthread_mutex_lock(&mutex_ptable);
    cmdq_push(&g_ptable_q, text);
    pthread_mutex_unlock(&mutex_ptable);
}

void ui_queue_data_in(const char *text)
{
    pthread_mutex_lock(&mutex_data_in);
    dataq_push(&g_data_in_q, text);
    pthread_mutex_unlock(&mutex_data_in);
}

void ui_queue_data_out(const char *text)
{
    pthread_mutex_lock(&mutex_data_out);
    dataq_push(&g_data_out_q, text);
    pthread_mutex_unlock(&mutex_data_out);
}

void ui_queue_cmd_out(const char *text)
{
    pthread_mutex_lock(&mutex_cmd_out);
    cmdq_push(&g_cmd_out_q, text);
    pthread_mutex_unlock(&mutex_cmd_out);
}

void ui_set_title_dirty(int val)
{
    pthread_mutex_lock(&mutex_title);
    title_dirty = val;
    pthread_mutex_unlock(&mutex_title);
}

void ui_set_status_dirty(int val)
{
    pthread_mutex_lock(&mutex_status);
    status_dirty = val;
    pthread_mutex_unlock(&mutex_status);
}

void ui_on_cancel()
{
    if (g_tnc_attached)
        arim_on_event(EV_CANCEL, 0);
}

WINDOW *ui_set_active_win(WINDOW *win)
{
    static WINDOW *active_win = NULL;
    WINDOW *prev;

    prev = active_win;
    active_win = win;
    return prev;
}

void ui_print_data_title()
{
    int center, start = 1;

    box(tnc_data_box, 0, 0);
    center = (tnc_data_box_w / 2) - 1;
    start = center - 9;
    if (start < 1)
        start = 1;
    mvwprintw(tnc_data_box, tnc_data_box_h - 1, start, " TRAFFIC MONITOR ");
}

attr_t ui_calc_data_in_attr(char *linebuf)
{
    char *p, call[MAX_CALLSIGN_SIZE+4], mycall[TNC_MYCALL_SIZE];
    size_t i, cnt, len;
    int found_call, frame_type;
    attr_t attr = A_NORMAL;

    if ((linebuf[0] == '<') || (mon_timestamp && linebuf[11] == '<'))
        attr = A_BOLD;
    if (color_code) {
        /* extract frame type */
        if (mon_timestamp)
            frame_type = linebuf[15];
        else
            frame_type = linebuf[4];
        /* check for net traffic first */
        cnt = arim_get_netcall_cnt();
        for (i = 0; i < cnt; i++) {
            if (!arim_copy_netcall(mycall, sizeof(mycall), i))
                continue;
            snprintf(call, sizeof(call), "|%s|", mycall);
            len = strlen(call);
            found_call = 0;
            p = linebuf;
            while (*p) {
                if (*p == '|') {
                    if (!strncasecmp(p, call, len)) {
                        found_call = 1;
                        break;
                    }
                }
                if ((p - linebuf) > MAX_ARIM_HDR_SIZE)
                    break;
                ++p;
            }
            if (found_call)
                break;
        }
        if (found_call) {
            switch (frame_type) {
            }
            if (frame_type == '!' || frame_type == 'E')
                attr |= COLOR_PAIR(1);
            else
                attr |= COLOR_PAIR(6);
        } else {
            /* check for TNC call */
            arim_copy_mycall(mycall, sizeof(mycall));
            snprintf(call, sizeof(call), "|%s|", mycall);
            len = strlen(call);
            found_call = 0;
            p = linebuf;
            while (*p) {
                if (*p == '|') {
                    if (!strncasecmp(p, call, len)) {
                        found_call = 1;
                        break;
                    }
                }
                /* special check for ARDOP pings */
                if ((*p == '>') && (p - linebuf) < (MAX_CALLSIGN_SIZE*2)+4) {
                    if (!strncasecmp(p + 1, mycall, strlen(mycall))) {
                        found_call = 1;
                        break;
                    }
                    if (!strncasecmp(p - strlen(mycall), mycall, strlen(mycall))) {
                        found_call = 1;
                        break;
                    }
                }
                if ((p - linebuf) > MAX_ARIM_HDR_SIZE)
                    break;
                ++p;
            }
            if (found_call) {
                switch (frame_type) {
                case 'M':
                case 'A':
                    attr |= COLOR_PAIR(2);
                    break;
                case 'Q':
                case 'R':
                    attr |= COLOR_PAIR(3);
                    break;
                case 'N':
                case 'E':
                case '!':
                    attr |= COLOR_PAIR(1);
                    break;
                case 'B':
                    attr |= COLOR_PAIR(5);
                    break;
                case 'P':
                case 'p':
                    attr |= (COLOR_PAIR(4)|A_BOLD);
                    break;
                }
            } else {
                switch (frame_type) {
                case 'I':
                    attr |= (COLOR_PAIR(4)|A_BOLD);
                    break;
                case 'E':
                    attr |= COLOR_PAIR(1);
                    break;
                case 'B':
                    attr |= COLOR_PAIR(5);
                    break;
                }
            }
        }
    }
    return attr;
}

void ui_refresh_data_win()
{
    int i, max_cols, cur;
    char linebuf[MAX_DATA_ROW_SIZE];

    cur = data_buf_top;
    max_cols = (tnc_data_box_w - 4) + 1;
    if (max_cols > sizeof(linebuf))
        max_cols = sizeof(linebuf);
    wclear(tnc_data_win);
    for (i = 0; i < max_data_rows; i++) {
        if (cur == data_buf_end)
            break;
        snprintf(linebuf, max_cols, "%s", data_buf[cur]);
        wattrset(tnc_data_win, ui_calc_data_in_attr(linebuf));
        mvwprintw(tnc_data_win, i, data_col, " %s", linebuf);
        wattrset(tnc_data_win, A_NORMAL);
        if (++cur == MAX_DATA_BUF_LEN)
            cur = 0;
    }
    if (show_titles) {
        ui_print_data_title();
    }
    touchwin(tnc_data_box);
    wrefresh(tnc_data_box);
}

void ui_clear_data_in()
{
    pthread_mutex_lock(&mutex_data_in);
    memset(data_buf, 0, sizeof(data_buf));
    data_buf_start = data_buf_end = data_buf_top = data_buf_cnt = 0;
    data_buf_scroll_timer = 0;
    pthread_mutex_unlock(&mutex_data_in);
    wclear(tnc_data_win);
    touchwin(tnc_data_box);
    wrefresh(tnc_data_box);
}

void ui_print_data_in()
{
    char *p;

    pthread_mutex_lock(&mutex_data_in);
    p = dataq_pop(&g_data_in_q);
    while (p) {
        if (data_buf_cnt < MAX_DATA_BUF_LEN)
            ++data_buf_cnt;
        snprintf(data_buf[data_buf_end], sizeof(data_buf[0]), "%s", p);
        p = data_buf[data_buf_end];
        while (*p) {
            if (*p < ' ')
                *p = ' ';
            ++p;
        }
        ++data_buf_end;
        if (data_buf_end == MAX_DATA_BUF_LEN)
            data_buf_end = 0;
        if (data_buf_cnt == MAX_DATA_BUF_LEN) {
            ++data_buf_start;
            if (data_buf_start == MAX_DATA_BUF_LEN)
                data_buf_start = 0;
        }
        p = dataq_pop(&g_data_in_q);
    }
    pthread_mutex_unlock(&mutex_data_in);
    if (data_buf_cnt > max_data_rows) {
        data_buf_top = data_buf_end - max_data_rows;
        if (data_buf_top < 0)
            data_buf_top += MAX_DATA_BUF_LEN;
    } else {
        data_buf_top = data_buf_end - data_buf_cnt;
    }
    ui_refresh_data_win();
}

void ui_print_cmd_title()
{
    int center, start = 1;

    box(tnc_cmd_box, 0, 0);
    center = (tnc_cmd_box_w / 2) - 1;
    start = center - 7;
    if (start < 1)
        start = 1;
    mvwprintw(tnc_cmd_box, tnc_cmd_box_h - 1, start, " TNC COMMANDS ");
}

void ui_print_cmd_in()
{
    char *p;

    if (!show_cmds)
        return;
    pthread_mutex_lock(&mutex_cmd_in);
    p = cmdq_pop(&g_cmd_in_q);
    while (p) {
        if (cur_cmd_row == max_cmd_rows) {
            wscrl(tnc_cmd_win, 1);
            cur_cmd_row--;
        }
        if (strlen(p) > (tnc_cmd_box_w - 4))
            p[tnc_cmd_box_w - 4] = '\0';
        if (color_code) {
            if (!strncasecmp(p, "<<", 2))
                wattrset(tnc_cmd_win, COLOR_PAIR(6)|A_BOLD);
            else if (!strncasecmp(p, ">> PTT TRUE", 13))
                wattrset(tnc_cmd_win, COLOR_PAIR(1)|A_NORMAL);
            else if (!strncasecmp(p, ">> PTT FALSE", 14))
                wattrset(tnc_cmd_win, COLOR_PAIR(2)|A_NORMAL);
            else if (!strncasecmp(p, ">> BUFFER", 11))
                wattrset(tnc_cmd_win, COLOR_PAIR(3)|A_NORMAL);
            else if (!strncasecmp(p, ">> PING", 9))
                wattrset(tnc_cmd_win, COLOR_PAIR(4)|A_NORMAL);
            else if (!strncasecmp(p, ">> BUSY", 9))
                wattrset(tnc_cmd_win, COLOR_PAIR(5)|A_NORMAL);
        } else if (!strncasecmp(p, "<<", 2)) {
            wattrset(tnc_cmd_win, A_BOLD);
        } else {
            wattrset(tnc_cmd_win, A_NORMAL);
        }
        mvwprintw(tnc_cmd_win, cur_cmd_row, cmd_col, " %s", p);
        wattrset(tnc_cmd_win, A_NORMAL);
        if (cur_cmd_row < max_cmd_rows)
            cur_cmd_row++;
        p = cmdq_pop(&g_cmd_in_q);
    }
    pthread_mutex_unlock(&mutex_cmd_in);
    if (!show_recents && !show_ptable) {
        touchwin(tnc_cmd_box);
        wrefresh(tnc_cmd_box);
    }
}

void ui_clear_calls_heard()
{
    memset(heard_list, 0, sizeof(heard_list));
    heard_list_cnt = 0;
    wclear(ui_list_win);
    touchwin(ui_list_box);
    wrefresh(ui_list_box);
}

void ui_get_heard_list(char *listbuf, size_t listbufsize)
{
    size_t i, len, cnt = 0;

    snprintf(listbuf, listbufsize, "Calls heard (%s):\n",
                last_time_heard == LT_HEARD_ELAPSED ? "ET" : "LT");
    cnt += strlen(listbuf);
    for (i = 0; i < heard_list_cnt; i++) {
        len = strlen(heard_list[i].htext);
        if ((cnt + len + 1) < listbufsize) {
            strncat(listbuf, &(heard_list[i].htext[1]), listbufsize - cnt - 1);
            cnt += len;
            strncat(listbuf, "\n", listbufsize - cnt - 1);
            ++cnt;
        } else {
            break;
        }
    }
    if ((cnt + 1) < listbufsize) {
        strncat(listbuf, "\n", listbufsize - cnt - 1);
        ++cnt;
    }
    listbuf[cnt] = '\0';
}

void ui_print_heard_list_title()
{
    box(ui_list_box, 0, 0);
    if (last_time_heard == LT_HEARD_ELAPSED)
        mvwprintw(ui_list_box, 0, 4, " CALLS HEARD (ET) ");
    else
        mvwprintw(ui_list_box, 0, 4, " CALLS HEARD (LT) ");
}

void ui_update_heard_list()
{
    static time_t tprev;
    time_t tcur, telapsed;
    struct tm *heard_time;
    int i, days, hours, minutes, reformat = 0;
    char heard[MAX_HEARD_SIZE];

    if (last_time_heard != prev_last_time_heard)
        reformat = 1;
    if (last_time_heard == LT_HEARD_ELAPSED) {
        tcur = time(NULL);
        if (reformat || (tcur - tprev) > 15) {
            tprev = tcur;
            for (i = 0; i < heard_list_cnt; i++) {
                telapsed = tcur - heard_list[i].htime;
                days = telapsed / (24*60*60);
                if (days > 99)
                    continue;
                telapsed = telapsed % (24*60*60);
                hours = telapsed / (60*60);
                telapsed = telapsed % (60*60);
                minutes = telapsed / 60;
                snprintf(heard, 16, "%s", heard_list[i].htext);
                snprintf(heard_list[i].htext, sizeof(heard_list[0].htext),
                            "%s %02d:%02d:%02d", heard, days, hours, minutes);
            }
            if (reformat && show_titles)
                ui_print_heard_list_title();
        }
    } else if (reformat) {
        for (i = 0; i < heard_list_cnt; i++) {
            pthread_mutex_lock(&mutex_time);
            if (!strncasecmp(g_ui_settings.utc_time, "TRUE", 4))
                heard_time = gmtime(&heard_list[i].htime);
            else
                heard_time = localtime(&heard_list[i].htime);
            pthread_mutex_unlock(&mutex_time);
            snprintf(heard, 16, "%s", heard_list[i].htext);
            snprintf(heard_list[i].htext, sizeof(heard_list[0].htext),
                        "%s %02d:%02d:%02d", heard, heard_time->tm_hour,
                            heard_time->tm_min, heard_time->tm_sec);
        }
        if (show_titles)
            ui_print_heard_list_title();
    }
    prev_last_time_heard = last_time_heard;
}

void ui_refresh_heard_list()
{
    int i;

    ui_update_heard_list();
    cur_list_row = 0;
    wclear(ui_list_win);
    for (i = 0; i < heard_list_cnt && i < max_list_rows; i++) {
        if (color_code) {
            switch (heard_list[i].htext[0]) {
            case '1':
                wattrset(ui_list_win, COLOR_PAIR(1)|A_NORMAL);
                break;
            case '2':
                wattrset(ui_list_win, COLOR_PAIR(2)|A_NORMAL);
                break;
            case '3':
                wattrset(ui_list_win, COLOR_PAIR(3)|A_NORMAL);
                break;
            case '4':
                wattrset(ui_list_win, COLOR_PAIR(4)|A_NORMAL|A_BOLD);
                break;
            case '5':
                wattrset(ui_list_win, COLOR_PAIR(5)|A_NORMAL);
                break;
            case '6':
                wattrset(ui_list_win, COLOR_PAIR(6)|A_NORMAL);
                break;
            }
        }
        mvwprintw(ui_list_win, cur_list_row, list_col, "%s", &(heard_list[i].htext[1]));
        wattrset(ui_list_win, A_NORMAL);
        cur_list_row++;
    }
    touchwin(ui_list_box);
    wrefresh(ui_list_box);
}

void ui_print_heard_list()
{
    static int once = 0;
    char *p;
    int i;

    if (!once) {
        once = 1;
        memset(&heard_list, 0, sizeof(heard_list));
    }

    pthread_mutex_lock(&mutex_heard);
    p = cmdq_pop(&g_heard_q);
    pthread_mutex_unlock(&mutex_heard);

    if (p) {
        memmove(&heard_list[1], &heard_list[0], MAX_HEARD_LIST_LEN * sizeof(HL_ENTRY));
        heard_list[0].htime = time(NULL);
        snprintf(heard_list[0].htext, sizeof(heard_list[0].htext), "%s", p);
        ++heard_list_cnt;
        for (i = 1; i < heard_list_cnt; i++) {
            if (!strncasecmp(&(heard_list[0].htext[3]), &(heard_list[i].htext[3]), 10)) {
                memmove(&heard_list[i], &heard_list[i + 1], (MAX_HEARD_LIST_LEN - i) * sizeof(HL_ENTRY));
                --heard_list_cnt;
                break;
            }
        }
        if (heard_list_cnt > MAX_HEARD_LIST_LEN)
            --heard_list_cnt;
        /* force reformatting of heard list */
        prev_last_time_heard = -1;
        ui_refresh_heard_list();
    } else if (last_time_heard == LT_HEARD_ELAPSED) {
        /* periodic check, results in update every 15 seconds */
        ui_refresh_heard_list();
    } else {
        touchwin(ui_list_box);
        wrefresh(ui_list_box);
    }
}

void ui_print_status_ind()
{
    int start, state;
    char idle_busy, tx_rx, ind[MAX_STATUS_IND_SIZE], fecmode[TNC_FECMODE_SIZE];
    char tnc_state[TNC_STATE_SIZE], remote_call[TNC_MYCALL_SIZE], bw_hz[TNC_ARQ_BW_SIZE];

    if (g_tnc_attached) {
        state = arim_get_state();
        switch (state) {
        case ST_ARQ_IN_CONNECT_WAIT:
        case ST_ARQ_OUT_CONNECT_WAIT:
        case ST_ARQ_CONNECTED:
        case ST_ARQ_MSG_RCV:
        case ST_ARQ_MSG_SEND_WAIT:
        case ST_ARQ_MSG_SEND:
        case ST_ARQ_FILE_RCV_WAIT_OK:
        case ST_ARQ_FILE_RCV_WAIT:
        case ST_ARQ_FILE_RCV:
        case ST_ARQ_FILE_SEND_WAIT:
        case ST_ARQ_FILE_SEND_WAIT_OK:
        case ST_ARQ_FILE_SEND:
        case ST_ARQ_AUTH_RCV_A2_WAIT:
        case ST_ARQ_AUTH_RCV_A3_WAIT:
        case ST_ARQ_AUTH_RCV_A4_WAIT:
        case ST_ARQ_AUTH_SEND_A1:
        case ST_ARQ_AUTH_SEND_A2:
        case ST_ARQ_AUTH_SEND_A3:
            arim_copy_remote_call(remote_call, sizeof(remote_call));
            arim_copy_tnc_state(tnc_state, sizeof(tnc_state));
            arim_copy_arq_bw_hz(bw_hz, sizeof(bw_hz));
            if (!strncasecmp(tnc_state, "IRStoISS", 8))
                tnc_state[3] = '\0';
             snprintf(ind, sizeof(ind),  " %c ARQ:%s%s %s S:%-4.4s",
                 (state == ST_ARQ_CONNECTED ? ' ' : '!'), remote_call,
                     (arim_arq_auth_get_status() ? "+" : ""), bw_hz, tnc_state);
            break;
        default:
            idle_busy = (state == ST_IDLE) ? 'I' : 'B';
            switch (state) {
            case ST_SEND_MSG_BUF_WAIT:
            case ST_SEND_NET_MSG_BUF_WAIT:
            case ST_SEND_QRY_BUF_WAIT:
            case ST_SEND_RESP_BUF_WAIT:
            case ST_SEND_ACKNAK_BUF_WAIT:
            case ST_SEND_BCN_BUF_WAIT:
            case ST_SEND_UN_BUF_WAIT:
            case ST_SEND_ACKNAK_PEND:
            case ST_SEND_RESP_PEND:
            case ST_SEND_PING_ACK_PEND:
            case ST_RCV_PING_ACK_WAIT:
                tx_rx = 'T';
                break;
            default:
                tx_rx = 'R';
                break;
            }
            arim_copy_fecmode(fecmode, sizeof(fecmode));
            if (!g_btime)
                snprintf(ind, sizeof(ind), " %c:%c %s:%d B:OFF",
                    idle_busy, tx_rx, fecmode, arim_get_fec_repeats());
            else
                snprintf(ind, sizeof(ind), " %c:%c %s:%d B:%03d",
                    idle_busy, tx_rx, fecmode, arim_get_fec_repeats(), g_btime);
            break;
        }
        start = COLS - strlen(ind) - 1;
        if (start < status_col)
            start = status_col;
        wattron(main_win, A_BOLD);
        wmove(main_win, status_row, start);
        wclrtoeol(main_win);
        mvwprintw(main_win, status_row, start, "%s", ind);
        wattroff(main_win, A_BOLD);
    }
}

void ui_print_status(const char *text, int temporary)
{
    static char status[MAX_STATUS_BAR_SIZE];

    if (text)
        snprintf(status, COLS - 2, "%s", text);
    wmove(main_win, status_row, 0);
    wclrtoeol(main_win);
    mvwprintw(main_win, status_row, status_col, "%s", status);
    ui_print_status_ind();
    wrefresh(main_win);
    if (temporary)
        status_timer = STATUS_TIMER_COUNT;
}

void ui_print_clock(int now)
{
    static time_t tprev = 0;
    time_t tcur;
    char clock[32];

    tcur = time(NULL);
    if (now || tcur - tprev > 5) {
        tprev = tcur;
        util_clock(clock, sizeof(clock));
        mvwprintw(main_win, TITLE_ROW, 1, "%s ", clock);
        wrefresh(main_win);
    }
}

void ui_print_new_ctrs()
{
    char alerts[32];

    snprintf(alerts, sizeof(alerts), "New:%dM,%dF", num_new_msgs, num_new_files);
    mvwprintw(main_win, TITLE_ROW, COLS - strlen(alerts) - 2, " %s ", alerts);
    wrefresh(main_win);
}

void ui_clear_new_ctrs()
{
    int cmd;

    if (num_new_msgs || num_new_files) {
        cmd = ui_show_dialog("\tAre you sure you want to clear\n\tthe new message and file counters?\n \n\t[Y]es   [N]o", "yYnN");
        if (cmd == 'y' || cmd == 'Y') {
            num_new_msgs = num_new_files = 0;
            ui_print_new_ctrs();
            ui_print_status("Clearing new message and file counters", 1);
        }
    }
}

void ui_check_title_dirty()
{
    char status[MAX_TITLE_STATUS_SIZE];

    if (!title_dirty) {
        ui_print_clock(0);
        return;
    }
    switch (title_dirty) {
    case STATUS_REFRESH:
        ui_print_title(NULL);
        break;
    case TITLE_TNC_DETACHED:
        snprintf(status, sizeof(status), "[Detached from TNC]");
        ui_print_title(status);
        break;
    case TITLE_TNC_ATTACHED:
        pthread_mutex_lock(&mutex_tnc_set);
        snprintf(status, sizeof(status), "[Attached TNC %d: %.20s][L:%c E:%c]",
                     g_cur_tnc + 1, g_tnc_settings[g_cur_tnc].name,
                                    g_tnc_settings[g_cur_tnc].listen[0],
                                    g_tnc_settings[g_cur_tnc].en_pingack[0]);
        pthread_mutex_unlock(&mutex_tnc_set);
        ui_print_title(status);
        break;
    }
    ui_set_title_dirty(0);
}

void ui_check_status_dirty()
{
    char cmd;

    ui_check_title_dirty();
    if (!status_dirty)
        return;

    switch (status_dirty) {
    case STATUS_REFRESH:
        ui_print_status(NULL, 0);
        break;
    case STATUS_WAIT_ACK:
        ui_print_status("ARIM Busy: message sent, waiting for ACK", 1);
        break;
    case STATUS_WAIT_RESP:
        ui_print_status("ARIM Busy: query sent, waiting for response", 1);
        break;
    case STATUS_NET_MSG_SENT:
        ui_print_status("ARIM Idle: done sending net message", 1);
        break;
    case STATUS_BEACON_SENT:
        ui_print_status("ARIM Idle: done sending beacon", 1);
        break;
    case STATUS_MSG_ACK_RCVD:
        ui_print_status("ARIM Idle: ACK received, saving message to Sent Messages...", 1);
        arim_store_msg_prev_sent();
        break;
    case STATUS_MSG_NAK_RCVD:
        ui_print_status("ARIM Idle: NAK received", 1);
        ui_set_status_dirty(0); /* must clear flag before opening modal dialog */
        cmd = ui_show_dialog("\tMessage send failed!\n\tDo you want to save\n\tthe message to your Outbox?\n \n\t[Y]es   [N]o", "yYnN");
        if (cmd == 'y' || cmd == 'Y') {
            ui_print_status("Saving message to Outbox...", 1);
            arim_store_msg_prev_out();
        }
        break;
    case STATUS_RESP_RCVD:
        ui_print_status("ARIM Idle: received response to query", 1);
        ++num_new_msgs;
        ui_print_new_ctrs();
        break;
    case STATUS_ACK_TIMEOUT:
        ui_print_status("ARIM Idle: message ACK wait time out", 1);
        ui_set_status_dirty(0); /* must clear flag before opening modal dialog */
        cmd = ui_show_dialog("\tMessage send failed!\n\tDo you want to save\n\tthe message to your Outbox?\n \n\t[Y]es   [N]o", "yYnN");
        if (cmd == 'y' || cmd == 'Y') {
            ui_print_status("Saving message to Outbox...", 1);
            arim_store_msg_prev_out();
        }
        break;
    case STATUS_RESP_TIMEOUT:
        ui_print_status("ARIM Idle: response wait time out", 1);
        break;
    case STATUS_FRAME_START:
        ui_print_status("ARIM Busy: ARIM frame starting", 1);
        break;
    case STATUS_FRAME_END:
        ui_print_status("ARIM Idle: ARIM frame received", 1);
        break;
    case STATUS_MSG_START:
        ui_print_status("ARIM Busy: message frame starting", 1);
        break;
    case STATUS_QRY_START:
        ui_print_status("ARIM Busy: query frame starting", 1);
        break;
    case STATUS_BCN_START:
        ui_print_status("ARIM Busy: beacon frame starting", 1);
        break;
    case STATUS_RESP_START:
        ui_print_status("ARIM Busy: response frame starting", 1);
        break;
    case STATUS_MSG_END:
        ui_print_status("ARIM Idle: message frame received", 1);
        break;
    case STATUS_QRY_END:
        ui_print_status("ARIM Idle: query frame received", 1);
        break;
    case STATUS_BCN_END:
        ui_print_status("ARIM Idle: beacon frame received", 1);
        break;
    case STATUS_RESP_END:
        ui_print_status("ARIM Idle: response frame received", 1);
        break;
    case STATUS_ARIM_FRAME_TO:
        ui_print_status("ARIM Idle: ARIM frame receive time out", 1);
        break;
    case STATUS_RESP_SEND:
        ui_print_status("ARIM Busy: sending response to query", 1);
        break;
    case STATUS_ACKNAK_SEND:
        ui_print_status("ARIM Busy: sending ACK/NAK", 1);
        break;
    case STATUS_MSG_RCVD:
        ui_print_status("ARIM Busy: received message for this station", 1);
        ++num_new_msgs;
        ui_print_new_ctrs();
        break;
    case STATUS_NET_MSG_RCVD:
        ui_print_status("ARIM Busy: received message for the net", 1);
        ++num_new_msgs;
        ui_print_new_ctrs();
        break;
    case STATUS_QUERY_RCVD:
        ui_print_status("ARIM Busy: received query for this station", 1);
        break;
    case STATUS_MSG_REPEAT:
        ui_print_status("ARIM Busy: ACK time out, repeating message send", 1);
        break;
    case STATUS_RESP_SENT:
        ui_print_status("ARIM Idle: done sending response", 1);
        break;
    case STATUS_ACKNAK_SENT:
        ui_print_status("ARIM Idle: done sending ACK/NAK", 1);
        break;
    case STATUS_QRY_SEND_CAN:
        ui_print_status("ARIM Idle: query send canceled!", 1);
        break;
    case STATUS_RESP_SEND_CAN:
        ui_print_status("ARIM Idle: response send canceled!", 1);
        break;
    case STATUS_ACKNAK_SEND_CAN:
        ui_print_status("ARIM Idle: ACK/NAK send canceled!", 1);
        break;
    case STATUS_BCN_SEND_CAN:
        ui_print_status("ARIM Idle: beacon send canceled!", 1);
        break;
    case STATUS_SEND_UNPROTO_CAN:
        ui_print_status("ARIM Idle: unproto send canceled!", 1);
        break;
    case STATUS_MSG_SEND_CAN:
        ui_print_status("ARIM Idle: message send canceled!", 1);
        ui_set_status_dirty(0); /* must clear flag before opening modal dialog */
        cmd = ui_show_dialog("\tMessage send canceled!\n\tDo you want to save\n\tthe message to your Outbox?\n \n\t[Y]es   [N]o", "yYnN");
        if (cmd == 'y' || cmd == 'Y') {
            ui_print_status("Saving message to Outbox...", 1);
            arim_store_msg_prev_out();
        }
        break;
    case STATUS_RESP_WAIT_CAN:
        ui_print_status("ARIM Idle: wait for response canceled!", 1);
        break;
    case STATUS_FRAME_WAIT_CAN:
        ui_print_status("ARIM Idle: wait for ARIM frame canceled!", 1);
        break;
    case STATUS_PING_SENT:
        ui_print_status("ARIM Busy: ping sent, waiting for ACK", 1);
        break;
    case STATUS_PING_ACK_RCVD:
        ui_print_status("ARIM Idle: ping ACK received", 1);
        break;
    case STATUS_PING_RCVD:
        ui_print_status("ARIM Busy: received ping", 1);
        break;
    case STATUS_PING_SEND_CAN:
        ui_print_status("ARIM Idle: ping repeats canceled!", 1);
        break;
    case STATUS_PING_MSG_SEND:
        ui_print_status("ARIM Busy: ping ACK quality >= threshold, sending message", 1);
        sleep(2); /* don't rush the ardopc TNC */
        arim_send_msg_pp();
        break;
    case STATUS_PING_ACK_TIMEOUT:
        ui_print_status("ARIM Idle: ping ACK wait time out", 1);
        break;
    case STATUS_PING_MSG_ACK_TO:
        ui_print_status("ARIM Idle: ping ACK timeout, message send canceled", 1);
        ui_set_status_dirty(0); /* must clear flag before opening modal dialog */
        cmd = ui_show_dialog("\tMessage send canceled!\n\tDo you want to save\n\tthe message to your Outbox?\n \n\t[Y]es   [N]o", "yYnN");
        if (cmd == 'y' || cmd == 'Y') {
            ui_print_status("Saving message to Outbox...", 1);
            arim_store_msg_prev_out();
        }
        break;
    case STATUS_PING_MSG_ACK_BAD:
        ui_print_status("ARIM Idle: ping ACK quality < threshold, message send canceled", 1);
        ui_set_status_dirty(0); /* must clear flag before opening modal dialog */
        cmd = ui_show_dialog("\tMessage send canceled!\n\tDo you want to save\n\tthe message to your Outbox?\n \n\t[Y]es   [N]o", "yYnN");
        if (cmd == 'y' || cmd == 'Y') {
            ui_print_status("Saving message to Outbox...", 1);
            arim_store_msg_prev_out();
        }
        break;
    case STATUS_PING_QRY_SEND:
        ui_print_status("ARIM Busy: ping ACK quality >= threshold, sending query", 1);
        sleep(2); /* don't rush the ardopc TNC */
        arim_send_query_pp();
        break;
    case STATUS_PING_QRY_ACK_TO:
        ui_print_status("ARIM Idle: ping ACK timeout, query send canceled", 1);
        break;
    case STATUS_PING_QRY_ACK_BAD:
        ui_print_status("ARIM Idle: ping ACK quality < threshold, query send canceled", 1);
        break;
    case STATUS_ARQ_CONN_REQ:
        ui_print_status("ARIM Busy: received ARQ connection request", 1);
        break;
    case STATUS_ARQ_CONN_CAN:
        ui_print_status("ARIM Idle: ARQ connection canceled!", 1);
        break;
    case STATUS_ARQ_CONNECTED:
        ui_print_status("ARIM Busy: ARQ connection started", 1);
        break;
    case STATUS_ARQ_DISCONNECTED:
        ui_print_status("ARIM Idle: ARQ connection ended", 1);
        break;
    case STATUS_ARQ_CONN_REQ_SENT:
        ui_print_status("ARIM Busy: Sending ARQ connection request", 1);
        break;
    case STATUS_ARQ_CONN_REQ_TO:
        ui_print_status("ARIM Idle: ARQ connection request timeout", 1);
        break;
    case STATUS_ARQ_CONN_PP_SEND:
        ui_print_status("ARIM Busy: ping ACK quality >= threshold, connecting...", 1);
        sleep(2); /* don't rush the ardopc TNC */
        arim_arq_send_conn_req_pp();
        break;
    case STATUS_ARQ_CONN_PP_ACK_TO:
        ui_print_status("ARIM Idle: ping ACK timeout, connection request canceled", 1);
        break;
    case STATUS_ARQ_CONN_PP_ACK_BAD:
        ui_print_status("ARIM Idle: ping ACK quality < threshold, connection request canceled", 1);
        break;
    case STATUS_ARQ_CONN_TIMEOUT:
        ui_print_status("ARIM Idle: ARQ connection timeout", 1);
        break;
    case STATUS_ARQ_FILE_RCV_WAIT:
        ui_print_status("ARIM Busy: ARQ file download requested", 1);
        break;
    case STATUS_ARQ_FILE_RCV:
        ui_print_status("ARIM Busy: ARQ file download in progress", 1);
        break;
    case STATUS_ARQ_FILE_RCV_DONE:
        ui_print_status("ARIM Idle: ARQ file download complete", 1);
        ++num_new_files;
        ui_print_new_ctrs();
        break;
    case STATUS_ARQ_FILE_RCV_ERROR:
        ui_print_status("ARIM Idle: ARQ file download failed", 1);
        break;
    case STATUS_ARQ_FILE_SEND:
        ui_print_status("ARIM Busy: ARQ file upload in progress", 1);
        break;
    case STATUS_ARQ_FILE_SEND_DONE:
        ui_print_status("ARIM Busy: ARQ file upload complete", 1);
        break;
    case STATUS_ARQ_FILE_SEND_ACK:
        ui_print_status("ARIM Busy: ARQ file upload acknowleged", 1);
        break;
    case STATUS_ARQ_MSG_RCV:
        ui_print_status("ARIM Busy: ARQ message download in progress", 1);
        break;
    case STATUS_ARQ_MSG_RCV_DONE:
        ui_print_status("ARIM Idle: ARQ message download complete", 1);
        ++num_new_msgs;
        ui_print_new_ctrs();
        break;
    case STATUS_ARQ_MSG_RCV_ERROR:
        ui_print_status("ARIM Idle: ARQ message download failed", 1);
        break;
    case STATUS_ARQ_MSG_SEND:
        ui_print_status("ARIM Busy: ARQ message upload in progress", 1);
        break;
    case STATUS_ARQ_MSG_SEND_ACK:
        ui_print_status("ARIM Busy: ARQ message upload acknowleged", 1);
        break;
    case STATUS_ARQ_MSG_SEND_DONE:
        ui_print_status("ARIM Idle: ARQ message upload complete", 1);
        break;
    case STATUS_ARQ_AUTH_BUSY:
        ui_print_status("ARIM Busy: ARQ session authentication in progress", 1);
        break;
    case STATUS_ARQ_AUTH_ERROR:
        ui_print_status("ARIM Idle: ARQ session authentication failed", 1);
        break;
    case STATUS_ARQ_EAUTH_REMOTE:
        ui_print_status("ARIM Idle: ARQ session authentication failed", 1);
        ui_set_status_dirty(0); /* must clear flag before opening modal dialog */
        cmd = ui_show_dialog("\tThis station cannot authenticate\n\tthe remote station!\n"
                             "\tDo you want to disconnect now?\n \n\t[Y]es   [N]o", "yYnN");
        if (cmd == 'y' || cmd == 'Y') {
            arim_arq_send_disconn_req();
            ui_print_status("Disconnecting...", 1);
        }
        break;
    case STATUS_ARQ_EAUTH_LOCAL:
        ui_print_status("ARIM Idle: ARQ session authentication failed", 1);
        ui_set_status_dirty(0); /* must clear flag before opening modal dialog */
        cmd = ui_show_dialog("\tRemote station cannot authenticate\n\tthis station!\n"
                             "\tDo you want to disconnect now?\n \n\t[Y]es   [N]o", "yYnN");
        if (cmd == 'y' || cmd == 'Y') {
            arim_arq_send_disconn_req();
            ui_print_status("Disconnecting...", 1);
        }
        break;
    case STATUS_ARQ_AUTH_OK:
        ui_print_status("ARIM Idle: ARQ session authenticated", 1);
        break;
    case STATUS_ARQ_RUN_CACHED_CMD:
        ui_print_status("ARIM Busy: ARQ session authenticated", 1);
        arim_arq_run_cached_cmd();
        break;
    }
    ui_set_status_dirty(0);
}

void ui_print_title(const char *new_status)
{
    static char status[MAX_TITLE_STATUS_SIZE];
    static int once = 0;
    char title[MAX_TITLE_SIZE];
    int center, startx;

    if (!once) {
        snprintf(status, sizeof(status), "[Detached from TNC]");
        once = 1;
    }
    if (new_status) {
        snprintf(status, sizeof(status), "%s", new_status);
    }
    snprintf(title, sizeof(title), "ARIM v%s %s", ARIM_VERSION, status);
    center = COLS / 2;
    startx = center - (strlen(title) / 2);
    if (startx < 0) {
        startx = 0;
        title[COLS] = '\0';
    }
    wmove(main_win, TITLE_ROW, 0);
    wclrtoeol(main_win);
    mvwprintw(main_win, TITLE_ROW, startx, "%s", title);
    wrefresh(main_win);
    ui_print_clock(1);
    ui_print_new_ctrs();
}

int ui_cmd_prompt()
{
    char cmd_line[MAX_CMD_SIZE+1];
    static char cmd_hist[MAX_CMD_HIST][MAX_CMD_SIZE+1];
    static int prev_cmd = 0, next_cmd = 0, cnt_hist = 0;
    size_t len = 0, cur = 0;
    int ch, temp, hist_cmd, max_len, quit = 0;

    max_len = tnc_cmd_box_w - 4;
    if (max_len > MAX_CMD_SIZE-1)
        max_len = MAX_CMD_SIZE-1;

    wmove(prompt_win, prompt_row, prompt_col);
    wclrtoeol(prompt_win);
    wrefresh(prompt_win);

    curs_set(1);
    keypad(prompt_win, TRUE);
    memset(cmd_line, 0, sizeof(cmd_line));
    hist_cmd = prev_cmd;
    while (!quit) {
        if ((status_timer && --status_timer == 0) ||
            (data_buf_scroll_timer && --data_buf_scroll_timer == 0)) {
            if (arim_is_arq_state())
                ui_print_status(ARQ_PROMPT_STR, 0);
            else
                ui_print_status(MENU_PROMPT_STR, 0);
        }
        ch = wgetch(prompt_win);
        switch (ch) {
        case ERR:
            curs_set(0);
            ui_print_cmd_in();
            ui_print_recents();
            ui_print_ptable();
            if (!data_buf_scroll_timer)
                ui_print_data_in();
            ui_print_heard_list();
            ui_check_status_dirty();
            wmove(prompt_win, prompt_row, prompt_col + cur);
            curs_set(1);
            break;
        case '\n':
            if (strlen(cmd_line) && strcmp(cmd_line, "!!") &&
                                    strcmp(cmd_hist[prev_cmd], cmd_line)) {
                snprintf(cmd_hist[next_cmd], sizeof(cmd_hist[next_cmd]), "%s", cmd_line);
                if (cnt_hist < MAX_CMD_HIST)
                    ++cnt_hist;
                prev_cmd = hist_cmd = next_cmd;
                ++next_cmd;
                if (next_cmd == MAX_CMD_HIST)
                    next_cmd = 0;
            }
            quit = 1;
            break;
        case 27:
            ui_on_cancel();
            break;
        case 127: /* DEL */
        case KEY_BACKSPACE:
            if (len && cur) {
                memmove(cmd_line + cur - 1, cmd_line + cur, MAX_CMD_SIZE - cur);
                --len;
                --cur;
                mvwdelch(prompt_win, prompt_row, prompt_col + cur);
            }
            break;
        case 4: /* CTRL-D */
        case KEY_DC:
            if (len && cur < len) {
                memmove(cmd_line + cur, cmd_line + cur + 1, MAX_CMD_SIZE - cur);
                mvwdelch(prompt_win, prompt_row, prompt_col + cur);
                --len;
            }
            break;
        case 11: /* CTRL-K */
            if (len && cur < len) {
                len -= (len - cur);
                cmd_line[cur] = '\0';
                wmove(prompt_win, prompt_row, prompt_col);
                wclrtoeol(prompt_win);
                waddstr(prompt_win, cmd_line);
            }
            break;
        case 21: /* CTRL-U */
            if (len && cur && cur <= len) {
                len -= cur;
                memmove(cmd_line, cmd_line + cur, MAX_CMD_SIZE - cur);
                cur = 0;
                wmove(prompt_win, prompt_row, prompt_col);
                wclrtoeol(prompt_win);
                waddstr(prompt_win, cmd_line);
            }
            break;
        case 1: /* CTRL-A */
        case KEY_HOME:
            if (cur) {
                cur = 0;
                wmove(prompt_win, prompt_row, prompt_col + cur);
            }
            break;
        case 5: /* CTRL-E */
        case KEY_END:
            if (cur < len) {
                cur = len;
                wmove(prompt_win, prompt_row, prompt_col + cur);
            }
            break;
        case 2: /* CTRL-B */
        case KEY_LEFT:
            if (cur) {
                --cur;
                wmove(prompt_win, prompt_row, prompt_col + cur);
            }
            break;
        case 6: /* CTRL-F */
        case KEY_RIGHT:
            if (cur < len) {
                ++cur;
                wmove(prompt_win, prompt_row, prompt_col + cur);
            }
            break;
        case 14: /* CTRL-N */
        case KEY_DOWN:
            if (hist_cmd != next_cmd) {
                temp = hist_cmd;
                ++hist_cmd;
                if (hist_cmd >= MAX_CMD_HIST)
                    hist_cmd = 0;
                if (hist_cmd != next_cmd) {
                    snprintf(cmd_line, sizeof(cmd_line), "%s", cmd_hist[hist_cmd]);
                } else {
                    cmd_line[0] = '\0';
                }
                if (hist_cmd == next_cmd)
                    hist_cmd = temp;
                cur = len = strlen(cmd_line);
                wmove(prompt_win, prompt_row, prompt_col);
                wclrtoeol(prompt_win);
                waddstr(prompt_win, cmd_line);
                wrefresh(prompt_win);
            }
            break;
        case 16: /* CTRL-P */
        case KEY_UP:
            if (hist_cmd != next_cmd) {
                temp = hist_cmd;
                snprintf(cmd_line, sizeof(cmd_line), "%s", cmd_hist[hist_cmd]);
                --hist_cmd;
                if (hist_cmd < 0) {
                    if (cnt_hist == MAX_CMD_HIST)
                        hist_cmd = MAX_CMD_HIST-1;
                    else
                        hist_cmd = 0;
                }
                if (hist_cmd == next_cmd)
                    hist_cmd = temp;
                cur = len = strlen(cmd_line);
                wmove(prompt_win, prompt_row, prompt_col);
                wclrtoeol(prompt_win);
                waddstr(prompt_win, cmd_line);
                wrefresh(prompt_win);
            }
            break;
        case 24: /* CTRL-X */
            if (arim_is_arq_state()) {
                temp = ui_show_dialog("\tAre you sure\n\tyou want to disconnect?\n \n\t[Y]es   [N]o", "yYnN");
                if (temp == 'y' || temp == 'Y')
                    arim_arq_send_disconn_req();
                quit = 1;
            }
            break;
        default:
            if (isprint(ch) && len < max_len) {
                if (cur == len) {
                    cmd_line[len++] = ch;
                    cmd_line[len] = '\0';
                    waddch(prompt_win, ch);
                } else {
                    memmove(cmd_line + cur + 1, cmd_line + cur, MAX_CMD_SIZE - cur);
                    cmd_line[cur] = ch;
                    ++len;
                    mvwinsch(prompt_win, prompt_row, prompt_col + cur, ch);
                }
                ++cur;
            }
        }
        if (g_win_changed)
            quit = 1;
    }
    keypad(prompt_win, FALSE);
    curs_set(0);
    wmove(prompt_win, prompt_row, prompt_col);
    wclrtoeol(prompt_win);
    wrefresh(prompt_win);
    cmdproc_cmd(cmd_line); /* process the command */
    return 1;
}

void ui_clear_recents()
{
    pthread_mutex_lock(&mutex_recents);
    memset(&recents_list, 0, sizeof(recents_list));
    recents_list_cnt = 0;
    pthread_mutex_unlock(&mutex_recents);
    if (show_recents && ui_recents_win) {
        delwin(ui_recents_win);
        ui_recents_win = NULL;
        ui_recents_win = newwin(tnc_cmd_box_h - 2, tnc_cmd_box_w - 2,
                                     tnc_cmd_box_y + 1, tnc_cmd_box_x + 1);
        if (!ui_recents_win) {
            ui_print_status("Recents: failed to create window", 1);
            return;
        }
        touchwin(ui_recents_win);
        wrefresh(ui_recents_win);
    }
    refresh_recents = recents_start_line = 0;
}

void ui_print_recents_title()
{
    int center, start;

    center = (tnc_cmd_box_w / 2) - 1;
    start = center - 9;
    if (start < 1)
        start = 1;
    mvwprintw(tnc_cmd_box, tnc_cmd_box_h - 1, start, " RECENT MESSAGES ");
    wrefresh(tnc_cmd_box);
}

void ui_print_recents()
{
    static int once = 0;
    char *p, recent[MAX_MBOX_HDR_SIZE+8];
    int i, max_cols, max_recents_rows, cur_recents_row = 0;

    if (!once) {
        once = 1;
        memset(&recents_list, 0, sizeof(recents_list));
    }

    if (show_recents && !ui_recents_win) {
        ui_recents_win = newwin(tnc_cmd_box_h - 2, tnc_cmd_box_w - 2,
                                     tnc_cmd_box_y + 1, tnc_cmd_box_x + 1);
        if (!ui_recents_win) {
            ui_print_status("Recents: failed to create window", 1);
            return;
        }
        max_recents_rows = tnc_cmd_box_h - 2;
        if (show_titles)
            ui_print_recents_title();
        refresh_recents = 1;
    }

    pthread_mutex_lock(&mutex_recents);
    p = cmdq_pop(&g_recents_q);
    pthread_mutex_unlock(&mutex_recents);

    if (p) {
        snprintf(recent, sizeof(recent), "%s", p);
        memmove(&recents_list[1], &recents_list[0], MAX_RECENTS_LIST_LEN*MAX_MBOX_HDR_SIZE);
        snprintf(recents_list[0], sizeof(recents_list[0]), "%s ---", recent);
        ++recents_list_cnt;
        if (recents_list_cnt > MAX_RECENTS_LIST_LEN)
            --recents_list_cnt;
        refresh_recents = 1;
    }
    if (show_recents && ui_recents_win && refresh_recents) {
        delwin(ui_recents_win);
        ui_recents_win = NULL;
        ui_recents_win = newwin(tnc_cmd_box_h - 2, tnc_cmd_box_w - 2,
                                     tnc_cmd_box_y + 1, tnc_cmd_box_x + 1);
        if (!ui_recents_win) {
            ui_print_status("Recents: failed to create window", 1);
            return;
        }
        max_recents_rows = tnc_cmd_box_h - 2;
        if (show_titles)
            ui_print_cmd_title();

        refresh_recents = 0;
        max_cols = (tnc_cmd_box_w - 4) + 1;
        if (max_cols > sizeof(recent))
            max_cols = sizeof(recent);
        cur_recents_row = 0;
        for (i = recents_start_line; i < recents_list_cnt &&
             i < (max_recents_rows + recents_start_line); i++) {
            snprintf(recent, max_cols, "[%3d] %s", i + 1, recents_list[i]);
            mvwprintw(ui_recents_win, cur_recents_row, recents_col, "%s", recent);
            cur_recents_row++;
        }
        touchwin(ui_recents_win);
        wrefresh(ui_recents_win);
    } else if ((!show_recents && ui_recents_win)) {
        delwin(ui_recents_win);
        ui_recents_win = NULL;
        if (show_titles)
            ui_print_cmd_title();
        touchwin(tnc_cmd_box);
        wrefresh(tnc_cmd_box);
        refresh_recents = recents_start_line = 0;
    }
}

void ui_refresh_recents()
{
    if (show_titles)
        ui_print_recents_title();
    refresh_recents = 1;
    ui_print_recents();
}

int ui_get_recent(int index, char *header, size_t size)
{
    if (index < MAX_RECENTS_LIST_LEN && recents_list[index][0]) {
        snprintf(header, size, "%s\n", recents_list[index]);
        return 1;
    }
    return 0;
}

int ui_set_recent_flag(const char *header, char flag)
{
    char *p;
    size_t i, len;

    for (i = 0; i < recents_list_cnt; i++) {
        len = strlen(recents_list[i]);
        if (len > 4 && !strncmp(header, recents_list[i], len)) {
            p = recents_list[i] + len;
            if (*(p - 4) == ' ') {
                switch(flag) {
                case 'R':
                    *(p - 3) = flag;
                    break;
                case 'F':
                    *(p - 2) = flag;
                    break;
                case 'S':
                    *(p - 1) = flag;
                    break;
                }
            }
            return 1;
        }
    }
    return 0;
}

void ui_clear_ptable()
{
    pthread_mutex_lock(&mutex_ptable);
    memset(&ptable_list, 0, sizeof(ptable_list));
    pthread_mutex_unlock(&mutex_ptable);
    if (show_ptable && ui_ptable_win) {
        delwin(ui_ptable_win);
        ui_ptable_win = NULL;
        ui_ptable_win = newwin(tnc_cmd_box_h - 2, tnc_cmd_box_w - 2,
                                     tnc_cmd_box_y + 1, tnc_cmd_box_x + 1);
        if (!ui_ptable_win) {
            ui_print_status("Ping History: failed to create window", 1);
            return;
        }
        touchwin(ui_ptable_win);
        wrefresh(ui_ptable_win);
    }
    refresh_ptable = ptable_list_cnt = ptable_start_line = 0;
}

void ui_print_ptable_title()
{
    int center, start;

    center = (tnc_cmd_box_w / 2) - 1;
    start = center - 10;
    if (start < 1)
        start = 1;
    if (last_time_heard == LT_HEARD_CLOCK)
        mvwprintw(tnc_cmd_box, tnc_cmd_box_h - 1, start, " PING HISTORY (LT) ");
    else
        mvwprintw(tnc_cmd_box, tnc_cmd_box_h - 1, start, " PING HISTORY (ET) ");
    wrefresh(tnc_cmd_box);
}

void ui_print_ptable()
{
    static int once = 0;
    static time_t tprev = 0;
    struct tm *ping_time;
    char *p, ping_data[MAX_PTABLE_ROW_SIZE];
    char in_time[16], out_time[16];
    int max_cols, max_ptable_rows, cur_ptable_row = 0;
    int i, days, hours, minutes;
    time_t tcur, telapsed;

    if (!once) {
        once = 1;
        memset(&ptable_list, 0, sizeof(ptable_list));
    }

    if (show_ptable && !ui_ptable_win) {
        ui_ptable_win = newwin(tnc_cmd_box_h - 2, tnc_cmd_box_w - 2,
                                     tnc_cmd_box_y + 1, tnc_cmd_box_x + 1);
        if (!ui_ptable_win) {
            ui_print_status("Ping History: failed to create window", 1);
            return;
        }
        max_ptable_rows = tnc_cmd_box_h - 2;
        if (show_titles)
            ui_print_ptable_title();
        refresh_ptable = 1;
    }

    pthread_mutex_lock(&mutex_ptable);
    p = cmdq_pop(&g_ptable_q);
    pthread_mutex_unlock(&mutex_ptable);

    /*
      layout of record taken from queue:
         byte 0:     in/out indicator
         byte 1-12:  callsign
         byte 13-15: inbound s/n ratio
         byte 16-18: inbound quality
         byte 19-21: outbound s/n ratio
         byte 22-24: outbound quality
    */

    if (p) {
        memmove(&ptable_list[1], &ptable_list[0], MAX_PTABLE_LIST_LEN * sizeof(PT_ENTRY));
        memset(&ptable_list[0], 0, sizeof(PT_ENTRY));
        snprintf(ptable_list[0].call, sizeof(ptable_list[0].call), "%.11s", &p[1]);
        if (p[0] == 'R')
            ptable_list[0].in_time = time(NULL);
        else
            ptable_list[0].out_time = time(NULL);
        snprintf(ptable_list[0].in_sn, sizeof(ptable_list[0].in_sn), "%.3s", &p[13]);
        snprintf(ptable_list[0].in_qual, sizeof(ptable_list[0].in_qual), "%.3s", &p[16]);
        snprintf(ptable_list[0].out_sn, sizeof(ptable_list[0].out_sn), "%.3s", &p[19]);
        snprintf(ptable_list[0].out_qual, sizeof(ptable_list[0].out_qual), "%.3s", &p[22]);
        ++ptable_list_cnt;
        for (i = 1; i < ptable_list_cnt; i++) {
            if (!strncasecmp(ptable_list[0].call, ptable_list[i].call, TNC_MYCALL_SIZE)) {
                /* copy existing complementary data before overwriting the record */
                if (p[0] == 'R') {
                    ptable_list[0].out_time = ptable_list[i].out_time;
                    snprintf(ptable_list[0].out_sn, sizeof(ptable_list[0].out_sn),
                                                            "%s", ptable_list[i].out_sn);
                    snprintf(ptable_list[0].out_qual, sizeof(ptable_list[0].out_qual),
                                                            "%s", ptable_list[i].out_qual);
                } else {
                    ptable_list[0].in_time = ptable_list[i].in_time;
                    snprintf(ptable_list[0].in_sn, sizeof(ptable_list[0].in_sn),
                                                            "%s", ptable_list[i].in_sn);
                    snprintf(ptable_list[0].in_qual, sizeof(ptable_list[0].in_qual),
                                                            "%s", ptable_list[i].in_qual);
                }
                memmove(&ptable_list[i], &ptable_list[i + 1],
                            (MAX_PTABLE_LIST_LEN - i) * sizeof(PT_ENTRY));
                --ptable_list_cnt;
                break;
            }
        }
        if (ptable_list_cnt > MAX_PTABLE_LIST_LEN)
            --ptable_list_cnt;
        refresh_ptable = 1;
    }
    tcur = time(NULL);
    if ((tcur - tprev) > 15) {
        tprev = tcur;
        refresh_ptable = 1;
    }
    if (show_ptable && ui_ptable_win && refresh_ptable) {
        delwin(ui_ptable_win);
        ui_ptable_win = NULL;
        ui_ptable_win = newwin(tnc_cmd_box_h - 2, tnc_cmd_box_w - 2,
                                     tnc_cmd_box_y + 1, tnc_cmd_box_x + 1);
        if (!ui_ptable_win) {
            ui_print_status("Ping History: failed to create window", 1);
            return;
        }
        max_ptable_rows = tnc_cmd_box_h - 2;
        if (show_titles)
            ui_print_ptable_title();
        refresh_ptable = 0;
        max_cols = (tnc_cmd_box_w - 4) + 1;
        if (max_cols > sizeof(ping_data))
            max_cols = sizeof(ping_data);
        cur_ptable_row = 0;
        for (i = ptable_start_line; i < ptable_list_cnt &&
             i < (max_ptable_rows + ptable_start_line); i++) {
            if (ptable_list[i].in_time) {
                if (last_time_heard == LT_HEARD_CLOCK) {
                    pthread_mutex_lock(&mutex_time);
                    if (!strncasecmp(g_ui_settings.utc_time, "TRUE", 4))
                        ping_time = gmtime(&ptable_list[i].in_time);
                    else
                        ping_time = localtime(&ptable_list[i].in_time);
                    pthread_mutex_unlock(&mutex_time);
                    snprintf(in_time, sizeof(in_time),
                        "%02d:%02d:%02d", ping_time->tm_hour,
                            ping_time->tm_min, ping_time->tm_sec);
                } else {
                    telapsed = tcur - ptable_list[i].in_time;
                    days = telapsed / (24*60*60);
                    if (days > 99)
                        days = 99;
                    telapsed = telapsed % (24*60*60);
                    hours = telapsed / (60*60);
                    telapsed = telapsed % (60*60);
                    minutes = telapsed / 60;
                    snprintf(in_time, sizeof(in_time),
                                "%02d:%02d:%02d", days, hours, minutes);
                }
            } else {
                snprintf(in_time, sizeof(in_time), "--:--:--");
            }
            if (ptable_list[i].out_time) {
                if (last_time_heard == LT_HEARD_CLOCK) {
                    pthread_mutex_lock(&mutex_time);
                    if (!strncasecmp(g_ui_settings.utc_time, "TRUE", 4))
                        ping_time = gmtime(&ptable_list[i].out_time);
                    else
                        ping_time = localtime(&ptable_list[i].out_time);
                    pthread_mutex_unlock(&mutex_time);
                    snprintf(out_time, sizeof(out_time),
                        "%02d:%02d:%02d", ping_time->tm_hour,
                            ping_time->tm_min, ping_time->tm_sec);
                } else {
                    telapsed = tcur - ptable_list[i].out_time;
                    days = telapsed / (24*60*60);
                    if (days > 99)
                        days = 99;
                    telapsed = telapsed % (24*60*60);
                    hours = telapsed / (60*60);
                    telapsed = telapsed % (60*60);
                    minutes = telapsed / 60;
                    snprintf(out_time, sizeof(out_time),
                                "%02d:%02d:%02d", days, hours, minutes);
                }
            } else {
                snprintf(out_time, sizeof(out_time), "--:--:--");
            }
            snprintf(ping_data, max_cols,
                "[%2d]%.11s [%s]>>S/N:%3sdB,Q:%3s  [%s]<<S/N:%3sdB,Q:%3s",
                    i + 1, ptable_list[i].call, in_time, ptable_list[i].in_sn,
                        ptable_list[i].in_qual, out_time, ptable_list[i].out_sn,
                                ptable_list[i].out_qual);
            mvwprintw(ui_ptable_win, cur_ptable_row, ptable_col, "%s", ping_data);
            cur_ptable_row++;
        }
        touchwin(ui_ptable_win);
        wrefresh(ui_ptable_win);
    } else if ((!show_ptable && ui_ptable_win)) {
        delwin(ui_ptable_win);
        ui_ptable_win = NULL;
        if (show_titles)
            ui_print_cmd_title();
        touchwin(tnc_cmd_box);
        wrefresh(tnc_cmd_box);
        refresh_ptable = ptable_start_line = 0;
    }
}

void ui_refresh_ptable()
{
    if (show_titles)
        ui_print_ptable_title();
    refresh_ptable = 1;
    ui_print_ptable();
}

int ui_init_color()
{
    if (!has_colors())
        return 0;
    if (start_color() != OK)
        return 0;
    if (COLORS < 8 || COLOR_PAIRS < 8)
        return 0;
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_BLUE, COLOR_BLACK);
    init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(6, COLOR_CYAN, COLOR_BLACK);
    init_pair(7, COLOR_WHITE, COLOR_BLACK);
    return 1;
}

void ui_end()
{
    endwin();
}

int ui_init()
{
    static int once = 0;

    if (!once) {
        once = 1;
        main_win = initscr();
        if (!strncasecmp(g_ui_settings.last_time_heard, "ELAPSED", 7))
            last_time_heard = LT_HEARD_ELAPSED;
        else
            last_time_heard = LT_HEARD_CLOCK;
    }
    if (!strncasecmp(g_ui_settings.show_titles, "TRUE", 4))
        show_titles = 1;
    else
        show_titles = 0;
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4))
        mon_timestamp = 1;
    else
        mon_timestamp = 0;
    if (!strncasecmp(g_ui_settings.color_code, "TRUE", 4))
        color_code = ui_init_color();
    else
        color_code = 0;
    prompt_row = 0, prompt_col = 1;
    status_row = LINES - 2;
    status_col = 1;
    cmd_row = 0, cmd_col = 0, cur_cmd_row = 0;
    data_row = 0, data_col = 0;
    cur_data_row = data_row;
    recents_row = 0, recents_col = 1;
    cur_recents_row = recents_row;
    ptable_row = 0, ptable_col = 1;
    cur_ptable_row = ptable_row;

    ui_list_box_h = LINES - 4;
    ui_list_box_w = LIST_BOX_WIDTH;
    ui_list_box_y = 2;
    ui_list_box_x = COLS - LIST_BOX_WIDTH - 1;
    ui_list_box = newwin(ui_list_box_h, ui_list_box_w, ui_list_box_y, ui_list_box_x);
    if (!ui_list_box) {
        ui_end();
        return 0;
    }
    box(ui_list_box, 0, 0);
    ui_list_win = derwin(ui_list_box, ui_list_box_h - 2, ui_list_box_w - 2, 1, 1);
    if (!ui_list_win) {
        ui_end();
        return 0;
    }
    max_list_rows = ui_list_box_h - 2;
    if (show_titles)
        ui_print_heard_list_title();

    prompt_box_h = 3;
    prompt_box_w = COLS - ui_list_box_w - 3;
    prompt_box_y = LINES - 5;
    prompt_box_x = 1;
    prompt_box = subwin(main_win, prompt_box_h, prompt_box_w, prompt_box_y, prompt_box_x);
    if (!prompt_box) {
        ui_end();
        return 0;
    }
    box(prompt_box, 0, 0);
    prompt_win = derwin(prompt_box, 1, prompt_box_w - 2, 1, 1);
    if (!prompt_win) {
        ui_end();
        return 0;
    }
    wtimeout(prompt_win, 100);

    tnc_cmd_box_h = (LINES / 3) - 1;
    if (show_titles && tnc_cmd_box_h < 5)
        tnc_cmd_box_h = 5;
    else if (tnc_cmd_box_h < 4)
        tnc_cmd_box_h = 4;
    tnc_cmd_box_w = COLS - ui_list_box_w - 3;
    tnc_cmd_box_y = prompt_box_y - tnc_cmd_box_h;
    tnc_cmd_box_x = 1;
    tnc_cmd_box = newwin(tnc_cmd_box_h, tnc_cmd_box_w, tnc_cmd_box_y, tnc_cmd_box_x);
    if (!tnc_cmd_box) {
        ui_end();
        return 0;
    }
    box(tnc_cmd_box, 0, 0);
    tnc_cmd_win = derwin(tnc_cmd_box, tnc_cmd_box_h - 2, tnc_cmd_box_w - 2, 1, 1);
    if (!tnc_cmd_win) {
        ui_end();
        return 0;
    }
    max_cmd_rows = tnc_cmd_box_h - 2;
    scrollok(tnc_cmd_win, TRUE);
    if (show_titles) {
        ui_print_cmd_title();
    }
    show_cmds = 1;

    tnc_data_box_h = LINES - tnc_cmd_box_h - prompt_box_h - 4;
    tnc_data_box_w = COLS - ui_list_box_w - 3;
    tnc_data_box_y = 2;
    tnc_data_box_x = 1;
    tnc_data_box = newwin(tnc_data_box_h, tnc_data_box_w, tnc_data_box_y, tnc_data_box_x);
    if (!tnc_data_box) {
        ui_end();
        return 0;
    }
    box(tnc_data_box, 0, 0);
    tnc_data_win = derwin(tnc_data_box, tnc_data_box_h - 2, tnc_data_box_w - 2, 1, 1);
    if (!tnc_data_win) {
        ui_end();
        return 0;
    }
    max_data_rows = tnc_data_box_h - 2;
    scrollok(tnc_data_win, TRUE);
    if (show_titles) {
        ui_print_data_title();
    }
    curs_set(0);
    wrefresh(main_win);
    wrefresh(ui_list_box);
    wrefresh(tnc_data_box);
    wrefresh(tnc_cmd_box);
    ui_set_active_win(tnc_data_box);
    return 1;
}

int ui_run()
{
    int cmd, temp, quit = 0;

    cbreak();
    keypad(stdscr, TRUE);
    noecho();
    nodelay(main_win, TRUE);
    clear();
    ui_print_title(NULL);
    ui_print_status(MENU_PROMPT_STR, 0);

    while (!quit) {
        if ((status_timer && --status_timer == 0) ||
            (data_buf_scroll_timer && --data_buf_scroll_timer == 0)) {
            if (arim_is_arq_state())
                ui_print_status(ARQ_PROMPT_STR, 0);
            else
                ui_print_status(MENU_PROMPT_STR, 0);
        }
        cmd = getch();
        switch (cmd) {
        case 'q':
        case 'Q':
            cmd = ui_show_dialog("\tAre you sure\n\tyou want to quit?\n \n\t[Y]es   [N]o", "yYnN");
            if (cmd == 'y' || cmd == 'Y') {
                quit = 1;
                temp = 0;
                if (arim_get_state() == ST_ARQ_CONNECTED) {
                    arim_arq_send_disconn_req();
                    while (arim_get_state() != ST_IDLE && temp < 50) {
                        /* wait for disconnect, time out after 5 seconds */
                        ui_print_cmd_in();
                        if (!data_buf_scroll_timer)
                            ui_print_data_in();
                        ui_check_status_dirty();
                        ++temp;
                        usleep(100000);
                    }
                }
                ui_print_status("Shutting down...", 0);
            }
            break;
        case 27:
            ui_on_cancel();
            break;
        case 't':
        case 'T':
            if (last_time_heard == LT_HEARD_ELAPSED) {
                last_time_heard = LT_HEARD_CLOCK;
                ui_print_status("Showing clock time in Heard List and Ping History, press 't' to toggle", 1);
            } else {
                last_time_heard = LT_HEARD_ELAPSED;
                ui_print_status("Showing elapsed time in Heard List and Ping History, press 't' to toggle", 1);
            }
            ui_refresh_heard_list();
            if (show_ptable)
                ui_refresh_ptable();
            break;
        case 'r':
        case 'R':
            if (show_ptable)
                break;
            if (show_recents) {
                show_recents = 0;
                ui_print_status("Showing TNC cmds, press 'r' to toggle", 1);
                break;
            }
            if (!arim_is_arq_state()) {
                if (!show_recents) {
                    show_recents = 1;
                    ui_print_status("Showing Recents, <SP> 'rr n' read, 'u' or 'd' to scroll, 'r' to toggle", 1);
                }
            } else {
                ui_print_status("Recent Messages view not available in ARQ session", 1);
            }
            break;
        case 'p':
        case 'P':
            if (show_recents)
                break;
            if (show_ptable) {
                show_ptable = 0;
                ui_print_status("Showing TNC cmds, press 'p' to toggle", 1);
                break;
            }
            if (!arim_is_arq_state()) {
                if (!show_ptable) {
                    show_ptable = 1;
                    ui_print_status("Showing Pings, <SP> 'u' or 'd' to scroll, 'p' to toggle", 1);
                }
            } else {
                ui_print_status("Ping History view not available in ARQ session", 1);
            }
            break;
        case 'd':
            if (show_ptable && ptable_list_cnt) {
                ptable_start_line++;
                if (ptable_start_line >= ptable_list_cnt)
                    ptable_start_line = ptable_list_cnt - 1;
                ui_refresh_ptable();
            }
            else if (show_recents && recents_list_cnt) {
                recents_start_line++;
                if (recents_start_line >= recents_list_cnt)
                    recents_start_line = recents_list_cnt - 1;
                ui_refresh_recents();
            }
            break;
        case 'u':
            if (show_ptable && ptable_list_cnt) {
                ptable_start_line--;
                if (ptable_start_line < 0)
                    ptable_start_line = 0;
                ui_refresh_ptable();
            }
            else if (show_recents && recents_list_cnt) {
                recents_start_line--;
                if (recents_start_line < 0)
                    recents_start_line = 0;
                ui_refresh_recents();
            }
            break;
        case 'f':
        case 'F':
            if (g_tnc_attached) {
                if (!arim_is_arq_state()) {
                    ui_show_fec_menu();
                } else {
                    ui_print_status("Listing shared files directory", 1);
                    ui_list_shared_files();
                }
                status_timer = 1;
            } else {
                ui_print_status("FEC control menu only available when TNC attached", 1);
            }
            break;
        case 'O':
        case 'o':
            if (arim_is_arq_state()) {
                ui_print_status("Listing messages in outbox", 1);
                ui_list_msg(MBOX_OUTBOX_FNAME, MBOX_TYPE_OUT);
            }
            break;
        case 'I':
        case 'i':
            if (arim_is_arq_state()) {
                ui_print_status("Listing messages in inbox", 1);
                ui_list_msg(MBOX_INBOX_FNAME, MBOX_TYPE_IN);
            }
            break;
        case 'S':
        case 's':
            if (arim_is_arq_state()) {
                ui_print_status("Listing sent messages", 1);
                ui_list_msg(MBOX_SENTBOX_FNAME, MBOX_TYPE_SENT);
            }
            break;
        case 'n':
        case 'N':
            ui_clear_new_ctrs();
            break;
        case 'h':
        case 'H':
            ui_show_help();
            status_timer = 1;
            break;
        case ' ':
            ui_cmd_prompt();
            break;
        case KEY_HOME:
            if (!data_buf_scroll_timer)
                ui_print_status(DATA_WIN_SCROLL_LEGEND, 0);
            data_buf_scroll_timer = DATA_BUF_SCROLLING_TIME;
            data_buf_top = data_buf_start;
            ui_refresh_data_win();
            break;
        case KEY_END:
            if (!data_buf_scroll_timer)
                ui_print_status(DATA_WIN_SCROLL_LEGEND, 0);
            data_buf_scroll_timer = DATA_BUF_SCROLLING_TIME;
            if (data_buf_cnt < max_data_rows)
                break;
            data_buf_top = data_buf_end - max_data_rows;
            if (data_buf_top < 0)
                data_buf_top += MAX_DATA_BUF_LEN;
            ui_refresh_data_win();
            break;
        case KEY_PPAGE:
            if (!data_buf_scroll_timer)
                ui_print_status(DATA_WIN_SCROLL_LEGEND, 0);
            data_buf_scroll_timer = DATA_BUF_SCROLLING_TIME;
            if (data_buf_top == data_buf_start)
                break;
            /* calculate distance from start */
            temp = data_buf_top - data_buf_start;
            if (temp < 0)
                temp += MAX_DATA_BUF_LEN;
            if (temp <= max_data_rows) {
                data_buf_top = data_buf_start;
            } else {
                data_buf_top -= max_data_rows;
                if (data_buf_top < 0)
                    data_buf_top += MAX_DATA_BUF_LEN;
            }
            ui_refresh_data_win();
            break;
        case KEY_UP:
            if (!data_buf_scroll_timer)
                ui_print_status(DATA_WIN_SCROLL_LEGEND, 0);
            data_buf_scroll_timer = DATA_BUF_SCROLLING_TIME;
            if (data_buf_top == data_buf_start)
                break;
            data_buf_top -= 1;
            if (data_buf_top < 0)
                data_buf_top += MAX_DATA_BUF_LEN;
            ui_refresh_data_win();
            break;
        case KEY_NPAGE:
            if (!data_buf_scroll_timer)
                ui_print_status(DATA_WIN_SCROLL_LEGEND, 0);
            data_buf_scroll_timer = DATA_BUF_SCROLLING_TIME;
            if (data_buf_top == data_buf_end || data_buf_cnt < max_data_rows)
                break;
            /* calculate distance from end */
            temp = data_buf_end - data_buf_top;
            if (temp < 0)
                temp += MAX_DATA_BUF_LEN;
            if (temp <= max_data_rows)
                break;
            data_buf_top += max_data_rows;
            if (data_buf_top >= MAX_DATA_BUF_LEN)
                data_buf_top -= MAX_DATA_BUF_LEN;
            if (data_buf_top == data_buf_end)
                break;
            ui_refresh_data_win();
            break;
        case KEY_DOWN:
            if (!data_buf_scroll_timer)
                ui_print_status(DATA_WIN_SCROLL_LEGEND, 0);
            data_buf_scroll_timer = DATA_BUF_SCROLLING_TIME;
            if (data_buf_top == data_buf_end)
                break;
            data_buf_top += 1;
            if (data_buf_top >= MAX_DATA_BUF_LEN)
                data_buf_top -= MAX_DATA_BUF_LEN;
            if (data_buf_top == data_buf_end)
                break;
            ui_refresh_data_win();
            break;
        case 'C':
        case 'c':
            data_buf_scroll_timer = 1;
            break;
        case 24: /* CTRL-X */
            if (arim_is_arq_state()) {
                cmd = ui_show_dialog("\tAre you sure\n\tyou want to disconnect?\n \n\t[Y]es   [N]o", "yYnN");
                if (cmd == 'y' || cmd == 'Y')
                    arim_arq_send_disconn_req();
            }
            break;
        default:
            ui_print_cmd_in();
            ui_print_recents();
            ui_print_ptable();
            if (!data_buf_scroll_timer)
                ui_print_data_in();
            ui_print_heard_list();
            ui_check_status_dirty();
            box(prompt_box, 0, 0);
            wrefresh(prompt_box);
            break;
        }
        if (g_new_install) {
            g_new_install = 0;
            ui_show_dialog("\tThis is a new installation!\n"
                           "\tYou should edit the " DEFAULT_INI_FNAME " file\n"
                           "\tto set your callsign and configure ARIM.\n \n\t[O]k", "oO \n");
        }
        if (g_win_changed) {
            /* terminal size changed, prepare to redraw ui */
            g_win_changed = 0;
            win_change_timer = WIN_CHANGE_TIMER_COUNT;
            show_recents = show_ptable = 0;
        } else if (win_change_timer && --win_change_timer == 0) {
                /* wipe screen and redraw ui */
                ui_end();
                clear();
                refresh();
                ui_init();
                ui_refresh_heard_list();
                ui_set_title_dirty(TITLE_REFRESH);
                status_timer = 1;
        }
        usleep(100000);
    }
    return 0;
}

