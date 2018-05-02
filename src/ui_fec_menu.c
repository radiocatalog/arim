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
#include "main.h"
#include "ini.h"
#include "ui.h"
#include "arim_proto.h"

#define MAX_FEC_ROW_SIZE  256
#define FEC_WIN_SCROLL_LEGEND  "Scrolling: UP DOWN PAGEUP PAGEDOWN HOME END, 'q' to quit. "

const char *fecmenu[] = {
    " FEC Control Menu - press [key] to select",
    "             or 'q' to quit",
    " ----------------------------------------",
    " FSK Modes:              PSK Modes:",
    " [a] 4FSK.200.50S        [f] 4PSK.200.100S",
    " [b] 4FSK.500.100S       [g] 4PSK.200.100",
    " [c] 4FSK.500.100        [h] 8PSK.200.100",
    " [d] 4FSK.2000.600S      [i] 4PSK.500.100",
    " [e] 4FSK.2000.600       [j] 8PSK.500.100",
    "                         [k] 4PSK.1000.100",
    " QAM Modes:              [l] 8PSK.1000.100",
    " [w] 16QAM.200.100       [m] 4PSK.2000.100",
    " [x] 16QAM.500.100       [n] 8PSK.2000.100",
    " [y] 16QAM.1000.100",
    " [z] 16QAM.2000.100",
    "",
    " FEC Repeats: [0] None [1] One [2] Two [3] Three",
    "    (Repeats allow better copy in marginal",
    "     conditions but reduce net throughput)",
    "",
    " Key to FEC modes:",
    "   The first component is the modulation type",
    "   e.g. 4FSK, 8PSK. The second is the bandwidth",
    "   in Hz at the -26 dB points. The third is the",
    "   baud rate. A trailing 'S' indicates a mode",
    "   with a shortened frame. NOTE: baud rates",
    "   over 300 are not currently permitted in the",
    "   United States below 29 MHz.",
    0,
};

char *feccmds[] = {
    "aFECMODE 4FSK.200.50S",
    "bFECMODE 4FSK.500.100S",
    "cFECMODE 4FSK.500.100",
    "dFECMODE 4FSK.2000.600S",
    "eFECMODE 4FSK.2000.600",
    "fFECMODE 4PSK.200.100S",
    "gFECMODE 4PSK.200.100",
    "hFECMODE 8PSK.200.100",
    "iFECMODE 4PSK.500.100",
    "jFECMODE 8PSK.500.100",
    "kFECMODE 4PSK.1000.100",
    "lFECMODE 8PSK.1000.100",
    "mFECMODE 4PSK.2000.100",
    "nFECMODE 8PSK.2000.100",
    "wFECMODE 16QAM.200.100",
    "xFECMODE 16QAM.500.100",
    "yFECMODE 16QAM.1000.100",
    "zFECMODE 16QAM.2000.100",
    "0FECREPEATS 0",
    "1FECREPEATS 1",
    "2FECREPEATS 2",
    "3FECREPEATS 3",
    0,
};

char *ui_get_feccmd(int key)
{
    int i;

    i = 0;
    while (feccmds[i]) {
        if (feccmds[i][0] == key)
           return &(feccmds[i][1]);
        ++i;
    };
    return NULL;
}

void ui_print_fec_menu_title()
{
    int center, start;

    center = (tnc_data_box_w / 2) - 1;
    start = center - 9;
    if (start < 1)
        start = 1;
    mvwprintw(tnc_data_box, tnc_data_box_h - 1, start, " FEC CONTROL MENU ");
    wrefresh(tnc_data_box);
}

void ui_show_fec_menu()
{
    WINDOW *fecmenu_win, *prev_win;
    char linebuf[MAX_FEC_ROW_SIZE];
    int i, state, cmd, max_cols, max_fecmenu_rows, max_fecmenu_lines;
    int cur, top = 0, quit = 0;
    char *p;

    fecmenu_win = newwin(tnc_data_box_h - 2, tnc_data_box_w - 2,
                                 tnc_data_box_y + 1, tnc_data_box_x + 1);
    if (!fecmenu_win) {
        ui_print_status("Help: failed to create FEC Ctl window", 1);
        return;
    }
    if (color_code)
        wbkgd(fecmenu_win, COLOR_PAIR(7));
    prev_win = ui_set_active_win(fecmenu_win);
    max_fecmenu_rows = tnc_data_box_h - 2;
    i = 0;
    while (fecmenu[i])
        ++i;
    max_fecmenu_lines = i;
    max_cols = (tnc_data_box_w - 4) + 1;
    if (max_cols > sizeof(linebuf))
        max_cols = sizeof(linebuf);
    cur = top;
    for (i = 0; i < max_fecmenu_rows && cur < max_fecmenu_lines; i++) {
        snprintf(linebuf, max_cols, "%s", fecmenu[cur++]);
        mvwprintw(fecmenu_win, i, 1, linebuf);
    }
    if (show_titles)
        ui_print_fec_menu_title();
    wrefresh(fecmenu_win);
    status_timer = 1;
    while (!quit) {
        if (status_timer && --status_timer == 0)
            ui_print_status(FEC_WIN_SCROLL_LEGEND, 0);
        cmd = getch();
        switch (cmd) {
        case KEY_HOME:
            top = 0;
            cur = top;
            wclear(fecmenu_win);
            for (i = 0; i < max_fecmenu_rows && cur < max_fecmenu_lines; i++) {
                snprintf(linebuf, max_cols, "%s", fecmenu[cur++]);
                mvwprintw(fecmenu_win, i, 1, linebuf);
            }
            wrefresh(fecmenu_win);
            break;
        case KEY_END:
            if (max_fecmenu_lines < max_fecmenu_rows)
                break;
            top = max_fecmenu_lines - max_fecmenu_rows;
            cur = top;
            wclear(fecmenu_win);
            for (i = 0; i < max_fecmenu_rows && cur < max_fecmenu_lines; i++) {
                snprintf(linebuf, max_cols, "%s", fecmenu[cur++]);
                mvwprintw(fecmenu_win, i, 1, linebuf);
            }
            wrefresh(fecmenu_win);
            break;
        case ' ':
        case KEY_NPAGE:
            top += max_fecmenu_rows;
            if (top > max_fecmenu_lines - 1)
                top = max_fecmenu_lines - 1;
            cur = top;
            wclear(fecmenu_win);
            for (i = 0; i < max_fecmenu_rows && cur < max_fecmenu_lines; i++) {
                snprintf(linebuf, max_cols, "%s", fecmenu[cur++]);
                mvwprintw(fecmenu_win, i, 1, linebuf);
            }
            wrefresh(fecmenu_win);
            break;
        case '-':
        case KEY_PPAGE:
            top -= max_fecmenu_rows;
            if (top < 0)
                top = 0;
            cur = top;
            wclear(fecmenu_win);
            for (i = 0; i < max_fecmenu_rows && cur < max_fecmenu_lines; i++) {
                snprintf(linebuf, max_cols, "%s", fecmenu[cur++]);
                mvwprintw(fecmenu_win, i, 1, linebuf);
            }
            wrefresh(fecmenu_win);
            break;
        case KEY_UP:
            top -= 1;
            if (top < 0)
                top = 0;
            cur = top;
            wclear(fecmenu_win);
            for (i = 0; i < max_fecmenu_rows && cur < max_fecmenu_lines; i++) {
                snprintf(linebuf, max_cols, "%s", fecmenu[cur++]);
                mvwprintw(fecmenu_win, i, 1, linebuf);
            }
            wrefresh(fecmenu_win);
            break;
        case '\n':
        case KEY_DOWN:
            top += 1;
            if (top > max_fecmenu_lines - 1)
                top = max_fecmenu_lines - 1;
            cur = top;
            wclear(fecmenu_win);
            for (i = 0; i < max_fecmenu_rows && cur < max_fecmenu_lines; i++) {
                snprintf(linebuf, max_cols, "%s", fecmenu[cur++]);
                mvwprintw(fecmenu_win, i, 1, linebuf);
            }
            wrefresh(fecmenu_win);
            break;
        case 'q':
        case 'Q':
            delwin(fecmenu_win);
            touchwin(tnc_data_box);
            wrefresh(tnc_data_box);
            quit = 1;
            break;
        case 27:
            ui_on_cancel();
            break;
        default:
            state = arim_get_state();
            if (state != ST_ARQ_CONNECTED &&
                state != ST_ARQ_IN_CONNECT_WAIT &&
                state != ST_ARQ_OUT_CONNECT_WAIT) {
                p = ui_get_feccmd(cmd);
                if (p) {
                    ui_queue_cmd_out(p);
                    ui_print_status(p, 1);
                }
            }
            ui_print_cmd_in();
            ui_print_recents();
            ui_print_ptable();
            ui_print_heard_list();
            ui_check_status_dirty();
            break;
        }
        if (g_win_changed)
            quit = 1;
        usleep(100000);
    }
    ui_set_active_win(prev_win);
    if (show_titles)
        ui_print_data_title();
}

