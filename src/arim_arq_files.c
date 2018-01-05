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
#include "bufq.h"
#include "arim_proto.h"
#include "ui.h"
#include "ui_files.h"
#include "util.h"
#include "ui_dialog.h"
#include "log.h"
#include "zlib.h"
#include "datathread.h"
#include "arim_arq.h"

static int zoption;
static FILEQUEUEITEM file_in;
static FILEQUEUEITEM file_out;
static size_t file_in_cnt, file_out_cnt;

int arim_arq_files_send_dyn_file(const char *fn, const char *destdir, int is_local)
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

int arim_arq_files_send_file(const char *fn, const char *destdir, int is_local)
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

int arim_arq_files_on_send_cmd()
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

size_t arim_arq_files_on_send_buffer(size_t size)
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

int arim_arq_files_on_rcv_frame(const char *data, size_t size)
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

int arim_arq_files_on_fget(char *cmd, size_t size, char *eol)
{
    char *p_name, *p_path, *s, *e;
    char linebuf[MAX_LOG_LINE_SIZE];
    int result;

    zoption = 0;
    p_path = NULL;
    /* empty outbound data buffer before handling file request */
    while (arim_get_buffer_cnt() > 0)
        sleep(1);
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
        result = arim_arq_files_send_file(p_name, p_path, 0);
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
    return 1;
}

int arim_arq_files_on_fput(char *cmd, size_t size, char *eol)
{
    char *p_check, *p_name, *p_path, *p_size, *s, *e;
    char linebuf[MAX_LOG_LINE_SIZE];

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
            if ((cmd + size) > eol) {
                arim_arq_files_on_rcv_frame(eol, size - (eol - cmd));
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
    return 1;
}

int arim_arq_files_on_loc_fget()
{
    /* called from cmd processor when user issues /FGET at prompt */
    arim_on_event(EV_ARQ_FILE_RCV_WAIT, 0);
    return 1;
}

int arim_arq_files_on_loc_fput(const char *fn, const char *destdir, int use_zoption)
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
    if (arim_arq_files_send_file(f, d, 1) == 1)
        /* if successful returns 1, otherwise -1 or 0 */
        arim_on_event(EV_ARQ_FILE_SEND_CMD, 0);
    return 1;
}

