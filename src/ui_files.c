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
#include <curses.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include "main.h"
#include "arim_message.h"
#include "arim_proto.h"
#include "arim_arq.h"
#include "arim_arq_files.h"
#include "ini.h"
#include "ui.h"
#include "ui_dialog.h"
#include "log.h"
#include "util.h"

int ui_check_files_dir(const char *path)
{
    char allowed_path[MAX_DIR_PATH_SIZE], test_path[MAX_DIR_PATH_SIZE];
    int i, wildcard = 0;
    size_t len;

    snprintf(test_path, sizeof(test_path), "%s", path);
    /* trim trailing '/' if present */
    len = strlen(test_path);
    if (test_path[len - 1] == '/')
        test_path[len - 1] = '\0';
    /* iterate over allowed directory paths and check for a match */
    for (i = 0; i < g_arim_settings.add_files_dir_cnt; i++) {
        snprintf(allowed_path, sizeof(allowed_path), "%s/%s",
                g_arim_settings.files_dir, g_arim_settings.add_files_dir[i]);
        len = strlen(allowed_path);
        if (len) {
            /* check for wildcard path spec */
            if (allowed_path[len - 1] == '*') {
                if (len > 1 && allowed_path[len - 2] == '/') {
                    allowed_path[len - 1] = '\0';
                    --len;
                    wildcard = 1;
                } else {
                    /* bad path spec */
                    continue;
                }
            }
        } else {
            continue; /* empty */
        }
        if (wildcard) {
            /* check for match with the stem of the path being tested */
            if (!strncmp(allowed_path, test_path, len))
                return 1;
            /* remove trailing '/' for exact match test that follows */
            allowed_path[len - 1] = '\0';
        }
        /* check for exact match with the path being tested */
        if (!strcmp(allowed_path, test_path))
            return 1;
    }
    return 0;
}

int ui_send_file(char *msgbuffer, size_t msgbufsize,
                    const char *fn, const char *to_call)
{
    FILE *fp;
    size_t len;

    if (!arim_is_idle())
        return 0;
    fp = fopen(fn, "r");
    if (fp == NULL)
        return 0;
    len = fread(msgbuffer, 1, msgbufsize, fp);
    fclose(fp);
    msgbuffer[len] = '\0';
    /* send the file */
    if (strlen(to_call) > 0)
        arim_send_msg(msgbuffer, to_call);
    return 1;
}

int ui_get_dyn_file(const char *fn, const char *cmd,
                        char *filebuf, size_t filebufsize)
{
    FILE *fp;
    size_t len, max, cnt = 0;
    char cmd_line[MAX_CMD_SIZE];

    if (atoi(g_arim_settings.max_file_size) <= 0) {
        snprintf(filebuf, filebufsize, "File: file sharing disabled.\n");
        return 0;
    }
    max = atoi(g_arim_settings.max_file_size);
    pthread_mutex_lock(&mutex_df_error_log);
    snprintf(cmd_line, sizeof(cmd_line), "%s 2>> %s", cmd, g_df_error_fn);
    pthread_mutex_unlock(&mutex_df_error_log);
    fp = popen(cmd_line, "r");
    if (!fp) {
        snprintf(filebuf, filebufsize, "File: %s read failed.\n", fn);
        return 0;
    }
    snprintf(filebuf, filebufsize, "File: %s\n\n", fn);
    cnt = strlen(filebuf);
    if (max > filebufsize - cnt)
        max = filebufsize - cnt;
    len = fread(filebuf + cnt, 1, max - 1, fp);
    pclose(fp);
    if (len == 0) {
        snprintf(filebuf, filebufsize, "File: %s read failed.\n", fn);
        return 0;
    }
    cnt += len;
    filebuf[cnt] = '\0';
    return 1;
}

int ui_get_file(const char *fn, char *filebuf, size_t filebufsize)
{
    FILE *fp;
    char fpath[MAX_DIR_PATH_SIZE], dpath[MAX_DIR_PATH_SIZE];
    size_t len, cnt = 0, max;

    if (atoi(g_arim_settings.max_file_size) <= 0) {
        snprintf(filebuf, filebufsize, "File: file sharing disabled.\n");
        return 0;
    }
    /* check if access to this dir is allowed */
    snprintf(fpath, sizeof(fpath), "%s/%s", g_arim_settings.files_dir, fn);
    snprintf(dpath, sizeof(dpath), "%s", dirname(fpath));
    if (strcmp(g_arim_settings.files_dir, dpath)) {
        /* if not the base shared files directory
           path, check to see if it's allowed */
        if (!ui_check_files_dir(dpath)) {
            snprintf(filebuf, filebufsize, "File: %s not found.\n", fn);
            return 0;
        }
    }
    max = atoi(g_arim_settings.max_file_size);
    snprintf(fpath, sizeof(fpath), "%s/%s", g_arim_settings.files_dir, fn);
    if (strstr(fpath, "..")) {
        snprintf(filebuf, filebufsize, "File: illegal file name.\n");
        return 0;
    }
    fp = fopen(fpath, "r");
    if (fp == NULL) {
        snprintf(filebuf, filebufsize, "File: %s not found.\n", fn);
        return 0;
    }
    snprintf(filebuf, filebufsize, "File: %s\n\n", fn);
    cnt = strlen(filebuf);
    if (max > filebufsize - cnt)
        max = filebufsize - cnt;
    len = fread(filebuf + cnt, 1, max - 1, fp);
    cnt += len;
    fclose(fp);
    filebuf[cnt] = '\0';
    return 1;
}

void ui_print_file_reader_title(const char *path)
{
    char *p, title[MAX_DIR_PATH_SIZE], temp[MAX_DIR_PATH_SIZE];
    int center, start;
    size_t len, max_len;

    snprintf(temp, sizeof(temp), "%s", path);
    snprintf(title, sizeof(title), " READ FILE: %s ", basename(temp));
    len = strlen(title);
    /* abbreviate path if necessary to fit in line */
    max_len = tnc_cmd_box_w - 2;
    if (sizeof(title) < max_len)
        max_len = sizeof(title);
    p = temp;
    while (len > max_len && strlen(p) > 3) {
        p += 3;
        snprintf(title, sizeof(title), " READ FILE: ...%s ", p);
        len = strlen(title);
    }
    center = (tnc_cmd_box_w / 2) - 1;
    start = center - (len / 2);
    if (start < 1)
        start = 1;
    mvwprintw(tnc_cmd_box, tnc_cmd_box_h - 1, start, title);
    wrefresh(tnc_cmd_box);
}

int ui_read_file(const char *fn, int index)
{
    WINDOW *read_pad;
    FILE *fp;
    size_t i, len;
    int cmd, max_pad_rows = 0, top = 0, quit = 0;
    int max_read_rows, max_read_cols, min_read_rows, min_read_cols, num_read_rows;
    char filebuf[MIN_MSG_BUF_SIZE+1], status[MAX_STATUS_BAR_SIZE];

    fp = fopen(fn, "r");
    if (fp == NULL)
        return 0;
    len = fread(filebuf, 1, sizeof(filebuf), fp);
    fclose(fp);
    filebuf[len] = '\0';
    for (i = 0; i < len; i++) {
        if (filebuf[i] == '\n') {
            ++max_pad_rows;
            /* convert CRLF line endings */
            if (i && filebuf[i - 1] == '\r') {
                /* shift remaining text including terminating null */
                memmove(&filebuf[i - 1], &filebuf[i], len - i + 1);
                --i;
                --len;
            }
        }
    }
    min_read_rows = tnc_cmd_box_y + 1;
    max_read_rows = min_read_rows + tnc_cmd_box_h - 3;
    min_read_cols = tnc_cmd_box_x + 2;
    max_read_cols = min_read_cols + tnc_cmd_box_w - 4;
    num_read_rows = max_read_rows - min_read_rows;
    if (show_titles)
        ui_print_file_reader_title(fn);
    read_pad = newpad(max_pad_rows + num_read_rows, max_read_cols);
    if (!read_pad)
        return 0;
    waddstr(read_pad, filebuf);
    prefresh(read_pad, top, 0, min_read_rows, min_read_cols,
                 max_read_rows, max_read_cols);
    if (index == -1) /* special case, configuration file */
        snprintf(status, sizeof(status),
                "Config file: %d lines - use UP, DOWN keys to scroll, 'q' to quit",
                    max_pad_rows);
    else
        snprintf(status, sizeof(status),
                "File [%d]: %d lines - use UP, DOWN keys to scroll, 'q' to quit",
                    index, max_pad_rows);
    status_timer = 1;
    while (!quit) {
        if (status_timer && --status_timer == 0)
            ui_print_status(status, 0);
        cmd = getch();
        switch (cmd) {
        case KEY_HOME:
            top = 0;
            prefresh(read_pad, top, 0, min_read_rows, min_read_cols,
                        max_read_rows, max_read_cols);
            break;
        case KEY_END:
            if (max_pad_rows < num_read_rows)
                break;
            top = max_pad_rows - num_read_rows;
            prefresh(read_pad, top, 0, min_read_rows, min_read_cols,
                        max_read_rows, max_read_cols);
            break;
        case ' ':
        case KEY_NPAGE:
            top += num_read_rows;
            if (top > max_pad_rows - 1)
                top = max_pad_rows - 1;
            prefresh(read_pad, top, 0, min_read_rows, min_read_cols,
                        max_read_rows, max_read_cols);
            break;
        case '-':
        case KEY_PPAGE:
            top -= num_read_rows;
            if (top < 0)
                top = 0;
            prefresh(read_pad, top, 0, min_read_rows, min_read_cols,
                        max_read_rows, max_read_cols);
            break;
        case KEY_UP:
            top -= 1;
            if (top < 0)
                top = 0;
            prefresh(read_pad, top, 0, min_read_rows, min_read_cols,
                        max_read_rows, max_read_cols);
            break;
        case '\n':
        case KEY_DOWN:
            top += 1;
            if (top > max_pad_rows - 1)
                top = max_pad_rows - 1;
            prefresh(read_pad, top, 0, min_read_rows, min_read_cols,
                        max_read_rows, max_read_cols);
            break;
        case 't':
        case 'T':
            if (last_time_heard == LT_HEARD_ELAPSED) {
                last_time_heard = LT_HEARD_CLOCK;
                ui_print_status("Showing clock time in Heard List, press 't' to toggle", 1);
            } else {
                last_time_heard = LT_HEARD_ELAPSED;
                ui_print_status("Showing elapsed time in Heard List, press 't' to toggle", 1);
            }
            ui_refresh_heard_list();
            break;
        case 'n':
        case 'N':
            ui_clear_new_ctrs();
            break;
        case 'q':
        case 'Q':
            delwin(read_pad);
            touchwin(tnc_cmd_box);
            wrefresh(tnc_cmd_box);
            status_timer = 1;
            quit = 1;
            break;
        case 27:
            ui_on_cancel();
            break;
        default:
            ui_print_heard_list();
            ui_check_status_dirty();
            break;
        }
        if (g_win_changed)
            quit = 1;
        usleep(100000);
    }
    if (show_titles)
        ui_print_cmd_title();
    return 1;
}

int ui_get_file_list(const char *basedir, const char *dir,
                     char *listbuf, size_t listbufsize)
{
    DIR *dirp;
    struct dirent *dent;
    struct stat stats;
    char *p, linebuf[MAX_DIR_LINE_SIZE];
    char fn[MAX_DIR_PATH_SIZE], path[MAX_DIR_PATH_SIZE];
    size_t i, len, max_file_size, cnt = 0;

    if (atoi(g_arim_settings.max_file_size) <= 0) {
        snprintf(listbuf, listbufsize, "File list: file sharing disabled.\n");
        return 1;
    }
    /* dir may be null if flist query has no argument */
    if (dir) {
        /* check if access to this dir is allowed */
        snprintf(path, sizeof(path), "%s/%s", basedir, dir);
        if (!ui_check_files_dir(path)) {
            snprintf(listbuf, listbufsize, "File list: directory not found.\n");
            return 0;
        }
    } else {
        snprintf(path, sizeof(path), "%s", basedir);
    }
    dirp = opendir(path);
    if (!dirp) {
        snprintf(listbuf, listbufsize, "File list: cannot open directory.\n");
        return 0;
    }
    if (dir)
        snprintf(listbuf, listbufsize, "File list: %s\n", dir);
    else
        snprintf(listbuf, listbufsize, "File list:\n");
    cnt += strlen(listbuf);
    i = 0;
    max_file_size = atoi(g_arim_settings.max_file_size);
    dent = readdir(dirp);
    while (dent) {
        snprintf(fn, sizeof(fn), "%s/%s", path, dent->d_name);
        if (stat(fn, &stats) == 0) {
            if (!S_ISDIR(stats.st_mode)) {
                if (stats.st_size <= max_file_size) {
                    snprintf(linebuf, sizeof(linebuf),
                        "%20s%8jd\n", dent->d_name, (intmax_t)stats.st_size);
                    len = strlen(linebuf);
                    if ((cnt + len) < listbufsize) {
                        strncat(listbuf, linebuf, listbufsize - cnt - 1);
                        cnt += len;
                    }
                }
            } else if (strcmp(dent->d_name, "..") && strcmp(dent->d_name, ".")) {
                if (ui_check_files_dir(fn)) {
                    snprintf(linebuf, sizeof(linebuf), "%20s%8s\n", dent->d_name, "DIR");
                    len = strlen(linebuf);
                    if ((cnt + len) < listbufsize) {
                        strncat(listbuf, linebuf, listbufsize - cnt - 1);
                        cnt += len;
                    }
                }
            }
        }
        dent = readdir(dirp);
    }
    closedir(dirp);
    /* list dynamic files only for shared files root dir */
    if (!dir) {
        for (i = 0; i < g_arim_settings.dyn_files_cnt; i++) {
            snprintf(fn, sizeof(fn), "%s", g_arim_settings.dyn_files[i]);
            p = strstr(fn, ":");
            if (p) {
                *p = '\0';
                snprintf(linebuf, sizeof(linebuf), "%20s%8s\n", fn, "DYN");
                len = strlen(linebuf);
                if ((cnt + len) < listbufsize) {
                    strncat(listbuf, linebuf, listbufsize - cnt - 1);
                    cnt += len;
                }
            }
        }
    }
    if ((cnt + 1) < listbufsize) {
        listbuf[cnt] = '\n';
        ++cnt;
    }
    listbuf[cnt] = '\0';
    return 1;
}

void ui_print_file_list_title(const char *path)
{
    char *p, title[MAX_DIR_PATH_SIZE], temp[MAX_DIR_PATH_SIZE];
    int center, start;
    size_t len, max_len;

    snprintf(temp, sizeof(temp), "%s", path);
    snprintf(title, sizeof(title), " LIST FILES: %s ", temp);
    len = strlen(title);
    max_len = tnc_data_box_w - 2;
    if (sizeof(title) < max_len)
        max_len = sizeof(title);
    p = temp;
    while (len > max_len && *p) {
        /* abbreviate directory path if it won't fit */
        while (*p && *p != '/')
            ++p;
        if (*p)
            ++p;
        snprintf(title, sizeof(title), " LIST FILES: .../%s ", p);
        len = strlen(title);
    }
    center = (tnc_data_box_w / 2) - 1;
    start = center - (len / 2);
    if (start < 1)
        start = 1;
    mvwhline(tnc_data_box, tnc_data_box_h - 1, 1, 0, tnc_data_box_w - 2);
    mvwprintw(tnc_data_box, tnc_data_box_h - 1, start, title);
    wrefresh(tnc_data_box);
}

int ui_files_get_line(char *cmd_line, size_t max_len)
{
    size_t len = 0, cur = 0;
    int ch,  quit = 0;

    wmove(prompt_win, prompt_row, prompt_col);
    wclrtoeol(prompt_win);
    wrefresh(prompt_win);

    curs_set(1);
    keypad(prompt_win, TRUE);
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
            ui_print_heard_list();
            ui_check_status_dirty();
            wmove(prompt_win, prompt_row, prompt_col + cur);
            curs_set(1);
            break;
        case '\n':
            quit = 1;
            break;
        case 27:
            ui_on_cancel();
            break;
        case 127: /* DEL */
        case KEY_BACKSPACE:
            if (len && cur) {
                memmove(cmd_line + cur - 1, cmd_line + cur, max_len - cur);
                --len;
                --cur;
                mvwdelch(prompt_win, prompt_row, prompt_col + cur);
            }
            break;
        case 4: /* CTRL-D */
        case KEY_DC:
            if (len && cur < len) {
                memmove(cmd_line + cur, cmd_line + cur + 1, max_len - cur);
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
                memmove(cmd_line, cmd_line + cur, max_len - cur);
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
        default:
            if (isprint(ch) && len < max_len) {
                if (cur == len) {
                    cmd_line[len++] = ch;
                    cmd_line[len] = '\0';
                    waddch(prompt_win, ch);
                } else {
                    memmove(cmd_line + cur + 1, cmd_line + cur, max_len - cur);
                    cmd_line[cur] = ch;
                    ++len;
                    mvwinsch(prompt_win, prompt_row, prompt_col + cur, ch);
                }
                ++cur;
            }
        }
    }
    keypad(prompt_win, FALSE);
    curs_set(0);
    wmove(prompt_win, prompt_row, prompt_col);
    wclrtoeol(prompt_win);
    wrefresh(prompt_win);
    return (len != 0);
}

void ui_list_files(const char *dir)
{
    WINDOW *dir_win, *prev_win;
    DIR *dirp;
    struct dirent *dent;
    struct stat stats;
    char linebuf[MAX_DIR_LINE_SIZE+1], msgbuffer[MIN_MSG_BUF_SIZE];
    char path[MAX_DIR_LIST_LEN+1][MAX_DIR_PATH_SIZE];
    char list[MAX_DIR_LIST_LEN+1][MAX_DIR_LINE_SIZE];
    char fn[MAX_DIR_PATH_SIZE], dpath[MAX_DIR_PATH_SIZE];
    char temp[MAX_DIR_PATH_SIZE], to_call[MAX_CALLSIGN_SIZE];
    char *p, *destdir, timestamp[MAX_TIMESTAMP_SIZE];
    int i, max_cols, max_dir_rows, max_dir_lines, max_len;
    int cmd, cur, top, quit = 0, level = 0, zoption = 0;
    size_t len;

    dir_win = newwin(tnc_data_box_h - 2, tnc_data_box_w - 2,
                                 tnc_data_box_y + 1, tnc_data_box_x + 1);
    if (!dir_win) {
        ui_print_status("List files: failed to create list window", 1);
        return;
    }
    max_dir_rows = tnc_data_box_h - 2;
    max_cols = (tnc_data_box_w - 4) + 1;
    if (max_cols > MAX_DIR_LINE_SIZE)
        max_cols = MAX_DIR_LINE_SIZE;
    prev_win = ui_set_active_win(dir_win);
    snprintf(dpath, sizeof(dpath), "%s", dir);

restart:
    wclear(dir_win);
    memset(&list, 0, sizeof(list));
    memset(&path, 0, sizeof(path));
    dirp = opendir(dpath);
    if (!dirp) {
        ui_print_status("List files: failed to open shared files directory", 1);
        return;
    }
    if (level)
        i = 1;
    else
        i = 0;
    dent = readdir(dirp);
    while (dent) {
        /* stat the file */
        snprintf(temp, sizeof(temp), "%s/%s", dpath, dent->d_name);
        if (stat(temp, &stats) == 0) {
            /* calculate max file name length, display line format is:
               ----- ----------------------- --------- -----------------
               nbr=5     name (variable)      size=9        time=17
               ----- ----------------------- --------- -----------------
               [  1] test.txt                      242 Aug 28 02:35 2017
               ----- ----------------------- --------- -----------------
            */
            max_len = max_cols - (5 + 1 + 0 + 1 + 9 + 1 + 17) - 1;
            snprintf(fn, sizeof(fn), "%s", dent->d_name);
            len = strlen(fn);
            p = temp;
            /* abbreviate file name if it won't fit in line */
            while (len > max_len && strlen(p) > 3) {
                p += 3;
                snprintf(fn, sizeof(fn), "...%s", p);
                len = strlen(fn);
            }
            /* store entry into list */
            if (S_ISDIR(stats.st_mode)) {
                if (strcmp(dent->d_name, ".")) {
                    if (strcmp(dent->d_name, "..")) {
                        snprintf(list[i], max_cols + 1, "D[%3d] %-*s %9s %17s",
                                 i + 1, max_len, fn, "DIRECTORY",
                                    util_file_timestamp(stats.st_mtime,
                                        timestamp, sizeof(timestamp)));
                        /* store in path list */
                        snprintf(path[i], sizeof(path[0]), "%s/%s", dpath, dent->d_name);
                        ++i;
                    } else if (level) {
                        /* put parent directory at top of listing */
                        snprintf(list[0], max_cols + 1, "D[%3d] %-*s %9s %17s",
                                 1, max_len, fn, "DIRECTORY",
                                    util_file_timestamp(stats.st_mtime,
                                        timestamp, sizeof(timestamp)));
                        /* store in path list */
                        snprintf(path[0], sizeof(path[0]), "%s/%s", dpath, dent->d_name);
                    }
                }
            } else {
                snprintf(list[i], max_cols + 1, "F[%3d] %-*s %9jd %17s",
                         i + 1, max_len, fn, (intmax_t)stats.st_size,
                            util_file_timestamp(stats.st_mtime,
                                timestamp, sizeof(timestamp)));
                /* store in path list */
                snprintf(path[i], sizeof(path[0]), "%s/%s", dpath, dent->d_name);
                ++i;
            }
            if (i == MAX_DIR_LIST_LEN) {
                snprintf(temp, sizeof(temp),
                    "\tToo many files to list;\n"
                        "\tonly the first %d are shown.\n \n\t[O]k", i);
                cmd = ui_show_dialog(temp, "oO \n");
                break;
            }
        }
        dent = readdir(dirp);
    }
    closedir(dirp);
    max_dir_lines = i;
    cur = top = 0;
    for (i = 0; i < max_dir_rows && cur < max_dir_lines; i++) {
        mvwprintw(dir_win, i, 1, &(list[cur][1]));
        ++cur;
    }
    wrefresh(dir_win);
    if (show_titles)
        ui_print_file_list_title(dpath);
    status_timer = 1;
    while (!quit) {
        if (status_timer && --status_timer == 0) {
            if (arim_is_arq_state())
                ui_print_status("<SP> for prompt: 'cd n' ch dir, 'rf n' read, "
                    "'sf [-z] n [dir]' send, 'ri' " DEFAULT_INI_FNAME ", 'q' quit", 0);
            else
                ui_print_status("<SP> for prompt: 'cd n' ch dir, 'rf n' read, "
                    "'sf n call' send, 'ri' " DEFAULT_INI_FNAME ", 'q' quit", 0);
        }
        cmd = getch();
        switch (cmd) {
        case ' ':
            memset(linebuf, 0, sizeof(linebuf));
            ui_files_get_line(linebuf, max_cols - 1);
            /* process the command */
            if (linebuf[0] == ':') {
                if (g_tnc_attached)
                    ui_queue_data_out(&linebuf[1]);
                break;
            } else if (linebuf[0] == '!') {
                if (g_tnc_attached)
                    ui_queue_cmd_out(&linebuf[1]);
                break;
            } else if (linebuf[0] == 'q') {
                quit = 1;
            } else {
                p = strtok(linebuf, " \t");
                if (!p)
                    break;
                if (!strncasecmp(p, "rf", 2)) {
                    p = strtok(NULL, " \t");
                    if (!p || !(i = atoi(p))) {
                        ui_print_status("Read file: invalid file number", 1);
                        break;
                    }
                    --i;
                    if (i >= 0 && i <= max_dir_lines) {
                        if (list[i][0] == 'F') {
                            /* ordinary data file, try to read it */
                            if (!ui_read_file(path[i], (int)(i + 1)))
                                ui_print_status("Read file: cannot read file", 1);
                        } else {
                            ui_print_status("Read file: cannot read directory", 1);
                        }
                    } else {
                        ui_print_status("Read file: invalid file number", 1);
                    }
                    if (show_recents)
                        ui_refresh_recents();
                } else if (!strncasecmp(p, "cd", 2)) {
                    p = strtok(NULL, " \t");
                    if (!p || !(i = atoi(p))) {
                        ui_print_status("Change directory: invalid directory number", 1);
                        break;
                    }
                    --i;
                    if (i >= 0 && i <= max_dir_lines) {
                        if (list[i][0] == 'D') {
                            /* directory, try to open and list it */
                            snprintf(fn, sizeof(fn), "%s", path[i]);
                            p = strstr(fn, "/..");
                            if (p) {
                                /* go up one level */
                                --p;
                                while (p > fn && *p != '/')
                                    --p;
                                *p = '\0';
                                snprintf(dpath, sizeof(dpath), "%s", fn);
                                --level;
                                goto restart;
                            }
                            snprintf(dpath, sizeof(dpath), "%s", fn);
                            ++level;
                            goto restart;
                        } else {
                            ui_print_status("Change directory: not a directory", 1);
                        }
                    } else {
                        ui_print_status("Change directory: invalid directory number", 1);
                    }
                    if (show_recents)
                        ui_refresh_recents();
                } else if (!strncasecmp(p, "ri", 2)) {
                    if (!ui_read_file(g_config_fname, -1))
                        ui_print_status("Read file: cannot open configuration file", 1);
                    if (show_recents)
                        ui_refresh_recents();
                } else if (!strncasecmp(p, "sf", 2)) {
                    if (!g_tnc_attached) {
                        ui_print_status("Send file: cannot send, no TNC attached", 1);
                        break;
                    }
                    zoption = 0;
                    p = strtok(NULL, " >\t");
                    if (p && !strcmp(p, "-z")) {
                        if (!arim_is_arq_state()) {
                            ui_print_status("Send file: -z option not supported in FEC mode", 1);
                            break;
                        }
                        p = strtok(NULL, " >\t");
                        zoption = 1;
                    }
                    if (!p || !(i = atoi(p))) {
                        ui_print_status("Send file: invalid file number", 1);
                        break;
                    }
                    --i;
                    if (i >= 0 && i <= max_dir_lines) {
                        if (list[i][0] == 'F') {
                            if (arim_is_arq_state()) {
                                if (arim_get_state() == ST_ARQ_CONNECTED) {
                                    /* get destination directory path */
                                    destdir = strtok(NULL, "\0");
                                    if (destdir) {
                                        snprintf(temp, sizeof(temp), "%s", destdir);
                                        destdir = temp;
                                    }
                                    /* initiate ARQ file upload */
                                    p = path[i] + strlen(g_arim_settings.files_dir) + 1;
                                    snprintf(fn, sizeof(fn), "%s", p);
                                    arim_arq_files_on_loc_fput(fn, destdir, zoption);
                                } else {
                                    ui_print_status("Send file: cannot send, TNC busy", 1);
                                }
                            } else {
                                p = strtok(NULL, " \t");
                                if (!p || !ini_validate_mycall(p)) {
                                    ui_print_status("Send file: invalid callsign", 1);
                                    break;
                                }
                                snprintf(to_call, sizeof(to_call), "%s", p);
                                snprintf(fn, sizeof(fn), "%s", path[i]);
                                if (ui_send_file(msgbuffer, sizeof(msgbuffer), fn, to_call))
                                    ui_print_status("ARIM Busy: sending file", 1);
                                else
                                    ui_print_status("Send file: cannot send, TNC busy", 1);
                            }
                        } else {
                            ui_print_status("Send file: cannot send directory", 1);
                        }
                    } else {
                        ui_print_status("Send file: invalid file number", 1);
                    }
                } else {
                    ui_print_status("List files: command not available in this view", 1);
                }
            }
            break;
        case 't':
        case 'T':
            if (last_time_heard == LT_HEARD_ELAPSED) {
                last_time_heard = LT_HEARD_CLOCK;
                ui_print_status("Showing clock time in Heard List, press 't' to toggle", 1);
            } else {
                last_time_heard = LT_HEARD_ELAPSED;
                ui_print_status("Showing elapsed time in Heard List, press 't' to toggle", 1);
            }
            ui_refresh_heard_list();
            if (show_ptable)
                ui_refresh_ptable();
            break;
        case 'r':
        case 'R':
            if (show_ptable)
                break;
            if (!show_recents) {
                show_recents = 1;
                ui_print_status("Showing Recents, press 'r' to toggle", 1);
            } else {
                show_recents = 0;
                ui_print_status("Showing TNC cmds, press 'r' to toggle", 1);
            }
        case 'p':
        case 'P':
            if (show_recents)
                break;
            if (!show_ptable) {
                show_ptable = 1;
                ui_print_status("Showing Pings, <SP> 'pi n' to ping, press 'p' to toggle", 1);
            } else {
                show_ptable = 0;
                ui_print_status("Showing TNC cmds, press 'p' to toggle", 1);
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
        case KEY_HOME:
            top = 0;
            cur = top;
            wclear(dir_win);
            for (i = 0; i < max_dir_rows && cur < max_dir_lines; i++) {
                mvwprintw(dir_win, i, 1, &(list[cur][1]));
                ++cur;
            }
            wrefresh(dir_win);
            break;
        case KEY_END:
            if (max_dir_lines < max_dir_rows)
                break;
            top = max_dir_lines - max_dir_rows;
            cur = top;
            wclear(dir_win);
            for (i = 0; i < max_dir_rows && cur < max_dir_lines; i++) {
                mvwprintw(dir_win, i, 1, &(list[cur][1]));
                ++cur;
            }
            wrefresh(dir_win);
            break;
        case KEY_NPAGE:
            top += max_dir_rows;
            if (top > max_dir_lines - 1)
                top = max_dir_lines - 1;
            cur = top;
            wclear(dir_win);
            for (i = 0; i < max_dir_rows && cur < max_dir_lines; i++) {
                mvwprintw(dir_win, i, 1, &(list[cur][1]));
                ++cur;
            }
            wrefresh(dir_win);
            break;
        case '-':
        case KEY_PPAGE:
            top -= max_dir_rows;
            if (top < 0)
                top = 0;
            cur = top;
            wclear(dir_win);
            for (i = 0; i < max_dir_rows && cur < max_dir_lines; i++) {
                mvwprintw(dir_win, i, 1, &(list[cur][1]));
                ++cur;
            }
            wrefresh(dir_win);
            break;
        case KEY_UP:
            top -= 1;
            if (top < 0)
                top = 0;
            cur = top;
            wclear(dir_win);
            for (i = 0; i < max_dir_rows && cur < max_dir_lines; i++) {
                mvwprintw(dir_win, i, 1, &(list[cur][1]));
                ++cur;
            }
            wrefresh(dir_win);
            break;
        case '\n':
        case KEY_DOWN:
            top += 1;
            if (top > max_dir_lines - 1)
                top = max_dir_lines - 1;
            cur = top;
            wclear(dir_win);
            for (i = 0; i < max_dir_rows && cur < max_dir_lines; i++) {
                mvwprintw(dir_win, i, 1, &(list[cur][1]));
                ++cur;
            }
            wrefresh(dir_win);
            break;
        case 'n':
        case 'N':
            ui_clear_new_ctrs();
            break;
        case 'q':
        case 'Q':
            quit = 1;
            break;
        case 24: /* CTRL-X */
            if (arim_is_arq_state()) {
                cmd = ui_show_dialog("\tAre you sure\n\tyou want to disconnect?\n \n\t[Y]es   [N]o", "yYnN");
                if (cmd == 'y' || cmd == 'Y')
                    arim_arq_send_disconn_req();
            }
            break;
        case 27:
            ui_on_cancel();
            break;
        default:
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
    delwin(dir_win);
    ui_set_active_win(prev_win);
    touchwin(tnc_data_box);
    wrefresh(tnc_data_box);
    if (show_titles)
        ui_print_data_title();
    status_timer = 1;
}

void ui_list_shared_files() {
    ui_list_files(g_arim_settings.files_dir);
}

