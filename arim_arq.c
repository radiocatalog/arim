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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include "main.h"
#include "bufq.h"
#include "arim_proto.h"
#include "cmdproc.h"
#include "ini.h"
#include "ui.h"
#include "ui_files.h"
#include "util.h"
#include "ui_dialog.h"
#include "log.h"
#include "mbox.h"
#include "zlib.h"
#include "datathread.h"

static int arq_rpts, arq_count;
static FILEQUEUEITEM file_in;
static FILEQUEUEITEM file_out;
static size_t file_in_cnt, file_out_cnt;
static MSGQUEUEITEM msg_in;
static MSGQUEUEITEM msg_out;
static size_t msg_in_cnt, msg_out_cnt;
static int zoption;

int arim_send_arq_conn_req(const char *repeats, const char *to_call)
{
    char tcall[TNC_MYCALL_SIZE], buffer[MAX_LOG_LINE_SIZE];
    size_t i, len;

    if (!g_tnc_attached || !arim_is_idle())
        return 0;
    /* force call to uppercase */
    len = strlen(to_call);
    for (i = 0; i < len; i++)
        tcall[i] = toupper(to_call[i]);
    tcall[i] = '\0';
    /* cache remote call and repeat count */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].arq_remote_call,
        sizeof(g_tnc_settings[g_cur_tnc].arq_remote_call), "%s", tcall);
    pthread_mutex_unlock(&mutex_tnc_set);
    if (repeats)
        arq_rpts = atoi(repeats);
    else
        arq_rpts = 10;
    arq_count = 0;
    /* are pilot pings needed first? */
    if (atoi(g_arim_settings.pilot_ping)) {
        snprintf(prev_to_call, sizeof(prev_to_call), "%s", to_call);
        arim_on_event(EV_ARQ_CONNECT_PP, 0);
        return 1;
    }
    /* change state */
    arim_on_event(EV_ARQ_CONNECT, 0);
    snprintf(buffer, sizeof(buffer), "ARQCALL %s %d", tcall, arq_rpts);
    ui_queue_cmd_out(buffer);
    return 1;
}

int arim_send_arq_conn_req_pp()
{
    char buffer[MAX_LOG_LINE_SIZE];

    /* change state */
    arim_on_event(EV_ARQ_CONNECT, 0);
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(buffer, sizeof(buffer), "ARQCALL %s %d",
                g_tnc_settings[g_cur_tnc].arq_remote_call, arq_rpts);
    pthread_mutex_unlock(&mutex_tnc_set);
    ui_queue_cmd_out(buffer);
    return 1;
}

int arim_send_arq_conn_req_ptt(int ptt_true)
{
    char buffer[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    char mycall[TNC_MYCALL_SIZE], tcall[TNC_MYCALL_SIZE];

    if (!ptt_true)
        return 1;
    /* if ptt true in connect wait state then
       print to monitor view and traffic log */
    arim_copy_mycall(mycall, sizeof(mycall));
    arim_copy_remote_call(tcall, sizeof(tcall));
    ++arq_count;
    snprintf(buffer, sizeof(buffer), "<< [@] %s>%s (Connect request %d of %d)",
                 mycall, tcall, arq_count, arq_rpts);
    ui_queue_traffic_log(buffer);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        snprintf(buffer, sizeof(buffer), "[%s] << [@] %s>%s (Connect request %d of %d)",
                util_timestamp(timestamp, sizeof(timestamp)),
                    mycall, tcall, arq_count, arq_rpts);
    }
    ui_queue_data_in(buffer);
    return 1;
}

int arim_on_arq_target()
{
    char buffer[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    char target_call[TNC_MYCALL_SIZE];

    /* we are the target of an incoming ARQ connect request so
       print to monitor view and traffic log */
    arim_copy_target_call(target_call, sizeof(target_call));
    /* populate remote call with placeholder value for display
       purposes until CONNECTED async response is received */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].arq_remote_call,
        sizeof(g_tnc_settings[g_cur_tnc].arq_remote_call), "%s", "?????");
    pthread_mutex_unlock(&mutex_tnc_set);
    snprintf(buffer, sizeof(buffer), ">> [@] %s>%s (Connect request)",
                g_tnc_settings[g_cur_tnc].arq_remote_call, target_call);
    ui_queue_traffic_log(buffer);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        pthread_mutex_lock(&mutex_tnc_set);
        snprintf(buffer, sizeof(buffer), "[%s] >> [@] %s>%s (Connect request)",
                util_timestamp(timestamp, sizeof(timestamp)),
                    g_tnc_settings[g_cur_tnc].arq_remote_call, target_call);
        pthread_mutex_unlock(&mutex_tnc_set);
    }
    ui_queue_data_in(buffer);
    return 1;
}

int arim_on_arq_connected()
{
    char buffer[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    char remote_call[TNC_MYCALL_SIZE], target_call[TNC_MYCALL_SIZE];

    /* we are connected to a remote station now so
       print to monitor view and traffic log */
    arim_copy_target_call(target_call, sizeof(target_call));
    if (!strlen(target_call))
        arim_copy_mycall(target_call, sizeof(target_call));
    arim_copy_remote_call(remote_call, sizeof(remote_call));
    snprintf(buffer, sizeof(buffer),
                ">> [@] %s>%s (Connected)",
                remote_call, target_call);
    ui_queue_traffic_log(buffer);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        snprintf(buffer, sizeof(buffer),
                "[%s] >> [@] %s>%s (Connected, Press Spacebar to type, CTRL-X to disconnect)",
                util_timestamp(timestamp, sizeof(timestamp)), remote_call, target_call);
    }
    ui_queue_data_in(buffer);
    snprintf(buffer, sizeof(buffer), "7[@] %-10s ", remote_call);
    ui_queue_heard(buffer);
    show_recents = show_ptable = 0; /* close recents or ping history view if open */
    return 1;
}

int arim_send_arq_disconn_req()
{
    char buffer[MAX_LOG_LINE_SIZE];
    char remote_call[TNC_MYCALL_SIZE], target_call[TNC_MYCALL_SIZE];

    arim_copy_target_call(target_call, sizeof(target_call));
    if (!strlen(target_call))
        arim_copy_mycall(target_call, sizeof(target_call));
    arim_copy_remote_call(remote_call, sizeof(remote_call));
    snprintf(buffer, sizeof(buffer),
                "%s<%s (Disconnect request)", remote_call, target_call);
    ui_queue_data_out(buffer);
    ui_queue_cmd_out("DISCONNECT");
    return 1;
}

int arim_on_arq_disconnected()
{
    char buffer[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    char remote_call[TNC_MYCALL_SIZE], target_call[TNC_MYCALL_SIZE];

    /* we are disconnected from the remote station now so
       print to monitor view and traffic log */
    arim_copy_target_call(target_call, sizeof(target_call));
    if (!strlen(target_call))
        arim_copy_mycall(target_call, sizeof(target_call));
    arim_copy_remote_call(remote_call, sizeof(remote_call));
    snprintf(buffer, sizeof(buffer),
                ">> [@] %s>%s (Disconnected)",
                remote_call, target_call);
    ui_queue_traffic_log(buffer);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        snprintf(buffer, sizeof(buffer),
                "[%s] >> [@] %s>%s (Disconnected)",
                util_timestamp(timestamp, sizeof(timestamp)), remote_call, target_call);
    }
    ui_queue_data_in(buffer);
    snprintf(buffer, sizeof(buffer), "7[@] %-10s ", remote_call);
    ui_queue_heard(buffer);
    return 1;
}

size_t arim_arq_send_remote(const char *msg)
{
    char sendcr[TNC_ARQ_SENDCR_SIZE], linebuf[MAX_LOG_LINE_SIZE];

    /* check to see if CR must be sent */
    arim_copy_arq_sendcr(sendcr, sizeof(sendcr));
    if (!strncasecmp(sendcr, "TRUE", 4))
        snprintf(linebuf, sizeof(linebuf), "%s\r\n", msg);
    else
        snprintf(linebuf, sizeof(linebuf), "%s\n", msg);
    ui_queue_data_out(linebuf);
    return strlen(linebuf);
}

int arim_arq_send_dyn_file(const char *fn, const char *destdir, int is_local)
{
    FILE *fp;
    char cmd_line[MAX_CMD_SIZE], cmd[MAX_CMD_SIZE];
    char linebuf[MAX_LOG_LINE_SIZE];
    size_t i, max, len;
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
    snprintf(cmd_line, sizeof(cmd_line), "%s 2>> %s", cmd, g_df_error_fn);
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
        snprintf(linebuf, sizeof(linebuf),
                    "ARQ: File upload %s failed, dynamic file invocation failed", fn);
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
        snprintf(linebuf, sizeof(linebuf),
                "ARQ: File upload %s failed, size exceeds limit", fn);
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
        snprintf(linebuf, sizeof(linebuf),
                "ARQ: File upload %s failed, dynamic file read failed", fn);
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
                snprintf(linebuf, sizeof(linebuf),
                        "ARQ: File upload %s failed, compression error", fn);
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
            snprintf(linebuf, sizeof(linebuf),
                    "ARQ: File upload %s failed, compression init error", fn);
            ui_queue_debug_log(linebuf);
            return -1;
        }
    }
    snprintf(file_out.name, sizeof(file_out.name), "%s", fn);
    snprintf(file_out.path, sizeof(file_out.path), "%s", destdir ? destdir : "");
    file_out.check = ccitt_crc16(file_out.data, file_out.size);
    /* enqueue command for TNC */
    if (destdir)
        snprintf(linebuf, sizeof(linebuf), "%s %s %zu %04X > %s", zoption ? "/FPUT -z" : "/FPUT",
                     file_out.name, file_out.size, file_out.check, file_out.path);
    else
        snprintf(linebuf, sizeof(linebuf), "%s %s %zu %04X", zoption ? "/FPUT -z" : "/FPUT",
                     file_out.name, file_out.size, file_out.check);
    len = arim_arq_send_remote(linebuf);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
        sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", len);
    pthread_mutex_unlock(&mutex_tnc_set);
    return 1;
}

int arim_arq_send_file(const char *fn, const char *destdir, int is_local)
{
    FILE *fp;
    char fpath[MAX_DIR_PATH_SIZE], dpath[MAX_DIR_PATH_SIZE];
    char linebuf[MAX_LOG_LINE_SIZE];
    size_t max, len;
    int result;
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
        snprintf(linebuf, sizeof(linebuf),
                    "ARQ: File upload %s failed, file sharing disabled", fn);
        ui_queue_debug_log(linebuf);
        return 0;
    }
    result = arim_arq_send_dyn_file(fn, destdir, is_local);
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
        snprintf(linebuf, sizeof(linebuf),
                        "ARQ: File upload %s failed, bad file name or path", fn);
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
            if (!ui_check_files_dir(dpath)) {
                snprintf(linebuf, sizeof(linebuf), "/ERROR File not found");
                arim_arq_send_remote(linebuf);
                snprintf(linebuf, sizeof(linebuf),
                                "ARQ: File upload %s failed, path not allowed", fn);
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
        snprintf(linebuf, sizeof(linebuf),
                        "ARQ: File upload %s failed, file not found", fn);
        ui_queue_debug_log(linebuf);
        return 0;
    }
    /* read into buffer, will be sent later by arim_on_arq_file_send_cmd() */
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
        snprintf(linebuf, sizeof(linebuf),
                    "ARQ: File upload %s failed, size exceeds limit", fn);
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
                snprintf(linebuf, sizeof(linebuf),
                         "ARQ: File upload %s failed, compression error", fn);
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
            snprintf(linebuf, sizeof(linebuf),
                     "ARQ: File upload %s failed, compression init error", fn);
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
        snprintf(linebuf, sizeof(linebuf), "%s %s %zu %04X > %s",
                 zoption ? "/FPUT -z" : "/FPUT",
                    file_out.name, file_out.size, file_out.check, file_out.path);
    else
        snprintf(linebuf, sizeof(linebuf), "%s %s %zu %04X",
                 zoption ? "/FPUT -z" : "/FPUT",
                    file_out.name, file_out.size, file_out.check);
    len = arim_arq_send_remote(linebuf);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
        sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", len);
    pthread_mutex_unlock(&mutex_tnc_set);
    return 1;
}


int arim_on_arq_file_send_cmd(size_t size)
{
    char linebuf[MAX_LOG_LINE_SIZE];

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
    snprintf(linebuf, sizeof(linebuf),
        "ARQ: File upload %s buffered for sending", file_out.name);
    ui_queue_debug_log(linebuf);
    return 1;
}

size_t arim_on_arq_file_send_buffer(size_t size)
{
    char linebuf[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];

    /* ignore preliminary BUFFER response if TNC isn't done buffering data */
    if (file_out.size > TNC_DATA_BLOCK_SIZE && size == TNC_DATA_BLOCK_SIZE)
        return size;
    if (file_out_cnt != size) {
        file_out_cnt = size;
        snprintf(linebuf, sizeof(linebuf), "ARQ: File upload %s sending %zu of %zu bytes",
                    file_out.name, file_out.size - file_out_cnt, file_out.size);
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf), "<< [@] %s %zu of %zu bytes",
                    file_out.name, file_out.size - file_out_cnt, file_out.size);
        ui_queue_traffic_log(linebuf);
        if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
            snprintf(linebuf, sizeof(linebuf), "[%s] << [@] %s %zu of %zu bytes",
                     util_timestamp(timestamp, sizeof(timestamp)),
                         file_out.name, file_out.size - file_out_cnt, file_out.size);
        }
        ui_queue_data_in(linebuf);
    }
    return size;
}

int arim_on_arq_file_rcv_frame(const char *data, size_t size)
{
    FILE *fp;
    DIR *dirp;
    char fpath[MAX_DIR_PATH_SIZE], dpath[MAX_DIR_PATH_SIZE];
    char timestamp[MAX_TIMESTAMP_SIZE], linebuf[MAX_LOG_LINE_SIZE];
    unsigned int check;
    z_stream zs;
    char zbuffer[MIN_DATA_BUF_SIZE];
    int zret;

    /* buffer data, increment count of bytes */
    if (file_in_cnt + size > sizeof(file_in.data)) {
        /* overflow */
        snprintf(linebuf, sizeof(linebuf),
            "ARQ: File download %s failed, buffer overflow %zu",
                 file_in.name, file_in_cnt + size);
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf), "/ERROR Buffer overflow");
        arim_arq_send_remote(linebuf);
        arim_on_event(EV_ARQ_FILE_ERROR, 0);
        return 0;
    }
    memcpy(file_in.data + file_in_cnt, data, size);
    file_in_cnt += size;
    snprintf(linebuf, sizeof(linebuf), "ARQ: File download %s reading %zu of %zu bytes",
                file_in.name, file_in_cnt, file_in.size);
    ui_queue_debug_log(linebuf);
    snprintf(linebuf, sizeof(linebuf), ">> [@] %s %zu of %zu bytes",
                file_in.name, file_in_cnt, file_in.size);
    ui_queue_traffic_log(linebuf);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        snprintf(linebuf, sizeof(linebuf), "[%s] >> [@] %s %zu of %zu bytes",
                 util_timestamp(timestamp, sizeof(timestamp)),
                     file_in.name, file_in_cnt, file_in.size);
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
            snprintf(linebuf, sizeof(linebuf),
               "ARQ: File download %s failed, bad checksum %04X", file_in.name, check);
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
            (strcmp(dpath, fpath) && !ui_check_files_dir(dpath))) {
            snprintf(linebuf, sizeof(linebuf),
               "ARQ: File download %s failed, directory %s not accessible", file_in.name, dpath);
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
                snprintf(linebuf, sizeof(linebuf),
                    "ARQ: File download %s failed, cannot open directory %s", file_in.name, dpath);
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
                    snprintf(linebuf, sizeof(linebuf),
                        "ARQ: File download %s failed, decompression failed", file_in.name);
                    ui_queue_debug_log(linebuf);
                    snprintf(linebuf, sizeof(linebuf), "/ERROR Decompression failed");
                    arim_arq_send_remote(linebuf);
                    arim_on_event(EV_ARQ_FILE_ERROR, 0);
                    return 0;
                }
                memcpy(file_in.data, zbuffer, zs.total_out);
                file_in.size = zs.total_out;
            } else {
                snprintf(linebuf, sizeof(linebuf),
                    "ARQ: File download %s failed, decompression initialization error", file_in.name);
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
            snprintf(linebuf, sizeof(linebuf),
                "ARQ: File download %s failed, file open error", file_in.name);
            ui_queue_debug_log(linebuf);
            snprintf(linebuf, sizeof(linebuf), "/ERROR Cannot open file");
            arim_arq_send_remote(linebuf);
            arim_on_event(EV_ARQ_FILE_ERROR, 0);
            return 0;
        }
        /* success */
        snprintf(linebuf, sizeof(linebuf),
            "ARQ: Saved %s file %s %zu bytes, checksum %04X",
                zoption ? "compressed" : "uncompressed",
                    file_in.name, file_in_cnt, check);
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf),
            "/OK %s %zu %04X saved", file_in.name, file_in_cnt, check);
        arim_arq_send_remote(linebuf);
        arim_on_event(EV_ARQ_FILE_RCV_DONE, 0);
    }
    return 1;
}

int arim_on_arq_msg_put(const char *data, int use_zoption)
{
    char linebuf[MAX_LOG_LINE_SIZE];
    size_t len;
    z_stream zs;
    char zbuffer[MIN_DATA_BUF_SIZE];
    int zret;

    zoption = use_zoption;
    /* copy into buffer, will be sent later by arim_on_arq_msg_send_cmd() */
    if (zoption) {
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;
        zs.avail_in = strlen(data);
        zs.next_in = (Bytef *)data;
        zs.avail_out = sizeof(zbuffer);
        zs.next_out = (Bytef *)zbuffer;
        zret = deflateInit(&zs, Z_BEST_COMPRESSION);
        if (zret == Z_OK) {
            zret = deflate(&zs, Z_FINISH);
            deflateEnd(&zs);
            if (zret != Z_STREAM_END) {
                ui_show_dialog("\tCannot send message:\n"
                               "\tcompression failed.\n \n\t[O]k", "oO \n");
                snprintf(linebuf, sizeof(linebuf),
                                "ARQ: Message upload failed, compression error");
                ui_queue_debug_log(linebuf);
                return 0;
            }
            memcpy(msg_out.data, zbuffer, zs.total_out);
            msg_out.size = zs.total_out;
        } else {
            ui_show_dialog("\tCannot send message:\n"
                           "\tcompression failed.\n \n\t[O]k", "oO \n");
            snprintf(linebuf, sizeof(linebuf),
                            "ARQ: Message upload failed, compression init error");
            ui_queue_debug_log(linebuf);
            return 0;
        }
    } else {
        snprintf(msg_out.data, sizeof(msg_out.data), "%s", data);
        msg_out.size = strlen(msg_out.data);
    }
    msg_out.check = ccitt_crc16((unsigned char *)msg_out.data, msg_out.size);
    arim_copy_remote_call(msg_out.call, sizeof(msg_out.call));
    /* enqueue command for TNC */
    snprintf(linebuf, sizeof(linebuf),
        "%s %s %zu %04X", zoption ? "/MPUT -z" : "/MPUT",
            msg_out.call, msg_out.size, msg_out.check);
    len = arim_arq_send_remote(linebuf);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
        sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", len);
    pthread_mutex_unlock(&mutex_tnc_set);
    arim_on_event(EV_ARQ_MSG_SEND_CMD, 0);
    return 1;
}

int arim_on_arq_msg_send_cmd()
{
    char linebuf[MAX_LOG_LINE_SIZE];

    pthread_mutex_lock(&mutex_msg_out);
    msgq_push(&g_msg_out_q, &msg_out);
    pthread_mutex_unlock(&mutex_msg_out);
    /* prime buffer count because update from TNC not immediate */
    pthread_mutex_lock(&mutex_tnc_set);
    snprintf(g_tnc_settings[g_cur_tnc].buffer,
        sizeof(g_tnc_settings[g_cur_tnc].buffer), "%zu", msg_out.size);
    pthread_mutex_unlock(&mutex_tnc_set);
    /* initialize cnt to prevent '0 of size' notification in monitor */
    msg_out_cnt = msg_out.size;
    snprintf(linebuf, sizeof(linebuf), "ARQ: Message upload buffered for sending");
    ui_queue_debug_log(linebuf);
    return 1;
}

size_t arim_on_arq_msg_send_buffer(size_t size)
{
    char linebuf[MAX_LOG_LINE_SIZE], timestamp[MAX_TIMESTAMP_SIZE];

    /* ignore preliminary BUFFER response if TNC isn't done buffering data */
    if (msg_out.size > TNC_DATA_BLOCK_SIZE && size == TNC_DATA_BLOCK_SIZE)
        return size;
    if (msg_out_cnt != size) {
        msg_out_cnt = size;
        snprintf(linebuf, sizeof(linebuf),
            "ARQ: Message to %s sending %zu of %zu bytes",
                msg_out.call, msg_out.size - msg_out_cnt, msg_out.size);
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf), "<< [@] Message to %s %zu of %zu bytes",
                    msg_out.call, msg_out.size - msg_out_cnt, msg_out.size);
        ui_queue_traffic_log(linebuf);
        if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
            snprintf(linebuf, sizeof(linebuf),
                "[%s] << [@] Message to %s %zu of %zu bytes",
                     util_timestamp(timestamp, sizeof(timestamp)),
                         msg_out.call, msg_out.size - msg_out_cnt, msg_out.size);
        }
        ui_queue_data_in(linebuf);
    }
    return size;
}

int arim_on_arq_msg_rcv_frame(const char *data, size_t size)
{
    char remote_call[TNC_MYCALL_SIZE], target_call[TNC_MYCALL_SIZE];
    char timestamp[MAX_TIMESTAMP_SIZE], linebuf[MAX_LOG_LINE_SIZE];
    unsigned int check;
    z_stream zs;
    char zbuffer[MIN_DATA_BUF_SIZE];
    int zret;

    /* buffer data, increment count of bytes */
    if (msg_in_cnt + size > sizeof(msg_in.data)) {
        /* overflow */
        snprintf(linebuf, sizeof(linebuf),
            "ARQ: Message download failed, buffer overflow %zu",
                msg_in_cnt + size);
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf), "/ERROR Message buffer overflow");
        arim_arq_send_remote(linebuf);
        arim_on_event(EV_ARQ_MSG_ERROR, 0);
        return 0;
    }
    memcpy(msg_in.data + msg_in_cnt, data, size);
    msg_in_cnt += size;
    msg_in.data[msg_in_cnt] = '\0';
    snprintf(linebuf, sizeof(linebuf),
        "ARQ: Message download reading %zu of %zu bytes", msg_in_cnt, msg_in.size);
    ui_queue_debug_log(linebuf);
    arim_copy_remote_call(remote_call, sizeof(remote_call));
    snprintf(linebuf, sizeof(linebuf),
        ">> [@] Message from %s %zu of %zu bytes",
            remote_call, msg_in_cnt, msg_in.size);
    ui_queue_traffic_log(linebuf);
    if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
        snprintf(linebuf, sizeof(linebuf),
             "[%s] >> [@] Message from %s %zu of %zu bytes",
                 util_timestamp(timestamp, sizeof(timestamp)),
                      remote_call, msg_in_cnt, msg_in.size);
    }
    ui_queue_data_in(linebuf);
    arim_on_event(EV_ARQ_MSG_RCV_FRAME, 0);
    if (msg_in_cnt >= msg_in.size) {
        /* if excess data, take most recent msg_in_size bytes */
        if (msg_in_cnt > msg_in.size) {
            memmove(msg_in.data, msg_in.data + (msg_in_cnt - msg_in.size),
                msg_in_cnt - msg_in.size);
            msg_in_cnt = msg_in.size;
        }
        /* verify checksum */
        check = ccitt_crc16((unsigned char *)msg_in.data, msg_in.size);
        if (msg_in.check != check) {
            snprintf(linebuf, sizeof(linebuf),
               "ARQ: Message download failed, bad checksum %04X",  check);
            ui_queue_debug_log(linebuf);
            snprintf(linebuf, sizeof(linebuf), "/ERROR Bad checksum");
            arim_arq_send_remote(linebuf);
            arim_on_event(EV_ARQ_MSG_ERROR, 0);
            return 0;
        }
        if (zoption) {
            zs.zalloc = Z_NULL;
            zs.zfree = Z_NULL;
            zs.opaque = Z_NULL;
            zs.avail_in = msg_in.size;
            zs.next_in = (Bytef *)msg_in.data;
            zs.avail_out = sizeof(zbuffer);
            zs.next_out = (Bytef *)zbuffer;
            zret = inflateInit(&zs);
            if (zret == Z_OK) {
                zret = inflate(&zs, Z_NO_FLUSH);
                inflateEnd(&zs);
                if (zret != Z_STREAM_END) {
                    snprintf(linebuf, sizeof(linebuf),
                        "ARQ: Message download failed, decompression error");
                    ui_queue_debug_log(linebuf);
                    snprintf(linebuf, sizeof(linebuf), "/ERROR Decompression failed");
                    arim_arq_send_remote(linebuf);
                    arim_on_event(EV_ARQ_MSG_ERROR, 0);
                    return 0;
                }
            } else {
                snprintf(linebuf, sizeof(linebuf),
                    "ARQ: Message download failed, decompression initialization error");
                ui_queue_debug_log(linebuf);
                snprintf(linebuf, sizeof(linebuf), "/ERROR Decompression failed");
                arim_arq_send_remote(linebuf);
                arim_on_event(EV_ARQ_MSG_ERROR, 0);
                return 0;
            }
            memcpy(msg_in.data, zbuffer, zs.total_out);
            msg_in.size = zs.total_out;
            msg_in.data[msg_in.size] = '\0';
            msg_in.check = ccitt_crc16((unsigned char *)msg_in.data, msg_in.size);
        }
        /* now store message to inbox */
        arim_copy_mycall(target_call, sizeof(target_call));
        snprintf(linebuf, sizeof(linebuf), "From %-10s %s To %-10s %04X",
                remote_call, util_date_timestamp(timestamp,
                        sizeof(timestamp)), target_call, msg_in.check);
        pthread_mutex_lock(&mutex_recents);
        cmdq_push(&g_recents_q, linebuf);
        pthread_mutex_unlock(&mutex_recents);
        if (!mbox_add_msg(MBOX_INBOX_FNAME,
                 remote_call, target_call, msg_in.check, msg_in.data)) {
            snprintf(linebuf, sizeof(linebuf),
                "ARQ: Message download failed, could not open inbox");
            ui_queue_debug_log(linebuf);
            snprintf(linebuf, sizeof(linebuf), "/ERROR Unable to save message");
            arim_arq_send_remote(linebuf);
            arim_on_event(EV_ARQ_MSG_ERROR, 0);
            return 0;
        }
        snprintf(linebuf, sizeof(linebuf),
            "ARQ: Saved %s message %zu bytes, checksum %04X",
               zoption ? "compressed" : "uncompressed",  msg_in_cnt, check);
        ui_queue_debug_log(linebuf);
        snprintf(linebuf, sizeof(linebuf),
            "/OK Message %zu %04X saved", msg_in_cnt, check);
        arim_arq_send_remote(linebuf);
        arim_on_event(EV_ARQ_MSG_RCV_DONE, 0);
    }
    return 1;
}

size_t arim_on_arq_cmd(const char *cmd, size_t size)
{
    /* called by datathread */
    static char buffer[MIN_MSG_BUF_SIZE*4];
    static size_t cnt = 0;
    char *p_check, *p_name, *p_path, *p_size;
    char *s, *e, *eol, respbuf[MIN_MSG_BUF_SIZE], cmdbuf[MIN_MSG_BUF_SIZE];
    char sendcr[TNC_ARQ_SENDCR_SIZE], linebuf[MAX_LOG_LINE_SIZE];
    char timestamp[MAX_TIMESTAMP_SIZE];
    int state, result, send_cr = 0;
    z_stream zs;
    char zbuffer[MIN_DATA_BUF_SIZE];
    int zret;

    state = arim_get_state();
    if (cmd && cmd[0] == '/') {
        if (size > sizeof(cmdbuf))
            return 0;
        memcpy(cmdbuf, cmd, size);
        cmdbuf[size] = '\0';
        /* check to see if CR must be sent */
        arim_copy_arq_sendcr(sendcr, sizeof(sendcr));
        if (!strncasecmp(sendcr, "TRUE", 4))
            send_cr = 1;
        /* strip EOL from command */
        eol = cmdbuf;
        while (*eol && *eol != '\n' && *eol != '\r')
            ++eol;
        if (*eol && *eol == '\r' && *(eol + 1) == '\n') {
            *eol++ = '\0';
            *eol++ = '\0';
        } else if (*eol && *eol == '\n') {
            *eol++ = '\0';
        } else {
            eol = 0;
        }
        /* print command to traffic monitor */
        snprintf(linebuf, sizeof(linebuf), ">> [@] %s", cmdbuf);
        ui_queue_traffic_log(linebuf);
        if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
            snprintf(linebuf, sizeof(linebuf), "[%s] >> [@] %s",
                util_timestamp(timestamp, sizeof(timestamp)), cmdbuf);
        }
        ui_queue_data_in(linebuf);
        if (!strncasecmp(cmdbuf, "/FPUT ", 6)) {
            /* remote station sends a file. If in a wait state already,
               abandon that transaction and respond to the /fput to avoid
               deadlock when commands are issued by both parties simultaneously */
            if (state == ST_ARQ_FILE_SEND_WAIT || state == ST_ARQ_MSG_SEND_WAIT) {
                arim_on_event(EV_ARQ_CANCEL_WAIT, 0);
                state = arim_get_state();
            }
            if (state == ST_ARQ_CONNECTED || state == ST_ARQ_FILE_RCV_WAIT) {
                zoption = 0;
                /* inbound file transfer, get parameters */
                p_size = p_check = p_path = NULL;
                s = cmdbuf + 6;
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
                    while (e > p_name && *e == ' ') {
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
                        arim_on_event(EV_ARQ_FILE_RCV, 0);
                        snprintf(file_in.name, sizeof(file_in.name), "%s", basename(p_name));
                        snprintf(file_in.path, sizeof(file_in.path), "%s",
                                     p_path ? p_path : DEFAULT_DOWNLOAD_DIR);
                        file_in.size = atoi(p_size);
                        if (1 != sscanf(p_check, "%x", &file_in.check))
                            file_in.check = 0;
                        file_in_cnt = 0;
                        snprintf(linebuf, sizeof(linebuf),
                                    "ARQ: File download %s to %s %zu %04X started",
                                        file_in.name, file_in.path, file_in.size, file_in.check);
                        ui_queue_debug_log(linebuf);
                        /* cache any data remaining */
                        if ((cmdbuf + size) > eol) {
                            arim_on_arq_file_rcv_frame(eol, size - (eol - cmdbuf));
                        }
                    } else {
                        snprintf(linebuf, sizeof(linebuf),
                            "ARQ: File download %s failed, bad size/checksum parameter", p_name);
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
            }
        } else if (!strncasecmp(cmdbuf, "/FGET ", 6)) {
            /* remote station requests a file. If in a wait state already,
               abandon that transaction and respond to the /fget to avoid
               deadlock when commands are issued by both parties simultaneously */
            if (state == ST_ARQ_FILE_SEND_WAIT ||
                state == ST_ARQ_FILE_RCV_WAIT ||
                state == ST_ARQ_MSG_SEND_WAIT) {
                arim_on_event(EV_ARQ_CANCEL_WAIT, 0);
                state = arim_get_state();
            }
            if (state == ST_ARQ_CONNECTED) {
                zoption = 0;
                p_path = NULL;
                /* empty outbound data buffer before handling file request */
                while (arim_get_buffer_cnt() > 0)
                    sleep(1);
                s = cmdbuf + 6;
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
                    while (e > p_name && *e == ' ') {
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
                    result = arim_arq_send_file(p_name, p_path, 0);
                    /* if successful returns 1, otherwise -1 or 0 */
                    if (result == 1)
                        arim_on_event(EV_ARQ_FILE_SEND_CMD, 0);
                    else
                        arim_on_event(EV_ARQ_FILE_ERROR, 0);
                } else {
                    snprintf(linebuf, sizeof(linebuf), "ARQ: Bad /FGET file name parameter");
                    ui_queue_debug_log(linebuf);
                    snprintf(linebuf, sizeof(linebuf), "/ERROR File not found");
                    arim_arq_send_remote(linebuf);
                    arim_on_event(EV_ARQ_FILE_ERROR, 0);
                }
            }
        } else if (!strncasecmp(cmdbuf, "/MPUT ", 6)) {
            /* remote station sends a message. If in a wait state already,
               abandon that transaction and respond to the /mput to avoid
               deadlock when commands are issued by both parties simultaneously */
            if (state == ST_ARQ_FILE_SEND_WAIT ||
                state == ST_ARQ_FILE_RCV_WAIT ||
                state == ST_ARQ_MSG_SEND_WAIT) {
                arim_on_event(EV_ARQ_CANCEL_WAIT, 0);
                state = arim_get_state();
            }
            if (state == ST_ARQ_CONNECTED) {
                zoption = 0;
                /* inbound message transfer, get parameters */
                p_size = p_check = 0;
                e = cmdbuf + 6;
                while (*e && *e == ' ')
                    ++e;
                if (*e && (e == strstr(e, "-z"))) {
                    zoption = 1;
                    e += 2;
                    while (*e && *e == ' ')
                        ++e;
                }
                p_name = e;
                if (*p_name) {
                    while (*e && *e != ' ') {
                        ++e;
                    }
                    /* at end of callsign */
                    if (*e) {
                        *e = '\0';
                        ++e;
                        /* at start of size */
                        p_size = e;
                        while (*e && *e != ' ') {
                            ++e;
                        }
                        *e = '\0';
                        ++e;
                        /* at start of check */
                        p_check = e;
                    }
                    if (p_size && p_check) {
                        arim_on_event(EV_ARQ_MSG_RCV, 0);
                        snprintf(msg_in.call, sizeof(msg_in.call), "%s", p_name);
                        msg_in.size = atoi(p_size);
                        if (1 != sscanf(p_check, "%x", &msg_in.check))
                            msg_in.check = 0;
                        msg_in_cnt = 0;
                        snprintf(linebuf, sizeof(linebuf),
                                    "ARQ: Message download %s %zu %04X started",
                                       msg_in.call , msg_in.size, msg_in.check);
                        ui_queue_debug_log(linebuf);
                        /* cache any data remaining */
                        if ((cmdbuf + size) > eol) {
                            arim_on_arq_msg_rcv_frame(eol, size - (eol - cmdbuf));
                        }
                    } else {
                        snprintf(linebuf, sizeof(linebuf),
                            "ARQ: Message download %s failed, bad size/checksum parameter", p_name);
                        ui_queue_debug_log(linebuf);
                        snprintf(linebuf, sizeof(linebuf), "/ERROR Bad parameters");
                        arim_arq_send_remote(linebuf);
                        arim_on_event(EV_ARQ_MSG_ERROR, 0);
                    }
                } else {
                    snprintf(linebuf, sizeof(linebuf),
                                "ARQ: Message download failed, bad /MPUT callsign");
                    ui_queue_debug_log(linebuf);
                    snprintf(linebuf, sizeof(linebuf), "/ERROR Bad callsign");
                    arim_arq_send_remote(linebuf);
                    arim_on_event(EV_ARQ_MSG_ERROR, 0);
                }
            }
        } else if (!strncasecmp(cmdbuf, "/ERROR ", 7)) {
            if (state == ST_ARQ_FILE_RCV_WAIT ||
                state == ST_ARQ_FILE_RCV ||
                state == ST_ARQ_FILE_SEND
            )
                arim_on_event(EV_ARQ_FILE_ERROR, 0);
            else if (state == ST_ARQ_MSG_RCV ||
                     state == ST_ARQ_MSG_SEND
            )
                arim_on_event(EV_ARQ_MSG_ERROR, 0);
        } else if (!strncasecmp(cmdbuf, "/OK ", 4)) {
            if (state == ST_ARQ_FILE_SEND) {
                arim_on_event(EV_ARQ_FILE_OK, 0);
            } else if (state == ST_ARQ_MSG_SEND) {
                arim_on_event(EV_ARQ_MSG_OK, 0);
                arim_copy_mycall(linebuf, sizeof(linebuf));
                if (zoption) {
                    zs.zalloc = Z_NULL;
                    zs.zfree = Z_NULL;
                    zs.opaque = Z_NULL;
                    zs.avail_in = msg_out.size;
                    zs.next_in = (Bytef *)msg_out.data;
                    zs.avail_out = sizeof(zbuffer);
                    zs.next_out = (Bytef *)zbuffer;
                    zret = inflateInit(&zs);
                    if (zret == Z_OK) {
                        zret = inflate(&zs, Z_NO_FLUSH);
                        inflateEnd(&zs);
                        if (zret != Z_STREAM_END) {
                            snprintf(linebuf, sizeof(linebuf),
                                "ARQ: Unable to save sent message, decompression failed");
                            ui_queue_debug_log(linebuf);
                        } else {
                            memcpy(msg_out.data, zbuffer, zs.total_out);
                            msg_out.size = zs.total_out;
                            msg_out.data[msg_out.size] = '\0';
                            msg_out.check = ccitt_crc16((unsigned char *)msg_out.data, msg_out.size);
                            /* store message to sent messages mailbox */
                            mbox_add_msg(MBOX_SENTBOX_FNAME,
                                linebuf, msg_out.call, msg_out.check, msg_out.data);
                        }
                    } else {
                        snprintf(linebuf, sizeof(linebuf),
                            "ARQ: Unable to save sent message, decompression init error");
                        ui_queue_debug_log(linebuf);
                    }
                }
            }
        } else if (state == ST_ARQ_CONNECTED) {
            /* empty outbound data buffer before handling query or command */
            while (arim_get_buffer_cnt() > 0)
                sleep(1);
            /* execute query or command, skip leading '/' character */
            result = cmdproc_query(cmdbuf + 1, respbuf, sizeof(respbuf));
            if (result == 1) {
                /* success, append response to data out buffer */
                size = strlen(respbuf);
                if ((cnt + size) >= sizeof(buffer)) {
                    /* overflow, reset buffer and return */
                    cnt = 0;
                    return cnt;
                }
                strncat(&buffer[cnt], respbuf, size);
                cnt += size;
            } else if (result == -1) {
                /* file access error */
                size = strlen("/ERROR File not found");
                if ((cnt + size) >= sizeof(buffer)) {
                    /* overflow, reset buffer and return */
                    cnt = 0;
                    return cnt;
                }
                strncat(&buffer[cnt], "/ERROR File not found", size);
                cnt += size;
            } else {
                /* no such query defined */
                size = strlen("/ERROR Unknown query");
                if ((cnt + size) >= sizeof(buffer)) {
                    /* overflow, reset buffer and return */
                    cnt = 0;
                    return cnt;
                }
                strncat(&buffer[cnt], "/ERROR Unknown query", size);
                cnt += size;
            }
        }
     } else if (cnt) {
        /* send data out one line at a time */
        e = buffer;
        if (*e) {
            while (*e && *e != '\n')
                ++e;
            if (*e) {
                *e = '\0';
                ++e;
            }
            if (send_cr)
                snprintf(respbuf, sizeof(respbuf), "%s\r\n", buffer);
            else
                snprintf(respbuf, sizeof(respbuf), "%s\n", buffer);
            ui_queue_data_out(respbuf);
            cnt -= (e - buffer);
            memmove(buffer, e, cnt + 1);
        }
    }
    return cnt;
}

int arim_on_arq_file_get()
{
    /* called from cmd processor when user issues /FGET at prompt */
    arim_on_event(EV_ARQ_FILE_RCV_WAIT, 0);
    return 1;
}

int arim_on_arq_file_put(const char *fn, const char *destdir, int use_zoption)
{
    /* called from cmd processor when user issues /FPUT at prompt */
    char linebuf[MAX_LOG_LINE_SIZE];
    char fpath[MAX_DIR_PATH_SIZE], dpath[MAX_DIR_PATH_SIZE];
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
    if (arim_arq_send_file(f, d, 1) == 1)
        /* if successful returns 1, otherwise -1 or 0 */
        arim_on_event(EV_ARQ_FILE_SEND_CMD, 0);
    return 1;
}

size_t arim_on_arq_resp(const char *resp, size_t size)
{
    static char buffer[MIN_MSG_BUF_SIZE*4];
    static size_t cnt = 0;
    char *e, linebuf[MIN_MSG_BUF_SIZE], timestamp[MAX_TIMESTAMP_SIZE];
    size_t i, len;

    if (resp) {
        /* append response to buffer */
        if ((cnt + size) >= sizeof(buffer)) {
            /* overflow, reset buffer and return */
            cnt = 0;
            return cnt;
        }
        strncat(&buffer[cnt], resp, size);
        cnt += size;
     } else if (cnt) {
        e = buffer;
        while (*e && *e != '\r' && *e != '\n')
            ++e;
        if (!*e)
            return cnt;
        if (*e == '\r' && *(e + 1) == '\n') {
            *e = '\0';
            ++e;
        }
        *e = '\0';
        ++e;
        len = strlen(buffer);
        for (i = 0; i < len; i++) {
            if (!isprint(buffer[i]))
                buffer[i] = ' ';
        }
        snprintf(linebuf, sizeof(linebuf), ">> [@] %s", buffer);
        ui_queue_traffic_log(linebuf);
        if (!strncasecmp(g_ui_settings.mon_timestamp, "TRUE", 4)) {
            snprintf(linebuf, sizeof(linebuf), "[%s] >> [@] %s",
                    util_timestamp(timestamp, sizeof(timestamp)), buffer);
        }
        ui_queue_data_in(linebuf);
        cnt -= (e - buffer);
        memmove(buffer, e, cnt + 1);
    }
    return cnt;
}

