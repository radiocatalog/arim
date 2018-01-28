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

#ifndef _UI_H_INCLUDED_
#define _UI_H_INCLUDED_

#include <curses.h>

#define TITLE_ROW                   1
#define STATUS_TIMER_COUNT          25
#define WIN_CHANGE_TIMER_COUNT      10
#define MENU_PROMPT_STR             "Hot keys: <SP> Cmd Prompt, 'r' Recents, 'p' Pings,'f' FEC Ctl, 'h' Help, 'q' Quit."
#define ARQ_PROMPT_STR              "Hot keys: <SP> Cmd Prompt, CTRL-X Disc, 'f' Files, 'i' Inbox, 'o' Outbox, 's' Sent, 'h' Help."
#define LIST_BOX_WIDTH              26

#define LT_HEARD_CLOCK              0
#define LT_HEARD_ELAPSED            1

#define TITLE_REFRESH               1
#define TITLE_TNC_DETACHED          2
#define TITLE_TNC_ATTACHED          3

#define STATUS_REFRESH              1
#define STATUS_WAIT_ACK             2
#define STATUS_WAIT_RESP            3
#define STATUS_BEACON_SENT          4
#define STATUS_NET_MSG_SENT         5
#define STATUS_MSG_ACK_RCVD         6
#define STATUS_MSG_NAK_RCVD         7
#define STATUS_RESP_RCVD            8
#define STATUS_ACK_TIMEOUT          9
#define STATUS_RESP_TIMEOUT         10
#define STATUS_FRAME_START          11
#define STATUS_FRAME_END            12
#define STATUS_ARIM_FRAME_TO        13
#define STATUS_RESP_SEND            14
#define STATUS_ACKNAK_SEND          15
#define STATUS_MSG_RCVD             16
#define STATUS_NET_MSG_RCVD         17
#define STATUS_QUERY_RCVD           18
#define STATUS_MSG_REPEAT           19
#define STATUS_RESP_SENT            20
#define STATUS_ACKNAK_SENT          21
#define STATUS_MSG_SEND_CAN         22
#define STATUS_QRY_SEND_CAN         23
#define STATUS_RESP_SEND_CAN        24
#define STATUS_ACKNAK_SEND_CAN      25
#define STATUS_BCN_SEND_CAN         26
#define STATUS_SEND_UNPROTO_CAN     27
#define STATUS_RESP_WAIT_CAN        30
#define STATUS_FRAME_WAIT_CAN       31
#define STATUS_MSG_START            32
#define STATUS_QRY_START            33
#define STATUS_RESP_START           34
#define STATUS_BCN_START            35
#define STATUS_MSG_END              36
#define STATUS_QRY_END              37
#define STATUS_RESP_END             38
#define STATUS_BCN_END              39
#define STATUS_PING_RCVD            40
#define STATUS_PING_SENT            41
#define STATUS_PING_ACK_RCVD        42
#define STATUS_PING_SEND_CAN        43
#define STATUS_PING_ACK_TIMEOUT     44
#define STATUS_PING_MSG_SEND        45
#define STATUS_PING_MSG_ACK_BAD     46
#define STATUS_PING_MSG_ACK_TO      47
#define STATUS_PING_QRY_SEND        48
#define STATUS_PING_QRY_ACK_TO      49
#define STATUS_PING_QRY_ACK_BAD     50
#define STATUS_ARQ_CONN_REQ         51
#define STATUS_ARQ_CONN_CAN         52
#define STATUS_ARQ_CONNECTED        53
#define STATUS_ARQ_DISCONNECTED     54
#define STATUS_ARQ_CONN_REQ_SENT    55
#define STATUS_ARQ_CONN_REQ_TO      56
#define STATUS_ARQ_CONN_PP_SEND     57
#define STATUS_ARQ_CONN_PP_ACK_TO   58
#define STATUS_ARQ_CONN_PP_ACK_BAD  59
#define STATUS_ARQ_CONN_TIMEOUT     60
#define STATUS_ARQ_FILE_RCV_WAIT    61
#define STATUS_ARQ_FILE_RCV         62
#define STATUS_ARQ_FILE_RCV_DONE    63
#define STATUS_ARQ_FILE_RCV_ERROR   64
#define STATUS_ARQ_FILE_SEND        65
#define STATUS_ARQ_FILE_SEND_DONE   66
#define STATUS_ARQ_FILE_SEND_ACK    67
#define STATUS_ARQ_MSG_RCV          68
#define STATUS_ARQ_MSG_RCV_DONE     69
#define STATUS_ARQ_MSG_RCV_ERROR    70
#define STATUS_ARQ_MSG_SEND         71
#define STATUS_ARQ_MSG_SEND_ACK     72
#define STATUS_ARQ_MSG_SEND_DONE    73
#define STATUS_ARQ_AUTH_BUSY        74
#define STATUS_ARQ_AUTH_OK          75
#define STATUS_ARQ_AUTH_ERROR       76
#define STATUS_ARQ_EAUTH_LOCAL      77
#define STATUS_ARQ_EAUTH_REMOTE     78
#define STATUS_ARQ_RUN_CACHED_CMD   79

extern WINDOW *main_win;
extern WINDOW *tnc_data_win;
extern WINDOW *tnc_data_box;
extern WINDOW *tnc_cmd_win;

extern WINDOW *main_win;
extern WINDOW *tnc_data_win;
extern WINDOW *tnc_data_box;
extern WINDOW *tnc_cmd_win;
extern WINDOW *tnc_cmd_box;
extern WINDOW *prompt_box;
extern WINDOW *prompt_win;

extern int data_buf_scroll_timer;
extern int status_timer;
extern int status_dirty;
extern int show_recents;
extern int show_ptable;
extern int show_titles;
extern int show_cmds;
extern int last_time_heard;
extern int recents_list_cnt;
extern int recents_start_line;
extern int ptable_list_cnt;
extern int ptable_start_line;

extern int ui_list_box_y, ui_list_box_x, ui_list_box_w, ui_list_box_h;
extern int tnc_data_box_y, tnc_data_box_x, tnc_data_box_w, tnc_data_box_h;
extern int tnc_cmd_box_y, tnc_cmd_box_x, tnc_cmd_box_w, tnc_cmd_box_h;
extern int prompt_box_y, prompt_box_x, prompt_box_w, prompt_box_h;
extern int prompt_row, prompt_col;

extern int num_new_msgs;
extern int num_new_files;

extern int ui_init(void);
extern int ui_run(void);
extern void ui_end(void);
extern void ui_print_status(const char *text, int temporary);
extern void ui_print_data_in(void);
extern void ui_print_data_title(void);
extern void ui_print_title(const char *status);
extern void ui_print_recents(void);
extern void ui_refresh_recents(void);
extern void ui_print_ptable(void);
extern void ui_refresh_ptable(void);
extern int ui_get_recent(int index, char *header, size_t size);
extern int ui_set_recent_flag(const char *header, char flag);
extern void ui_print_heard_list(void);
extern void ui_refresh_heard_list(void);
extern void ui_queue_data_in(const char *text);
extern void ui_queue_data_out(const char *text);
extern void ui_print_cmd_in(void);
extern void ui_print_cmd_title(void);
extern void ui_queue_cmd_out(const char *text);
extern void ui_queue_debug_log(const char *text);
extern void ui_queue_traffic_log(const char *text);
extern void ui_queue_heard(const char *text);
extern void ui_queue_ptable(const char *text);
extern void ui_get_heard_list(char *listbuf, size_t listbufsize);
extern void ui_on_cancel(void);
extern void ui_set_title_dirty(int val);
extern void ui_set_status_dirty(int val);
extern void ui_check_status_dirty(void);
extern WINDOW *ui_set_active_win(WINDOW *win);
extern void ui_clear_data_in(void);
extern void ui_clear_calls_heard(void);
extern void ui_clear_ptable(void);
extern void ui_clear_recents(void);
extern void ui_clear_new_ctrs(void);

#endif

