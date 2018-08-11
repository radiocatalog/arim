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

#define TITLE_ROW                       1
#define STATUS_TIMER_COUNT              50
#define WIN_CHANGE_TIMER_COUNT          10
#define MENU_PROMPT_STR                 "Hot keys: <SP> Cmd Prompt, 'r' Recents, 'p' Pings,'f' FEC Ctl, 'h' Help, 'q' Quit."
#define ARQ_PROMPT_STR                  "Hot keys: <SP> Cmd Prompt, CTRL-X Disc, 'f' Files, 'i' Inbox, 'o' Outbox, 's' Sent, 'h' Help."
#define LIST_BOX_WIDTH                  26

#define LT_HEARD_CLOCK                  0
#define LT_HEARD_ELAPSED                1

#define TITLE_REFRESH                   1
#define TITLE_TNC_DETACHED              2
#define TITLE_TNC_ATTACHED              3

#define STATUS_REFRESH                  1
#define STATUS_MSG_WAIT_ACK             2
#define STATUS_WAIT_RESP                3
#define STATUS_BEACON_SENT              4
#define STATUS_NET_MSG_SENT             5
#define STATUS_MSG_ACK_RCVD             6
#define STATUS_MSG_NAK_RCVD             7
#define STATUS_RESP_RCVD                8
#define STATUS_MSG_ACK_TIMEOUT          9
#define STATUS_RESP_TIMEOUT             10
#define STATUS_FRAME_START              11
#define STATUS_FRAME_END                12
#define STATUS_ARIM_FRAME_TO            13
#define STATUS_RESP_SEND                14
#define STATUS_ACKNAK_SEND              15
#define STATUS_MSG_RCVD                 16
#define STATUS_NET_MSG_RCVD             17
#define STATUS_QUERY_RCVD               18
#define STATUS_MSG_REPEAT               19
#define STATUS_RESP_SENT                20
#define STATUS_ACKNAK_SENT              21
#define STATUS_MSG_SEND_CAN             22
#define STATUS_QRY_SEND_CAN             23
#define STATUS_RESP_SEND_CAN            24
#define STATUS_ACKNAK_SEND_CAN          25
#define STATUS_BCN_SEND_CAN             26
#define STATUS_SEND_UNPROTO_CAN         27
#define STATUS_RESP_WAIT_CAN            30
#define STATUS_FRAME_WAIT_CAN           31
#define STATUS_MSG_START                32
#define STATUS_QRY_START                33
#define STATUS_RESP_START               34
#define STATUS_BCN_START                35
#define STATUS_MSG_END                  36
#define STATUS_QRY_END                  37
#define STATUS_RESP_END                 38
#define STATUS_BCN_END                  39
#define STATUS_PING_RCVD                40
#define STATUS_PING_SENT                41
#define STATUS_PING_ACK_RCVD            42
#define STATUS_PING_SEND_CAN            43
#define STATUS_PING_ACK_TIMEOUT         44
#define STATUS_PING_MSG_SEND            45
#define STATUS_PING_MSG_ACK_BAD         46
#define STATUS_PING_MSG_ACK_TO          47
#define STATUS_PING_QRY_SEND            48
#define STATUS_PING_QRY_ACK_TO          49
#define STATUS_PING_QRY_ACK_BAD         50
#define STATUS_ARQ_CONN_REQ             51
#define STATUS_ARQ_CONN_CAN             52
#define STATUS_ARQ_CONNECTED            53
#define STATUS_ARQ_DISCONNECTED         54
#define STATUS_ARQ_CONN_REQ_SENT        55
#define STATUS_ARQ_CONN_REQ_FAIL        56
#define STATUS_ARQ_CONN_PP_SEND         57
#define STATUS_ARQ_CONN_PP_ACK_TO       58
#define STATUS_ARQ_CONN_PP_ACK_BAD      59
#define STATUS_ARQ_CONN_TIMEOUT         60
#define STATUS_ARQ_FILE_RCV_WAIT        61
#define STATUS_ARQ_FILE_RCV             62
#define STATUS_ARQ_FILE_RCV_DONE        63
#define STATUS_ARQ_FILE_RCV_ERROR       64
#define STATUS_ARQ_FILE_RCV_TIMEOUT     65
#define STATUS_ARQ_FILE_SEND            66
#define STATUS_ARQ_FILE_SEND_DONE       67
#define STATUS_ARQ_FILE_SEND_ERROR      68
#define STATUS_ARQ_FILE_SEND_ACK        69
#define STATUS_ARQ_FILE_SEND_TIMEOUT    70
#define STATUS_ARQ_MSG_RCV              71
#define STATUS_ARQ_MSG_RCV_DONE         72
#define STATUS_ARQ_MSG_RCV_ERROR        73
#define STATUS_ARQ_MSG_RCV_TIMEOUT      74
#define STATUS_ARQ_MSG_SEND             75
#define STATUS_ARQ_MSG_SEND_ACK         76
#define STATUS_ARQ_MSG_SEND_ERROR       77
#define STATUS_ARQ_MSG_SEND_TIMEOUT     78
#define STATUS_ARQ_MSG_SEND_DONE        79
#define STATUS_ARQ_AUTH_BUSY            80
#define STATUS_ARQ_AUTH_OK              81
#define STATUS_ARQ_AUTH_ERROR           82
#define STATUS_ARQ_EAUTH_LOCAL          83
#define STATUS_ARQ_EAUTH_REMOTE         84
#define STATUS_ARQ_RUN_CACHED_CMD       85
#define STATUS_ARQ_FLIST_RCV_WAIT       86
#define STATUS_ARQ_FLIST_RCV            87
#define STATUS_ARQ_FLIST_RCV_DONE       88
#define STATUS_ARQ_FLIST_RCV_ERROR      89
#define STATUS_ARQ_FLIST_RCV_TIMEOUT    90
#define STATUS_ARQ_FLIST_SEND           91
#define STATUS_ARQ_FLIST_SEND_DONE      92
#define STATUS_ARQ_FLIST_SEND_ERROR     93
#define STATUS_ARQ_FLIST_SEND_ACK       94
#define STATUS_ARQ_FLIST_SEND_TIMEOUT   95
#define STATUS_ARQ_CONN_REQ_REPEAT      96

#define STATUS_XFER_PROG_START          101
#define STATUS_XFER_PROG_UPDATE         102
#define STATUS_XFER_PROG_END            103

#define STATUS_XFER_DIR_DOWN            0
#define STATUS_XFER_DIR_UP              1

extern WINDOW *main_win;
extern int color_code;
extern int mon_timestamp;
extern int data_buf_scroll_timer;
extern int status_timer;
extern int status_dirty;
extern int show_titles;
extern int show_cmds;
extern int last_time_heard;

extern int num_new_msgs;
extern int num_new_files;

extern int ui_init(void);
extern int ui_run(void);
extern void ui_end(void);
extern void ui_print_status(const char *text, int temporary);
extern void ui_print_title(const char *status);
extern void ui_on_cancel(void);
extern void ui_set_title_dirty(int val);
extern void ui_set_status_dirty(int val);
extern void ui_check_status_dirty(void);
extern WINDOW *ui_set_active_win(WINDOW *win);
extern void ui_clear_calls_heard(void);
extern void ui_clear_new_ctrs(void);
extern void ui_truncate_line(char *line, size_t size);
extern void ui_status_xfer_start(int min, int max, int dir);
extern void ui_status_xfer_update(int val);
extern void ui_status_xfer_end(void);

#endif

