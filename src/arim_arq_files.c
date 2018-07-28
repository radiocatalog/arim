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
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include "main.h"
#include "ini.h"
#include "bufq.h"
#include "arim_proto.h"
#include "ui.h"
#include "ui_files.h"
#include "ui_tnc_data_win.h"
#include "util.h"
#include "ui_dialog.h"
#include "log.h"
#include "zlib.h"
#include "datathread.h"
#include "arim_arq.h"
#include "arim_arq_auth.h"

static int zoption;
static FILEQUEUEITEM file_in;
static FILEQUEUEITEM file_out;
static size_t file_in_cnt, file_out_cnt;

int arim_arq_files_send_flist(const char *dir)
{
    char linebuf[MAX_LOG_LINE_SIZE], databuf[MIN_DATA_BUF_SIZE];
    size_t max, len;
    int numch, result;
    z_stream zs;
    char zbuffer[MIN_DATA_BUF_SIZE];
    int zret;

    max = atoi(g_arim_settings.max_file_size);
    if (max <= 0) {
        snprintf(linebuf, sizeof(linebuf), "/ERROR File sharing disabled");
        arim_arq_send_remote(linebuf);
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File listing upload failed, file sharing disabled");
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        return 0;
    }
    if (dir) {
        if (strstr(dir, "..")) {
            /* prevent directory traversal */
            snprintf(linebuf, sizeof(linebuf), "/ERROR Bad file directory path");
            arim_arq_send_remote(linebuf);
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File listing upload for %s failed, bad directory path", dir);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            return 0;
        }
    }
    /* now get the file listing */
    result = ui_get_file_list(g_arim_settings.files_dir, dir,
                              (char *)file_out.data, sizeof(file_out.data));
    if (!result) {
        snprintf(linebuf, sizeof(linebuf), "/ERROR Directory not found");
        arim_arq_send_remote(linebuf);
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File listing upload for %s failed, cannot open directory",
                             dir ? dir : "(root)");
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        return 0;
    }
    file_out.size = strlen((char *)file_out.data);
    snprintf(file_out.path, sizeof(file_out.path), "%s", dir ? dir : "");
    /* compress file listing if -z option invoked */
    if (zoption) {
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;
        zs.avail_in = file_out.size;
        zs.next_in = file_out.data;
        zs.avail_out = sizeof(zbuffer);
        zs.next_out = (Bytef *)zbuffer;
        zret = deflateInit(&zs, Z_BEST_COMPRESSION);
        if (zret == Z_OK) {
            zret = deflate(&zs, Z_FINISH);
            if (zret != Z_STREAM_END) {
                snprintf(linebuf, sizeof(linebuf), "/ERROR Cannot send file listing");
                arim_arq_send_remote(linebuf);
                numch = snprintf(linebuf, sizeof(linebuf),
                                 "ARQ: File listing upload %s failed, compression error",
                                     dir ? dir : "(root)");
                if (numch >= sizeof(linebuf))
                    ui_truncate_line(linebuf, sizeof(linebuf));
                ui_queue_debug_log(linebuf);
                return 0;
            }
            deflateEnd(&zs);
            memcpy(file_out.data, zbuffer, zs.total_out);
            file_out.size = zs.total_out;
        } else {
            snprintf(linebuf, sizeof(linebuf), "/ERROR Cannot send file listing");
            arim_arq_send_remote(linebuf);
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File listing upload %s failed, compression init error",
                                 dir ? dir : "(root)");
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            return 0;
        }
    }
    file_out.check = ccitt_crc16(file_out.data, file_out.size);
    /* enqueue command for TNC */
    if (dir)
        snprintf((char *)databuf, sizeof(databuf), "%s %s %zu %04X",
                 zoption ? "/FLPUT -z" : "/FLPUT",
                    file_out.path, file_out.size, file_out.check);
    else
        snprintf((char *)databuf, sizeof(databuf), "%s %zu %04X",
                 zoption ? "/FLPUT -z" : "/FLPUT", file_out.size, file_out.check);
    len = arim_arq_send_remote(databuf);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
             sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", len);
    pthread_mutex_unlock(&mutex_tnc_set);
    return 1;
}

int arim_arq_files_flist_on_send_cmd()
{
    char linebuf[MAX_LOG_LINE_SIZE];
    int numch;

    pthread_mutex_lock(&mutex_file_out);
    fileq_push(&g_file_out_q, &file_out);
    pthread_mutex_unlock(&mutex_file_out);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
             sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", file_out.size);
    pthread_mutex_unlock(&mutex_tnc_set);
    /* initialize cnt to prevent '0 of size' notification in monitor */
    file_out_cnt = file_out.size;
    numch = snprintf(linebuf, sizeof(linebuf),
                     "ARQ: File listing upload for %s buffered for sending",
                         *file_out.path ? file_out.path : "(root)");
    if (numch >= sizeof(linebuf))
        ui_truncate_line(linebuf, sizeof(linebuf));
    ui_queue_debug_log(linebuf);
    return 1;
}

size_t arim_arq_files_flist_on_send_buffer(size_t size)
{
    char linebuf[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    int numch;

    /* ignore preliminary BUFFER response if TNC isn't done buffering data */
    if (file_out.size > TNC_DATA_BLOCK_SIZE && size == TNC_DATA_BLOCK_SIZE)
        return size;
    if (file_out_cnt != size) {
        file_out_cnt = size;
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File listing upload for %s sending %zu of %zu bytes",
                            *file_out.path ? file_out.path : "(root)",
                                file_out.size - file_out_cnt, file_out.size);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        if (*file_out.path)
            numch = snprintf(linebuf, sizeof(linebuf), "<< [@] %s %zu of %zu bytes",
                             file_out.path, file_out.size - file_out_cnt, file_out.size);
        else
            numch = snprintf(linebuf, sizeof(linebuf), "<< [@] %zu of %zu bytes",
                             file_out.size - file_out_cnt, file_out.size);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_traffic_log(linebuf);
        if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
            if (*file_out.path)
                numch = snprintf(linebuf, sizeof(linebuf), "[%s] << [@] %s %zu of %zu bytes",
                                 util_timestamp(timestamp, sizeof(timestamp)),
                                     file_out.path, file_out.size - file_out_cnt, file_out.size);
            else
                numch = snprintf(linebuf, sizeof(linebuf), "[%s] << [@] %zu of %zu bytes",
                                 util_timestamp(timestamp, sizeof(timestamp)),
                                         file_out.size - file_out_cnt, file_out.size);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
        }
        ui_queue_data_in(linebuf);
    }
    return size;
}

int arim_arq_files_flist_on_rcv_frame(const char *data, size_t size)
{
    char databuf[MIN_DATA_BUF_SIZE];
    char timestamp[MAX_TIMESTAMP_SIZE], linebuf[MAX_LOG_LINE_SIZE];
    int numch;
    unsigned int check;
    z_stream zs;
    char zbuffer[MIN_DATA_BUF_SIZE];
    int zret;

    /* buffer data, increment count of bytes */
    if (file_in_cnt + size > sizeof(file_in.data)) {
        /* overflow */
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File listing download for %s failed, buffer overflow %zu",
                             *file_in.path ? file_in.path : "(root)", file_in_cnt + size);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf), "/ERROR Buffer overflow");
        arim_arq_send_remote(linebuf);
        arim_on_event(EV_ARQ_FILE_ERROR, 0);
        return 0;
    }
    memcpy(file_in.data + file_in_cnt, data, size);
    file_in_cnt += size;
    numch = snprintf(linebuf, sizeof(linebuf),
                     "ARQ: File listing download for %s reading %zu of %zu bytes",
                         *file_in.path ? file_in.path : "(root)", file_in_cnt, file_in.size);
    if (numch >= sizeof(linebuf))
        ui_truncate_line(linebuf, sizeof(linebuf));
    ui_queue_debug_log(linebuf);
    if (*file_in.path)
        numch = snprintf(linebuf, sizeof(linebuf), ">> [@] %s %zu of %zu bytes",
                         file_in.path, file_in_cnt, file_in.size);
    else
        numch = snprintf(linebuf, sizeof(linebuf), ">> [@] %zu of %zu bytes",
                         file_in_cnt, file_in.size);
    if (numch >= sizeof(linebuf))
        ui_truncate_line(linebuf, sizeof(linebuf));
    ui_queue_traffic_log(linebuf);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        if (*file_in.path)
            numch = snprintf(linebuf, sizeof(linebuf), "[%s] >> [@] %s %zu of %zu bytes",
                             util_timestamp(timestamp, sizeof(timestamp)),
                                file_in.path, file_in_cnt, file_in.size);
        else
            numch = snprintf(linebuf, sizeof(linebuf), "[%s] >> [@] %zu of %zu bytes",
                             util_timestamp(timestamp, sizeof(timestamp)),
                                 file_in_cnt, file_in.size);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
    }
    ui_queue_data_in(linebuf);
    arim_on_event(EV_ARQ_FLIST_RCV_FRAME, 0);
    if (file_in_cnt >= file_in.size) {
        /* if excess data, take most recent file_in.size bytes */
        if (file_in_cnt > file_in.size) {
            memmove(file_in.data, file_in.data + (file_in_cnt - file_in.size),
                                                  file_in_cnt - file_in.size);
            file_in_cnt = file_in.size;
        }
        /* verify checksum */
        check = ccitt_crc16(file_in.data, file_in.size);
        if (file_in.check != check) {
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File listing download %s failed, bad checksum %04X",
                                 *file_in.path ? file_in.path : "(root)", check);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            snprintf(linebuf, sizeof(linebuf), "/ERROR Bad checksum");
            arim_arq_send_remote(linebuf);
            arim_on_event(EV_ARQ_FILE_ERROR, 0);
            return 0;
        }
        if (zoption) {
            zs.zalloc = Z_NULL;
            zs.zfree = Z_NULL;
            zs.opaque = Z_NULL;
            zs.avail_in = file_in.size;
            zs.next_in = file_in.data;
            zs.avail_out = sizeof(zbuffer);
            zs.next_out = (Bytef *)zbuffer;
            zret = inflateInit(&zs);
            if (zret == Z_OK) {
                zret = inflate(&zs, Z_NO_FLUSH);
                inflateEnd(&zs);
                if (zret != Z_STREAM_END) {
                    numch = snprintf(linebuf, sizeof(linebuf),
                                     "ARQ: File download %s failed, decompression failed",
                                         file_in.name);
                    if (numch >= sizeof(linebuf))
                        ui_truncate_line(linebuf, sizeof(linebuf));
                    ui_queue_debug_log(linebuf);
                    snprintf(linebuf, sizeof(linebuf), "/ERROR Decompression failed");
                    arim_arq_send_remote(linebuf);
                    arim_on_event(EV_ARQ_FILE_ERROR, 0);
                    return 0;
                }
                memcpy(file_in.data, zbuffer, zs.total_out);
                file_in.size = zs.total_out;
            } else {
                numch = snprintf(linebuf, sizeof(linebuf),
                                 "ARQ: File download %s failed, decompression initialization error",
                                    file_in.name);
                if (numch >= sizeof(linebuf))
                    ui_truncate_line(linebuf, sizeof(linebuf));
                ui_queue_debug_log(linebuf);
                snprintf(linebuf, sizeof(linebuf), "/ERROR Decompression failed");
                arim_arq_send_remote(linebuf);
                arim_on_event(EV_ARQ_FILE_ERROR, 0);
                return 0;
            }
        }
        /* success */
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: Received %s file listing %s %zu bytes, checksum %04X",
                               zoption ? "compressed" : "uncompressed",
                                   *file_in.path ? file_in.path : "(root)", file_in_cnt, check);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        if (*file_in.path)
            snprintf(databuf, sizeof(databuf),
                     "/OK Received listing of %s %zu %04X", file_in.path, file_in_cnt, check);
        else
            snprintf(databuf, sizeof(databuf),
                     "/OK Received listing %zu %04X", file_in_cnt, check);
        arim_arq_send_remote(databuf);
        arim_on_event(EV_ARQ_FLIST_RCV_DONE, 0);
    }
    return 1;
}

void arim_arq_files_on_flget_done()
{
    ui_list_remote_files((char *)file_in.data, *file_in.path ? file_in.path : NULL);
}

int arim_arq_files_on_flput(char *cmd, size_t size, char *eol)
{
    char *p_check, *p_path, *p_size, *s, *e;
    char linebuf[MAX_LOG_LINE_SIZE];
    int numch;

    zoption = 0;
    /* inbound file listing, get parameters */
    p_size = p_check = p_path = NULL;
    s = cmd + 7;
    while (*s && (*s == ' ' || *s == '/'))
        ++s;
    if (*s && (s == strstr(s, "-z"))) {
        zoption = 1;
        s += 2;
        while (*s && (*s == ' ' || *s == '/'))
            ++s;
    }
    if (*s && eol) {
        /* check for checksum argument */
        e = eol - 1;
        while (e > s && (*e == ' ' || *e == '\0')) {
            *e = '\0';
            --e;
        }
        while (e > s && *e != ' ')
            --e;
        if (e > s) {
            /* at start of checksum */
            p_check = e + 1;
            while (e > s && *e == ' ') {
                *e = '\0';
                --e;
            }
            while (e > s && *e != ' ')
                --e;
            /* at start of size */
            if (e > s)
                p_size = e + 1;
            else
                p_size = e;
        }
        if (e > s) {
            while (e > s && *e == ' ') {
                *e = '\0';
                --e;
            }
            if (*s)
                p_path = s;
        }
        if (p_size && p_check) {
            snprintf(file_in.path, sizeof(file_in.path), "%s", p_path ? p_path : "");
            file_in.size = atoi(p_size);
            if (1 != sscanf(p_check, "%x", &file_in.check))
                file_in.check = 0;
            file_in_cnt = 0;
            /* data arriving next */
            arim_on_event(EV_ARQ_FLIST_RCV, 0);
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File listing download for %s %zu %04X started",
                                 p_path ? file_in.path : "(root)", file_in.size, file_in.check);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            /* cache any data remaining */
            if ((cmd + size) > eol) {
                arim_arq_files_flist_on_rcv_frame(eol, size - (eol - cmd));
            }
        } else {
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File listing download for %s failed, bad size/checksum parameter",
                                 p_path ? p_path : "(root)");
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            snprintf(linebuf, sizeof(linebuf), "/ERROR Bad parameters");
            arim_arq_send_remote(linebuf);
            arim_on_event(EV_ARQ_FILE_ERROR, 0);
        }
    } else {
        snprintf(linebuf, sizeof(linebuf),
                 "ARQ: File listing download failed, bad /FLPUT directory name");
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf), "/ERROR Directory not found");
        arim_arq_send_remote(linebuf);
        arim_on_event(EV_ARQ_FILE_ERROR, 0);
    }
    return 1;
}

int arim_arq_files_on_flget(char *cmd, size_t size, char *eol)
{
    char *p_path, *s, *e;
    char linebuf[MAX_LOG_LINE_SIZE], remote_call[TNC_MYCALL_SIZE];
    char dpath[MAX_PATH_SIZE];
    int result;

    zoption = 0;
    p_path = NULL;
    /* empty outbound data buffer before handling file listing request */
    while (arim_get_buffer_cnt() > 0)
        sleep(1);
    /* parse the parameters */
    s = cmd + 7;
    while (*s && (*s == ' ' || *s == '/'))
        ++s;
    if (*s && (s == strstr(s, "-z"))) {
        zoption = 1;
        s += 2;
        while (*s && (*s == ' ' || *s == '/'))
            ++s;
    }
    if (*s && eol) {
        p_path = s;
        /* trim trailing spaces */
        e = eol - 1;
        while (e > p_path && (*e == ' ' || *e == '\0')) {
            *e = '\0';
            --e;
        }
        snprintf(dpath, sizeof(dpath), "%s/%s", g_arim_settings.files_dir, p_path);
        if (!ini_check_ac_files_dir(dpath) && !ini_check_add_files_dir(dpath)) {
            /* directory not found */
            snprintf(linebuf, sizeof(linebuf),
                     "ARQ: File listing download %s failed, directory not found", p_path);
            ui_queue_debug_log(linebuf);
            snprintf(linebuf, sizeof(linebuf), "/ERROR Directory not found");
            arim_arq_send_remote(linebuf);
            arim_on_event(EV_ARQ_FILE_ERROR, 0);
            return 0;
        }
        /* check to see if this is an access controlled dir */
        if (ini_check_ac_files_dir(dpath) && !arim_arq_auth_get_status()) {
            /* auth required, send /A1 challenge */
            arim_copy_remote_call(remote_call, sizeof(remote_call));
            if (arim_arq_auth_on_send_a1(remote_call, "FLGET", p_path)) {
                arim_on_event(EV_ARQ_AUTH_SEND_CMD, 1);
            } else {
                /* no access for remote call, send /EAUTH response */
                snprintf(linebuf, sizeof(linebuf), "/EAUTH");
                arim_arq_send_remote(linebuf);
            }
        } else {
            /* no auth required or session previously authenticated */
            result = arim_arq_files_send_flist(p_path);
            /* if successful returns 1, otherwise -1 or 0 */
            if (result == 1)
                arim_on_event(EV_ARQ_FLIST_SEND_CMD, 0);
            else
                arim_on_event(EV_ARQ_FILE_ERROR, 0);
        }
    } else {
        /* root shared file dir */
        result = arim_arq_files_send_flist(NULL);
        /* if successful returns 1, otherwise -1 or 0 */
        if (result == 1)
            arim_on_event(EV_ARQ_FLIST_SEND_CMD, 0);
        else
            arim_on_event(EV_ARQ_FILE_ERROR, 0);
    }
    return 1;
}

int arim_arq_files_send_dyn_file(const char *fn, const char *destdir, int is_local)
{
    FILE *fp;
    char cmd_line[MAX_CMD_SIZE], cmd[MAX_CMD_SIZE];
    char linebuf[MAX_LOG_LINE_SIZE], databuf[MIN_DATA_BUF_SIZE];
    size_t max, len;
    int i, numch;
    z_stream zs;
    char zbuffer[MIN_DATA_BUF_SIZE];
    int zret;

    /* check for dynamic file name */
    len = strlen(fn);
    for (i = 0; i < g_arim_settings.dyn_files_cnt; i++) {
        if (!strncmp(g_arim_settings.dyn_files[i], fn, len)) {
            if (g_arim_settings.dyn_files[i][len] == ':') {
                snprintf(cmd, sizeof(cmd), "%s", &(g_arim_settings.dyn_files[i][len + 1]));
                break;
            }
        }
    }
    if (i == g_arim_settings.dyn_files_cnt) {
        return 0;
    }
    max = atoi(g_arim_settings.max_file_size);
    pthread_mutex_lock(&mutex_df_error_log);
    numch = snprintf(cmd_line, sizeof(cmd_line), "%s 2>> %s", cmd, g_df_error_fn);
    pthread_mutex_unlock(&mutex_df_error_log);
    fp = popen(cmd_line, "r");
    if (!fp) {
        if (is_local) {
            ui_show_dialog("\tCannot send dynamic file:\n"
                           "\tcommand invocation failed.\n \n\t[O]k", "oO \n");
        } else {
            snprintf(linebuf, sizeof(linebuf),
                        "/ERROR Cannot open file");
            arim_arq_send_remote(linebuf);
        }
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s failed, dynamic file invocation failed", fn);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        return -1;
    }
    file_out.size = fread(file_out.data, 1, sizeof(file_out.data), fp);
    pclose(fp);
    /* test file size */
    if (file_out.size > max) {
        if (is_local) {
            ui_show_dialog("\tCannot send file:\n"
                           "\tfile size exceeds limit.\n \n\t[O]k", "oO \n");
        } else {
            snprintf(linebuf, sizeof(linebuf), "/ERROR File size exceeds limit");
            arim_arq_send_remote(linebuf);
        }
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s failed, size exceeds limit", fn);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        return -1;
    } else if (file_out.size == 0) {
        if (is_local) {
            ui_show_dialog("\tCannot send file:\n"
                           "\tdynamic file read failed.\n \n\t[O]k", "oO \n");
        } else {
            snprintf(linebuf, sizeof(linebuf), "/ERROR Cannot open file");
            arim_arq_send_remote(linebuf);
        }
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s failed, dynamic file read failed", fn);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        return -1;
    }
    /* compress file if -z option invoked */
    if (zoption) {
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;
        zs.avail_in = file_out.size;
        zs.next_in = file_out.data;
        zs.avail_out = sizeof(zbuffer);
        zs.next_out = (Bytef *)zbuffer;
        zret = deflateInit(&zs, Z_BEST_COMPRESSION);
        if (zret == Z_OK) {
            zret = deflate(&zs, Z_FINISH);
            if (zret != Z_STREAM_END) {
                if (is_local) {
                    ui_show_dialog("\tCannot send file:\n"
                                   "\tcompression failed.\n \n\t[O]k", "oO \n");
                } else {
                    snprintf(linebuf, sizeof(linebuf), "/ERROR Cannot open file");
                    arim_arq_send_remote(linebuf);
                }
                numch = snprintf(linebuf, sizeof(linebuf),
                                 "ARQ: File upload %s failed, compression error", fn);
                if (numch >= sizeof(linebuf))
                    ui_truncate_line(linebuf, sizeof(linebuf));
                ui_queue_debug_log(linebuf);
                return -1;
            }
            deflateEnd(&zs);
            memcpy(file_out.data, zbuffer, zs.total_out);
            file_out.size = zs.total_out;
        } else {
            if (is_local) {
                ui_show_dialog("\tCannot send file:\n"
                               "\tcompression failed.\n \n\t[O]k", "oO \n");
            } else {
                snprintf(linebuf, sizeof(linebuf), "/ERROR Cannot open file");
                arim_arq_send_remote(linebuf);
            }
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File upload %s failed, compression init error", fn);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            return -1;
        }
    }
    snprintf(file_out.name, sizeof(file_out.name), "%s", fn);
    snprintf(file_out.path, sizeof(file_out.path), "%s", destdir ? destdir : "");
    file_out.check = ccitt_crc16(file_out.data, file_out.size);
    /* enqueue command for TNC */
    if (destdir)
        snprintf(databuf, sizeof(databuf), "%s %s %zu %04X > %s",
                 zoption ? "/FPUT -z" : "/FPUT",
                     file_out.name, file_out.size, file_out.check, file_out.path);
    else
        snprintf(databuf, sizeof(databuf), "%s %s %zu %04X",
                 zoption ? "/FPUT -z" : "/FPUT",
                     file_out.name, file_out.size, file_out.check);
    len = arim_arq_send_remote(databuf);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
             sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", len);
    pthread_mutex_unlock(&mutex_tnc_set);
    return 1;
}

int arim_arq_files_send_file(const char *fn, const char *destdir, int is_local)
{
    FILE *fp;
    char fpath[MAX_PATH_SIZE], dpath[MAX_PATH_SIZE];
    char linebuf[MAX_LOG_LINE_SIZE], databuf[MIN_DATA_BUF_SIZE];
    size_t max, len;
    int numch, result;
    z_stream zs;
    char zbuffer[MIN_DATA_BUF_SIZE];
    int zret;

    max = atoi(g_arim_settings.max_file_size);
    if (max <= 0) {
        if (is_local) {
            ui_show_dialog("\tCannot send file:\n"
                           "\tfile sharing is disabled.\n \n\t[O]k", "oO \n");
        } else {
            snprintf(linebuf, sizeof(linebuf), "/ERROR File sharing disabled");
            arim_arq_send_remote(linebuf);
        }
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s failed, file sharing disabled", fn);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        return 0;
    }
    result = arim_arq_files_send_dyn_file(fn, destdir, is_local);
    if (result) {
        /* result may be 1 for success, -1 for error, 0 for no match */
        return result;
    }
    /* not a dynamic file */
    if (strstr(fn, "..")) {
        /* prevent directory traversal */
        if (is_local) {
            ui_show_dialog("\tCannot send file:\n"
                           "\tbad file name or path.\n \n\t[O]k", "oO \n");
        } else {
            snprintf(linebuf, sizeof(linebuf), "/ERROR Bad file name");
            arim_arq_send_remote(linebuf);
        }
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s failed, bad file name or path", fn);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        return 0;
    }
    snprintf(fpath, sizeof(fpath), "%s", fn);
    if (strstr(basename(fpath), DEFAULT_DIGEST_FNAME)) {
        /* deny access to password digest file */
        if (is_local) {
            ui_show_dialog("\tCannot send file:\n"
                           "\tpassword file not accessible.\n \n\t[O]k", "oO \n");
        } else {
            snprintf(linebuf, sizeof(linebuf), "/ERROR File not found");
            arim_arq_send_remote(linebuf);
        }
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s failed, password digest file not accessible", fn);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        return 0;
    }
    if (!is_local) {
        /* check if access to this dir is allowed */
        snprintf(fpath, sizeof(fpath), "%s/%s", g_arim_settings.files_dir, fn);
        snprintf(dpath, sizeof(dpath), "%s", dirname(fpath));
        if (strcmp(g_arim_settings.files_dir, dpath)) {
            /* if not the base shared files directory
               path, check to see if it's allowed */
            if (!ini_check_add_files_dir(dpath) && !ini_check_ac_files_dir(dpath)) {
                snprintf(linebuf, sizeof(linebuf), "/ERROR File not found");
                arim_arq_send_remote(linebuf);
                numch = snprintf(linebuf, sizeof(linebuf),
                                 "ARQ: File upload %s failed, path not allowed", fn);
                if (numch >= sizeof(linebuf))
                    ui_truncate_line(linebuf, sizeof(linebuf));
                ui_queue_debug_log(linebuf);
                return 0;
            }
        }
    }
    snprintf(fpath, sizeof(fpath), "%s/%s", g_arim_settings.files_dir, fn);
    fp = fopen(fpath, "r");
    if (fp == NULL) {
        if (is_local) {
            ui_show_dialog("\tCannot send file:\n"
                           "\tfile not found.\n \n\t[O]k", "oO \n");
        } else {
            snprintf(linebuf, sizeof(linebuf), "/ERROR File not found");
            arim_arq_send_remote(linebuf);
        }
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s failed, file not found", fn);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        return 0;
    }
    /* read into buffer, will be sent later by arim_arq_files_on_send_cmd() */
    file_out.size = fread(file_out.data, 1, sizeof(file_out.data), fp);
    fclose(fp);
    /* test size of file */
    if (file_out.size > max) {
        if (is_local) {
            ui_show_dialog("\tCannot send file:\n"
                           "\tfile size exceeds limit.\n \n\t[O]k", "oO \n");
        } else {
            snprintf(linebuf, sizeof(linebuf), "/ERROR File size exceeds limit");
            arim_arq_send_remote(linebuf);
        }
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s failed, size exceeds limit", fn);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        return 0;
    }
    /* compress file if -z option invoked */
    if (zoption) {
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;
        zs.avail_in = file_out.size;
        zs.next_in = file_out.data;
        zs.avail_out = sizeof(zbuffer);
        zs.next_out = (Bytef *)zbuffer;
        zret = deflateInit(&zs, Z_BEST_COMPRESSION);
        if (zret == Z_OK) {
            zret = deflate(&zs, Z_FINISH);
            if (zret != Z_STREAM_END) {
                if (is_local) {
                    ui_show_dialog("\tCannot send file:\n"
                                   "\tcompression failed.\n \n\t[O]k", "oO \n");
                } else {
                    snprintf(linebuf, sizeof(linebuf), "/ERROR Cannot open file");
                    arim_arq_send_remote(linebuf);
                }
                numch = snprintf(linebuf, sizeof(linebuf),
                                 "ARQ: File upload %s failed, compression error", fn);
                if (numch >= sizeof(linebuf))
                    ui_truncate_line(linebuf, sizeof(linebuf));
                ui_queue_debug_log(linebuf);
                return 0;
            }
            deflateEnd(&zs);
            memcpy(file_out.data, zbuffer, zs.total_out);
            file_out.size = zs.total_out;
        } else {
            if (is_local) {
                ui_show_dialog("\tCannot send file:\n"
                               "\tcompression failed.\n \n\t[O]k", "oO \n");
            } else {
                snprintf(linebuf, sizeof(linebuf), "/ERROR Cannot open file");
                arim_arq_send_remote(linebuf);
            }
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File upload %s failed, compression init error", fn);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            return 0;
        }
    }
    snprintf(fpath, sizeof(fpath), "%s", fn);
    snprintf(file_out.name, sizeof(file_out.name), "%s", basename(fpath));
    snprintf(file_out.path, sizeof(file_out.path), "%s", destdir ? destdir : "");
    file_out.check = ccitt_crc16(file_out.data, file_out.size);
    /* enqueue command for TNC */
    if (destdir)
        snprintf(databuf, sizeof(databuf), "%s %s %zu %04X > %s",
                 zoption ? "/FPUT -z" : "/FPUT",
                    file_out.name, file_out.size, file_out.check, file_out.path);
    else
        snprintf(databuf, sizeof(databuf), "%s %s %zu %04X",
                 zoption ? "/FPUT -z" : "/FPUT",
                    file_out.name, file_out.size, file_out.check);
    len = arim_arq_send_remote(databuf);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
             sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", len);
    pthread_mutex_unlock(&mutex_tnc_set);
    return 1;
}

int arim_arq_files_on_send_cmd()
{
    char linebuf[MAX_LOG_LINE_SIZE];
    int numch;

    pthread_mutex_lock(&mutex_file_out);
    fileq_push(&g_file_out_q, &file_out);
    pthread_mutex_unlock(&mutex_file_out);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
             sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", file_out.size);
    pthread_mutex_unlock(&mutex_tnc_set);
    /* initialize cnt to prevent '0 of size' notification in monitor */
    file_out_cnt = file_out.size;
    numch = snprintf(linebuf, sizeof(linebuf),
                     "ARQ: File upload %s buffered for sending", file_out.name);
    if (numch >= sizeof(linebuf))
        ui_truncate_line(linebuf, sizeof(linebuf));
    ui_queue_debug_log(linebuf);
    return 1;
}

size_t arim_arq_files_on_send_buffer(size_t size)
{
    char linebuf[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    int numch;

    /* ignore preliminary BUFFER response if TNC isn't done buffering data */
    if (file_out.size > TNC_DATA_BLOCK_SIZE && size == TNC_DATA_BLOCK_SIZE)
        return size;
    if (file_out_cnt != size) {
        file_out_cnt = size;
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s sending %zu of %zu bytes",
                            file_out.name, file_out.size - file_out_cnt, file_out.size);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        numch = snprintf(linebuf, sizeof(linebuf), "<< [@] %s %zu of %zu bytes",
                         file_out.name, file_out.size - file_out_cnt, file_out.size);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_traffic_log(linebuf);
        if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
            numch = snprintf(linebuf, sizeof(linebuf), "[%s] << [@] %s %zu of %zu bytes",
                             util_timestamp(timestamp, sizeof(timestamp)),
                                 file_out.name, file_out.size - file_out_cnt, file_out.size);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
        }
        ui_queue_data_in(linebuf);
    }
    return size;
}

int arim_arq_files_on_rcv_frame(const char *data, size_t size)
{
    FILE *fp;
    DIR *dirp;
    char fpath[MAX_PATH_SIZE*2], dpath[MAX_PATH_SIZE], databuf[MIN_DATA_BUF_SIZE];
    char timestamp[MAX_TIMESTAMP_SIZE], linebuf[MAX_LOG_LINE_SIZE];
    int numch;
    unsigned int check;
    z_stream zs;
    char zbuffer[MIN_DATA_BUF_SIZE];
    int zret;

    /* buffer data, increment count of bytes */
    if (file_in_cnt + size > sizeof(file_in.data)) {
        /* overflow */
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File download %s failed, buffer overflow %zu",
                             file_in.name, file_in_cnt + size);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf), "/ERROR Buffer overflow");
        arim_arq_send_remote(linebuf);
        arim_on_event(EV_ARQ_FILE_ERROR, 0);
        return 0;
    }
    memcpy(file_in.data + file_in_cnt, data, size);
    file_in_cnt += size;
    numch = snprintf(linebuf, sizeof(linebuf),
                     "ARQ: File download %s reading %zu of %zu bytes",
                         file_in.name, file_in_cnt, file_in.size);
    if (numch >= sizeof(linebuf))
        ui_truncate_line(linebuf, sizeof(linebuf));
    ui_queue_debug_log(linebuf);
    numch = snprintf(linebuf, sizeof(linebuf), ">> [@] %s %zu of %zu bytes",
                     file_in.name, file_in_cnt, file_in.size);
    if (numch >= sizeof(linebuf))
        ui_truncate_line(linebuf, sizeof(linebuf));
    ui_queue_traffic_log(linebuf);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        numch = snprintf(linebuf, sizeof(linebuf), "[%s] >> [@] %s %zu of %zu bytes",
                         util_timestamp(timestamp, sizeof(timestamp)),
                            file_in.name, file_in_cnt, file_in.size);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
    }
    ui_queue_data_in(linebuf);
    arim_on_event(EV_ARQ_FILE_RCV_FRAME, 0);
    if (file_in_cnt >= file_in.size) {
        /* if excess data, take most recent file_in.size bytes */
        if (file_in_cnt > file_in.size) {
            memmove(file_in.data, file_in.data + (file_in_cnt - file_in.size),
                                                  file_in_cnt - file_in.size);
            file_in_cnt = file_in.size;
        }
        /* verify checksum */
        check = ccitt_crc16(file_in.data, file_in.size);
        if (file_in.check != check) {
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File download %s failed, bad checksum %04X",
                                 file_in.name, check);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            snprintf(linebuf, sizeof(linebuf), "/ERROR Bad checksum");
            arim_arq_send_remote(linebuf);
            arim_on_event(EV_ARQ_FILE_ERROR, 0);
            return 0;
        }
        /* make sure access to directory is allowed */
        snprintf(dpath, sizeof(dpath), "%s/%s", g_arim_settings.files_dir, file_in.path);
        snprintf(fpath, sizeof(fpath), "%s/%s", g_arim_settings.files_dir, DEFAULT_DOWNLOAD_DIR);
        if ((strstr(file_in.path, "..") || strstr(file_in.name, "..")) ||
            (strcmp(dpath, fpath) &&
            !ini_check_add_files_dir(dpath) &&
            !ini_check_ac_files_dir(dpath))) {
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File download %s failed, directory %s not accessible",
                                  file_in.name, dpath);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            snprintf(linebuf, sizeof(linebuf), "/ERROR Directory not accessible");
            arim_arq_send_remote(linebuf);
            arim_on_event(EV_ARQ_FILE_ERROR, 0);
            return 0;
        }
        dirp = opendir(dpath);
        if (!dirp) {
            /* if directory not found, try to create it */
            if (errno == ENOENT &&
                mkdir(dpath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
                numch = snprintf(linebuf, sizeof(linebuf),
                                 "ARQ: File download %s failed, cannot open directory %s",
                                     file_in.name, dpath);
                if (numch >= sizeof(linebuf))
                    ui_truncate_line(linebuf, sizeof(linebuf));
                ui_queue_debug_log(linebuf);
                snprintf(linebuf, sizeof(linebuf), "/ERROR Cannot open directory");
                arim_arq_send_remote(linebuf);
                arim_on_event(EV_ARQ_FILE_ERROR, 0);
                return 0;
            }
        } else {
            closedir(dirp);
        }
        if (zoption) {
            zs.zalloc = Z_NULL;
            zs.zfree = Z_NULL;
            zs.opaque = Z_NULL;
            zs.avail_in = file_in.size;
            zs.next_in = file_in.data;
            zs.avail_out = sizeof(zbuffer);
            zs.next_out = (Bytef *)zbuffer;
            zret = inflateInit(&zs);
            if (zret == Z_OK) {
                zret = inflate(&zs, Z_NO_FLUSH);
                inflateEnd(&zs);
                if (zret != Z_STREAM_END) {
                    numch = snprintf(linebuf, sizeof(linebuf),
                                     "ARQ: File download %s failed, decompression failed",
                                         file_in.name);
                    if (numch >= sizeof(linebuf))
                        ui_truncate_line(linebuf, sizeof(linebuf));
                    ui_queue_debug_log(linebuf);
                    snprintf(linebuf, sizeof(linebuf), "/ERROR Decompression failed");
                    arim_arq_send_remote(linebuf);
                    arim_on_event(EV_ARQ_FILE_ERROR, 0);
                    return 0;
                }
                memcpy(file_in.data, zbuffer, zs.total_out);
                file_in.size = zs.total_out;
            } else {
                numch = snprintf(linebuf, sizeof(linebuf),
                                 "ARQ: File download %s failed, decompression initialization error",
                                    file_in.name);
                if (numch >= sizeof(linebuf))
                    ui_truncate_line(linebuf, sizeof(linebuf));
                ui_queue_debug_log(linebuf);
                snprintf(linebuf, sizeof(linebuf), "/ERROR Decompression failed");
                arim_arq_send_remote(linebuf);
                arim_on_event(EV_ARQ_FILE_ERROR, 0);
                return 0;
            }
        }
        /* now write file */
        snprintf(fpath, sizeof(fpath), "%s/%s", dpath, file_in.name);
        fp = fopen(fpath, "w");
        if (fp != NULL) {
            fwrite(file_in.data, 1, file_in.size, fp);
            fclose(fp);
        } else {
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File download %s failed, file open error", file_in.name);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            snprintf(linebuf, sizeof(linebuf), "/ERROR Cannot open file");
            arim_arq_send_remote(linebuf);
            arim_on_event(EV_ARQ_FILE_ERROR, 0);
            return 0;
        }
        /* success */
        numch = snprintf(linebuf, sizeof(linebuf),
                         "ARQ: Saved %s file %s %zu bytes, checksum %04X",
                               zoption ? "compressed" : "uncompressed",
                                   file_in.name, file_in_cnt, check);
        if (numch >= sizeof(linebuf))
            ui_truncate_line(linebuf, sizeof(linebuf));
        ui_queue_debug_log(linebuf);
        snprintf(databuf, sizeof(databuf),
                 "/OK %s %zu %04X saved", file_in.name, file_in_cnt, check);
        arim_arq_send_remote(databuf);
        arim_on_event(EV_ARQ_FILE_RCV_DONE, 0);
    }
    return 1;
}

int arim_arq_files_on_fget(char *cmd, size_t size, char *eol)
{
    char *p_name, *p_path, *s, *e;
    char linebuf[MAX_LOG_LINE_SIZE], remote_call[TNC_MYCALL_SIZE];
    char dpath[MAX_PATH_SIZE], add_file_dir[MAX_DIR_PATH_SIZE];
    int result;

    zoption = 0;
    p_path = NULL;
    /* empty outbound data buffer before handling file request */
    while (arim_get_buffer_cnt() > 0)
        sleep(1);
    /* parse the parameters */
    s = cmd + 6;
    while (*s && *s == ' ')
        ++s;
    if (*s && (s == strstr(s, "-z"))) {
        zoption = 1;
        s += 2;
        while (*s && *s == ' ')
            ++s;
    }
    p_name = s;
    if (*p_name && eol) {
        /* trim trailing spaces */
        e = eol - 1;
        while (e > p_name && (*e == ' ' || *e == '\0')) {
            *e = '\0';
            --e;
        }
        /* check for destination dir argument */
        s = e;
        while (s > p_name && *s != '>')
            --s;
        if (*s == '>') {
            /* found a destination dir path, trim leading spaces */
            *s = '\0';
            e = s - 1;
            ++s;
            while (*s && *s == ' ')
                ++s;
            /* ignore leading '/' */
            if (*s == '/')
                ++s;
            p_path = s;
            /* ignore empty result */
            if (!strlen(p_path))
                p_path = NULL;
        }
        /* replace stray '>' characters in file name string */
        s = strstr(p_name, ">");
        while (s) {
            *s = ' ';
            s = strstr(p_name, ">");
        }
        /* trim trailing spaces from file name */
        while (e > p_name && *e == ' ') {
            *e = '\0';
            --e;
        }
        /* check for directory component in name */
        snprintf(add_file_dir, sizeof(add_file_dir), "%s", p_name);
        e = add_file_dir + strlen(add_file_dir);
        while (e > add_file_dir && *e != '/')
            --e;
        *e = '\0';
        if (e > add_file_dir) {
            snprintf(dpath, sizeof(dpath), "%s/%s", g_arim_settings.files_dir, add_file_dir);
            if (!ini_check_ac_files_dir(dpath) && !ini_check_add_files_dir(dpath)) {
                /* directory not found */
                snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s failed, file not found", p_name);
                ui_queue_debug_log(linebuf);
                snprintf(linebuf, sizeof(linebuf), "/ERROR File not found");
                arim_arq_send_remote(linebuf);
                arim_on_event(EV_ARQ_FILE_ERROR, 0);
                return 0;
            }
            /* check to see if this is an access controlled dir */
            if (ini_check_ac_files_dir(dpath) && !arim_arq_auth_get_status()) {
                /* auth required, send /A1 challenge */
                arim_copy_remote_call(remote_call, sizeof(remote_call));
                if (arim_arq_auth_on_send_a1(remote_call, "FGET", p_name)) {
                    arim_on_event(EV_ARQ_AUTH_SEND_CMD, 1);
                } else {
                    /* no access for remote call, send /EAUTH response */
                    snprintf(linebuf, sizeof(linebuf), "/EAUTH");
                    arim_arq_send_remote(linebuf);
                }
            } else {
                /* no auth required or session previously authenticated */
                result = arim_arq_files_send_file(p_name, p_path, 0);
                /* if successful returns 1, otherwise -1 or 0 */
                if (result == 1)
                    arim_on_event(EV_ARQ_FILE_SEND_CMD, 0);
                else
                    arim_on_event(EV_ARQ_FILE_ERROR, 0);
            }
        } else {
            /* file located in root shared file dir */
            result = arim_arq_files_send_file(p_name, p_path, 0);
            /* if successful returns 1, otherwise -1 or 0 */
            if (result == 1)
                arim_on_event(EV_ARQ_FILE_SEND_CMD, 0);
            else
                arim_on_event(EV_ARQ_FILE_ERROR, 0);
        }
    } else {
        snprintf(linebuf, sizeof(linebuf), "ARQ: Bad /FGET file name parameter");
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf), "/ERROR File not found");
        arim_arq_send_remote(linebuf);
        arim_on_event(EV_ARQ_FILE_ERROR, 0);
    }
    return 1;
}

int arim_arq_files_on_fput(char *cmd, size_t size, char *eol, int arq_cs_role)
{
    char *p_check, *p_name, *p_path, *p_size, *s, *e;
    char linebuf[MAX_LOG_LINE_SIZE], remote_call[TNC_MYCALL_SIZE];
    char dpath[MAX_PATH_SIZE];
    int numch;

    zoption = 0;
    /* inbound file transfer, get parameters */
    p_size = p_check = p_path = NULL;
    s = cmd + 6;
    while (*s && *s == ' ')
        ++s;
    if (*s && (s == strstr(s, "-z"))) {
        zoption = 1;
        s += 2;
        while (*s && *s == ' ')
            ++s;
    }
    p_name = s;
    if (*p_name && eol) {
        /* check for destination dir argument */
        e = eol - 1;
        while (e > p_name && (*e == ' ' || *e == '\0')) {
            *e = '\0';
            --e;
        }
        s = e;
        while (s > p_name && *s != '>')
            --s;
        if (*s == '>') {
            /* found a path, trim leading spaces */
            *s = '\0';
            e = s - 1;
            ++s;
            while (*s && *s == ' ')
                ++s;
            /* ignore leading '/' */
            if (*s == '/')
                ++s;
            p_path = s;
            /* ignore empty result */
            if (!strlen(p_path))
                p_path = NULL;
        }
        while (e > p_name && *e == ' ')
            --e;
        while (e > p_name && *e != ' ')
            --e;
        if (e > p_name) {
            /* at start of checksum */
            p_check = e + 1;
            *e = '\0';
            while (e > p_name && *e == ' ')
                --e;
            while (e > p_name && *e != ' ') {
                --e;
            }
            if (e > p_name) {
                /* at start of size */
                p_size = e + 1;
                *e = '\0';
            }
        }
        /* replace stray '>' characters in file name string */
        s = strstr(p_name, ">");
        while (s) {
            *s = ' ';
            s = strstr(p_name, ">");
        }
        /* trim trailing spaces from file name */
        while (e > p_name && *e == ' ') {
            *e = '\0';
            --e;
        }
        if (p_size && p_check) {
            snprintf(file_in.name, sizeof(file_in.name), "%s", basename(p_name));
            snprintf(file_in.path, sizeof(file_in.path), "%s",
                         p_path ? p_path : DEFAULT_DOWNLOAD_DIR);
            file_in.size = atoi(p_size);
            if (1 != sscanf(p_check, "%x", &file_in.check))
                file_in.check = 0;
            file_in_cnt = 0;
            if (arq_cs_role == ARQ_SERVER_STN) {
                if (p_path) {
                    snprintf(dpath, sizeof(dpath), "%s/%s", g_arim_settings.files_dir, p_path);
                    if (!ini_check_ac_files_dir(dpath) && !ini_check_add_files_dir(dpath)) {
                        /* directory not found */
                        numch = snprintf(linebuf, sizeof(linebuf),
                                         "ARQ: File download %s failed, directory not found", dpath);
                        if (numch >= sizeof(linebuf))
                            ui_truncate_line(linebuf, sizeof(linebuf));
                        ui_queue_debug_log(linebuf);
                        snprintf(linebuf, sizeof(linebuf), "/ERROR Directory not found");
                        arim_arq_send_remote(linebuf);
                        arim_on_event(EV_ARQ_FILE_ERROR, 0);
                        return 0;
                    }
                    /* check to see if this is an access controlled dir */
                    if (ini_check_ac_files_dir(dpath) && !arim_arq_auth_get_status()) {
                        /* auth required, send a1 challenge */
                        arim_copy_remote_call(remote_call, sizeof(remote_call));
                        if (arim_arq_auth_on_send_a1(remote_call, "FPUT", p_name)) {
                            arim_on_event(EV_ARQ_AUTH_SEND_CMD, 1);
                        } else {
                            /* no access for remote call, send /EAUTH response */
                            snprintf(linebuf, sizeof(linebuf), "/EAUTH");
                            arim_arq_send_remote(linebuf);
                        }
                    } else {
                        /* no auth required or session previously authenticated */
                        snprintf(linebuf, sizeof(linebuf), "/OK");
                        arim_arq_send_remote(linebuf);
                        arim_on_event(EV_ARQ_FILE_RCV_WAIT_OK, 0);
                        numch = snprintf(linebuf, sizeof(linebuf),
                                         "ARQ: File download %s to %s %zu %04X sending OK",
                                             file_in.name, file_in.path, file_in.size, file_in.check);
                        if (numch >= sizeof(linebuf))
                            ui_truncate_line(linebuf, sizeof(linebuf));
                        ui_queue_debug_log(linebuf);
                    }
                } else {
                    /* file located in root shared file dir */
                    snprintf(linebuf, sizeof(linebuf), "/OK");
                    arim_arq_send_remote(linebuf);
                    arim_on_event(EV_ARQ_FILE_RCV_WAIT_OK, 0);
                    numch = snprintf(linebuf, sizeof(linebuf),
                                     "ARQ: File download %s to %s %zu %04X sending OK",
                                         file_in.name, file_in.path, file_in.size, file_in.check);
                    if (numch >= sizeof(linebuf))
                        ui_truncate_line(linebuf, sizeof(linebuf));
                    ui_queue_debug_log(linebuf);
                }
            } else {
                /* data arriving next if role is is 'client' */
                arim_on_event(EV_ARQ_FILE_RCV, 0);
                numch = snprintf(linebuf, sizeof(linebuf),
                                 "ARQ: File download %s to %s %zu %04X started",
                                     file_in.name, file_in.path, file_in.size, file_in.check);
                if (numch >= sizeof(linebuf))
                    ui_truncate_line(linebuf, sizeof(linebuf));
                ui_queue_debug_log(linebuf);
                /* cache any data remaining */
                if ((cmd + size) > eol) {
                    arim_arq_files_on_rcv_frame(eol, size - (eol - cmd));
                }
            }
        } else {
            numch = snprintf(linebuf, sizeof(linebuf),
                             "ARQ: File download %s failed, bad size/checksum parameter", p_name);
            if (numch >= sizeof(linebuf))
                ui_truncate_line(linebuf, sizeof(linebuf));
            ui_queue_debug_log(linebuf);
            snprintf(linebuf, sizeof(linebuf), "/ERROR Bad parameters");
            arim_arq_send_remote(linebuf);
            arim_on_event(EV_ARQ_FILE_ERROR, 0);
        }
    } else {
        snprintf(linebuf, sizeof(linebuf),
                 "ARQ: File download failed, bad /FPUT file name");
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf), "/ERROR File not found");
        arim_arq_send_remote(linebuf);
        arim_on_event(EV_ARQ_FILE_ERROR, 0);
    }
    return 1;
}

int arim_arq_files_on_client_fget(const char *cmd, const char *fn, const char *destdir, int use_zoption)
{
    /* called from cmd processor when user issues /FGET at prompt */
    char linebuf[MAX_LOG_LINE_SIZE];
    char fpath[MAX_PATH_SIZE];
    char *e, *f;
    size_t len;

    snprintf(fpath, sizeof(fpath), "%s", fn);
    /* replace stray '>' characters in file name string */
    f = strstr(fpath, ">");
    while (f) {
        *f = ' ';
        f = strstr(fpath, ">");
    }
    /* trim leading and trailing spaces */
    f = fpath;
    while (*f && *f == ' ')
        ++f;
    len = strlen(fpath);
    e = &fpath[len - 1];
    while (e > f && *e == ' ') {
        *e = '\0';
        --e;
    }
    if (!strlen(f)) {
        ui_show_dialog("\tCannot get file:\n"
                       "\tbad file name or path.\n \n\t[O]k", "oO \n");
        snprintf(linebuf, sizeof(linebuf),
                 "ARQ: File download failed, bad file name or path");
        ui_queue_debug_log(linebuf);
        return 0;
    }
    arim_arq_auth_set_ha2_info("FGET", f);
    arim_arq_send_remote(cmd);
    arim_on_event(EV_ARQ_FILE_RCV_WAIT, 0);
    return 1;
}

int arim_arq_files_on_client_fput(const char *fn, const char *destdir, int use_zoption)
{
    /* called from cmd processor when user issues /FPUT at prompt */
    char linebuf[MAX_LOG_LINE_SIZE];
    char fpath[MAX_PATH_SIZE], dpath[MAX_DIR_PATH_SIZE];
    char *e, *f, *d;
    size_t len;

    zoption = use_zoption;
    snprintf(fpath, sizeof(fpath), "%s", fn);
    /* replace stray '>' characters in file name string */
    f = strstr(fpath, ">");
    while (f) {
        *f = ' ';
        f = strstr(fpath, ">");
    }
    /* trim leading and trailing spaces */
    f = fpath;
    while (*f && *f == ' ')
        ++f;
    len = strlen(fpath);
    e = &fpath[len - 1];
    while (e > f && *e == ' ') {
        *e = '\0';
        --e;
    }
    if (!strlen(f)) {
        ui_show_dialog("\tCannot send file:\n"
                       "\tbad file name or path.\n \n\t[O]k", "oO \n");
        snprintf(linebuf, sizeof(linebuf),
                 "ARQ: File upload failed, bad file name or path");
        ui_queue_debug_log(linebuf);
        return 0;
    }
    if (strstr(basename(f), DEFAULT_DIGEST_FNAME)) {
        /* deny access to password digest file */
        ui_show_dialog("\tCannot send file:\n"
                       "\tpassword file not accessible.\n \n\t[O]k", "oO \n");
        snprintf(linebuf, sizeof(linebuf),
                 "ARQ: File upload failed, password digest file not accessible");
        ui_queue_debug_log(linebuf);
        return 0;
    }
    if (destdir) {
        snprintf(dpath, sizeof(dpath), "%s", destdir);
        /* replace stray '>' characters in destination dir string */
        d = strstr(dpath, ">");
        while (d) {
            *d = ' ';
            d = strstr(dpath, ">");
        }
        /* trim leading and trailing spaces */
        d = dpath;
        while (*d && *d == ' ')
            ++d;
        len = strlen(dpath);
        e = &dpath[len - 1];
        while (e > d && *e == ' ') {
            *e = '\0';
            --e;
        }
        /* ignore leading '/' character */
        if (*d == '/')
            ++d;
        if (!strlen(d))
            d = NULL;
    } else {
        d = NULL;
    }
    if (arim_arq_files_send_file(f, d, 1) == 1) {
        /* if successful returns 1, otherwise -1 or 0 */
        arim_arq_auth_set_ha2_info("FPUT", basename(f)); /* base file name only for /FPUT */
        arim_on_event(EV_ARQ_FILE_SEND_CMD_CLIENT, 0);
    }
    return 1;
}

int arim_arq_files_on_client_file(const char *cmd)
{
    char *s, *e;
    char fpath[MAX_PATH_SIZE];

    /* called from cmd processor when user issues /FILE at prompt */
    snprintf(fpath, sizeof(fpath), "%s", cmd);
    s = fpath + 5;
    while (*s && *s == ' ')
        ++s;
    if (*s) {
        /* trim trailing spaces */
        e = s + strlen(s) - 1;
        while (e > s && *e == ' ') {
            *e = '\0';
            --e;
        }
    }
    arim_arq_auth_set_ha2_info("FILE", s);
    arim_arq_send_remote(cmd);
    return 1;
}

int arim_arq_files_on_client_flist(const char *cmd)
{
    char *s, *e;
    char dpath[MAX_DIR_PATH_SIZE];

    /* called from cmd processor when user issues /FLIST at prompt */
    snprintf(dpath, sizeof(dpath), "%s", cmd);
    s = dpath + 6;
    while (*s && (*s == ' ' || *s == '/'))
        ++s;
    if (*s) {
        /* trim trailing spaces */
        e = s + strlen(s) - 1;
        while (e > s && *e == ' ') {
            *e = '\0';
            --e;
        }
    }
    arim_arq_auth_set_ha2_info("FLIST", s);
    arim_arq_send_remote(cmd);
    return 1;
}
int arim_arq_files_on_client_flget(const char *cmd, const char *destdir, int use_zoption)
{
    char *p, dpath[MAX_PATH_SIZE];

    if (destdir) {
        snprintf(dpath, sizeof(dpath), "%s", destdir);
        /* trim trailing spaces */
        p = dpath + strlen(dpath) - 1;
        while (p > dpath && *p == ' ') {
            *p = '\0';
            --p;
        }
    }
    arim_arq_auth_set_ha2_info("FLGET", destdir ? dpath : "");
    arim_arq_send_remote(cmd);
    arim_on_event(EV_ARQ_FLIST_RCV_WAIT, 0);
    return 1;
}

