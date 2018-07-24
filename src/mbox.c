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

/* need to define _GNU_SOURCE for strptime() */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>
#include "main.h"
#include "ini.h"
#include "mbox.h"
#include "util.h"
#include "ui_msg.h"
#include "ui.h"

char mbox_dir_path[MAX_PATH_SIZE];

int mbox_purge(const char *fn, int days)
{
    FILE *mboxfp, *tempfp;
    int fd, numch;
    char *p, header[MAX_MBOX_HDR_SIZE], linebuf[MAX_MSG_LINE_SIZE];
    char fpath[MAX_PATH_SIZE*2], tempfn[MAX_PATH_SIZE*2];
    char month[16], day[8], timestamp[16], year[8], datetime[64], logbuf[MAX_LOG_LINE_SIZE];
    struct tm tm, *ptm;
    time_t hdr_time, cur_time;

    /* 0 days means "disabled" */
    if (days == 0)
       return 1;

    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "r");
    if (mboxfp == NULL)
        return 0;
    snprintf(tempfn, sizeof(tempfn), "%s/temp.mbox.XXXXXX", mbox_dir_path);
    fd = mkstemp(tempfn);
    if (fd == -1) {
        fclose(mboxfp);
        return 0;
    }
    tempfp = fdopen(fd, "r+");
    if (tempfp == NULL) {
        close(fd);
        fclose(mboxfp);
        return 0;
    }
    cur_time = time(NULL);
    if (!strncasecmp(g_ui_settings.utc_time, "TRUE", 4))
        ptm = gmtime(&cur_time);
    else
        ptm = localtime(&cur_time);
    cur_time = mktime(ptm);
    flockfile(mboxfp);
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p) {
        while (p && strncmp(p, "From ", 5)) {
            fprintf(tempfp, "%s", linebuf);
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
        }
        if (p) {
            snprintf(header, sizeof(header), "%s", p);
            /* skip to month */
            p = strtok(header, " ");
            if (p && strtok(NULL, " ") && strtok(NULL, " ")) {
                /* get the month */
                p = strtok(NULL, " ");
                if (p)
                    snprintf(month, sizeof(month), "%s", p);
                /* get day */
                p = strtok(NULL, " ");
                if (p)
                    snprintf(day, sizeof(day), "%s", p);
                /* get time */
                p = strtok(NULL, " ");
                if (p)
                    snprintf(timestamp, sizeof(timestamp), "%s", p);
                /* get year */
                p = strtok(NULL, " ");
                if (p) {
                    snprintf(year, sizeof(year), "%s", p);
                    snprintf(datetime, sizeof(datetime), "%s %s %s %s", day, month, year, timestamp);
                    memset(&tm, 0, sizeof(struct tm));
                    if (strptime(datetime, "%d %b %Y %H:%M:%S", &tm)) {
                        hdr_time = mktime(&tm);
                        if (hdr_time != -1) {
                            if (difftime(cur_time, hdr_time) > (double)(days*24*60*60)) {
                                /* message aged out, skip over it */
                                numch = snprintf(logbuf, sizeof(logbuf), "MBOX %s purged: [%s]", fn, linebuf);
                                if (numch >= sizeof(logbuf))
                                    ui_truncate_line(logbuf, sizeof(logbuf));
                                ui_queue_debug_log(logbuf);
                                p = fgets(linebuf, sizeof(linebuf), mboxfp);
                                while (p && strncmp(p, "From ", 5)) {
                                    p = fgets(linebuf, sizeof(linebuf), mboxfp);
                                }
                                continue;
                            }
                        }
                    }
                }
            }
            fprintf(tempfp, "%s", linebuf);
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
        }
    } /* while (p) */
    funlockfile(mboxfp);
    fclose(mboxfp);
    unlink(fpath);
    fclose(tempfp);
    rename(tempfn, fpath);
    return 1;
}

int mbox_add_msg(const char *fn, const char *fm_call, const char *to_call, int check, const char *msg)
{
    FILE *mboxfp;
    char timestamp[MAX_TIMESTAMP_SIZE];
    char fpath[MAX_PATH_SIZE*2];
    const char *p, *prev, *stop;

    stop = msg + strlen(msg);
    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "a");
    if (mboxfp != NULL) {
        flockfile(mboxfp);
        fprintf(mboxfp, "From %-10s %s To %-10s %04X ---\nFrom: %s\nTo: %s\n\n",
                fm_call, util_date_timestamp(timestamp, sizeof(timestamp)),
                        to_call, check, fm_call, to_call);
        p = msg;
        prev = p;
        fputc(*p++, mboxfp);
        while (*p) {
            if (*prev == '\n') {
                /* From line must be escaped by prefixing it with '>' char */
                while (*p && *p == '>')
                    ++p;
                if (*p == 'F' && (p + 1) < stop && *(p + 1) == 'r' &&
                                 (p + 2) < stop && *(p + 2) == 'o' &&
                                 (p + 3) < stop && *(p + 3) == 'm' &&
                                 (p + 4) < stop && *(p + 4) == ' ') {
                    fputc('>', mboxfp);
                }
                p = prev + 1;
            }
            fputc(*p, mboxfp);
            prev = p;
            ++p;
        }
        if (*prev == '\n')
            fputc('\n', mboxfp);
        else
            fprintf(mboxfp, "\n\n");
        funlockfile(mboxfp);
        fclose(mboxfp);
    } else {
        return 0;
    }
    msg_view_restart = 1;
    return 1;
}

int mbox_clear_flag(const char *fn, const char *hdr, int flag)
{
    FILE *mboxfp, *tempfp;
    int fd;
    char *p, linebuf[MAX_MSG_LINE_SIZE];
    char fpath[MAX_PATH_SIZE*2], tempfn[MAX_PATH_SIZE*2];

    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "r");
    if (mboxfp == NULL)
        return 0;
    snprintf(tempfn, sizeof(tempfn), "%s/temp.mbox.XXXXXX", mbox_dir_path);
    fd = mkstemp(tempfn);
    if (fd == -1) {
        fclose(mboxfp);
        return 0;
    }
    tempfp = fdopen(fd, "r+");
    if (tempfp == NULL) {
        close(fd);
        fclose(mboxfp);
        return 0;
    }
    flockfile(mboxfp);
    /* find matching header in file */
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p && strncmp(linebuf, hdr, strlen(hdr))) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    if (p) {
        /* got it, clear flag */
        while (*p && *p != '\n')
            ++p;
        if (*p && *(p - 4) == ' ') {
            switch(flag) {
            case 'R':
            case 'r':
                *(p - 3) = '-';
                break;
            case 'F':
            case 'f':
                *(p - 2) = '-';
                break;
            case 'S':
            case 's':
                *(p - 1) = '-';
                break;
            case '*':
                *(p - 3) = '-';
                *(p - 2) = '-';
                *(p - 1) = '-';
                break;
            }
        }
        fprintf(tempfp, "%s", linebuf);
    }
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    funlockfile(mboxfp);
    fclose(mboxfp);
    unlink(fpath);
    fclose(tempfp);
    rename(tempfn, fpath);
    return 1;
}

int mbox_get_msg_list(char *msgbuffer, size_t msgbufsize,
                         const char *fn, const char *to_call)
{
    FILE *mboxfp;
    size_t len, cnt = 0;
    int numlines = 0;
    char linebuf[MAX_MSG_LINE_SIZE], fpath[MAX_PATH_SIZE*2];
    char *p, test[TNC_MYCALL_SIZE+8], header[MAX_MBOX_HDR_SIZE];
    int i, numch;

    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "r");
    if (mboxfp == NULL)
        return 0;
    flockfile(mboxfp);
    memset(msgbuffer, 0, msgbufsize);
    /* print preamble */
    snprintf(header, sizeof(header), "Messages for %s:\n", to_call);
    len = strlen(header);
    if ((cnt + len) < msgbufsize) {
        strncat(msgbuffer, header, msgbufsize - cnt - 1);
        cnt += strlen(header);
        ++numlines;
    }
    /* find and copy messages addressed to 'to_call' */
    snprintf(test, sizeof(test), " TO %s ", to_call);
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p) {
        if (!strncmp(p, "From ", 5)) {
            /* message header, see if to_call matches */
            memset(header, 0, sizeof(header));
            len = strlen(linebuf);
            for (i = 0; i < len; i++)
                header[i] = toupper(linebuf[i]);
            if (strstr(header, test)) {
                /* print header into buffer */
                linebuf[59] = '\0';
                numch = snprintf(header, sizeof(header), "%3d %s\n", numlines, &linebuf[16]);
                len = strlen(header);
                if ((cnt + len) < msgbufsize) {
                    strncat(msgbuffer, header, msgbufsize - cnt - 1);
                    cnt += len;
                    ++numlines;
                }
            }
        }
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    if (numlines < 2) {
        snprintf(header, sizeof(header), "   No messages.\n");
        len = strlen(header);
        if ((cnt + len) < msgbufsize) {
            strncat(msgbuffer, header, msgbufsize - cnt - 1);
            cnt += strlen(header);
            ++numlines;
        }
    }
    snprintf(header, sizeof(header), "End\n");
    len = strlen(header);
    if ((cnt + len) < msgbufsize) {
        strncat(msgbuffer, header, msgbufsize - cnt - 1);
        cnt += strlen(header);
        ++numlines;
    }
    funlockfile(mboxfp);
    fclose(mboxfp);
    (void)numch; /* suppress 'assigned but not used' warning for dummy var */
    return numlines;
}

int mbox_get_headers_to(char headers[][MAX_MBOX_HDR_SIZE],
                            int max_hdrs, const char *fn, const char *to_call)
{
    FILE *mboxfp;
    char linebuf[MAX_MSG_LINE_SIZE], fpath[MAX_PATH_SIZE*2];
    char *p, test[TNC_MYCALL_SIZE+8], header[MAX_MBOX_HDR_SIZE];
    int i, numch, len, cnt = 0;

    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "r");
    if (mboxfp == NULL)
        return 0;
    flockfile(mboxfp);
    /* find and copy messages addressed to 'to_call' */
    snprintf(test, sizeof(test), " TO %s ", to_call);
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p) {
        if (!strncmp(p, "From ", 5)) {
            /* message header, see if to_call matches */
            memset(header, 0, sizeof(header));
            len = strlen(linebuf);
            for (i = 0; i < len; i++)
                header[i] = toupper(linebuf[i]);
            if (strstr(header, test)) {
                /* print header into buffer */
                numch = snprintf(headers[cnt++], MAX_MBOX_HDR_SIZE, "%s", linebuf);
                if (cnt == max_hdrs)
                    break;
            }
        }
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    funlockfile(mboxfp);
    fclose(mboxfp);
    (void)numch; /* suppress 'assigned but not used' warning for dummy var */
    return cnt;
}

int mbox_get_msg(char *msgbuffer, size_t msgbufsize,
                         const char *fn, const char *hdr)
{
    FILE *mboxfp;
    size_t len, cnt = 0;
    char *p, linebuf[MAX_MSG_LINE_SIZE], fpath[MAX_PATH_SIZE*2];

    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "r");
    if (mboxfp == NULL)
        return 0;
    flockfile(mboxfp);
    memset(msgbuffer, 0, msgbufsize);
    /* find matching header in file */
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p && strncmp(linebuf, hdr, strlen(hdr))) {
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    if (p) {
        /* got it, skip over headers */
        do {
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
            if (p && *p == '\n') {
                /* found blank line between headers and body, stop */
                break;
            }
        } while (p);
        /* now copy body of message into msg buffer */
        do {
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
            if (p) {
                len = strlen(linebuf);
                /* check for 'From ' char sequence escaped with '>' char(s) */
                while (*p && *p == '>')
                    ++p;
                if (p > linebuf && !strncmp(p, "From ", 5)) {
                    /* found one, unescape line by removing first '>' char */
                    if ((cnt + len - 1) < msgbufsize) {
                        strncat(msgbuffer, &linebuf[1], msgbufsize - cnt - 1);
                        cnt += len - 1;
                    }
                } else if (p && !strncmp(p, "From ", 5)) {
                    /* unescaped mbox header from next msg in file, stop */
                    break;
                } else {
                    /* write into msg buffer */
                    if ((cnt + len) < msgbufsize) {
                        strncat(msgbuffer, linebuf, msgbufsize - cnt - 1);
                        cnt += len;
                    }
                }
            }
        } while (p);
        /* remove empty line added when stored to mbox file */
        if (msgbuffer[cnt - 1] == '\n' && msgbuffer[cnt - 2] == '\n')
            msgbuffer[cnt - 2] = '\0';
    }
    funlockfile(mboxfp);
    fclose(mboxfp);
    return 1;
}

int mbox_delete_msg(const char *fn, const char *hdr)
{
    FILE *mboxfp, *tempfp;
    int fd;
    char *p, linebuf[MAX_MSG_LINE_SIZE];
    char fpath[MAX_PATH_SIZE*2], tempfn[MAX_PATH_SIZE*2];

    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "r");
    if (mboxfp == NULL)
        return 0;
    snprintf(tempfn, sizeof(tempfn), "%s/temp.mbox.XXXXXX", mbox_dir_path);
    fd = mkstemp(tempfn);
    if (fd == -1) {
        fclose(mboxfp);
        return 0;
    }
    tempfp = fdopen(fd, "r+");
    if (tempfp == NULL) {
        close(fd);
        fclose(mboxfp);
        return 0;
    }
    flockfile(mboxfp);
    /* find matching header in file */
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p && strncmp(linebuf, hdr, strlen(hdr))) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    if (p) {
        /* got it, skip over message */
        do {
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
            if (p && !strncmp(p, "From ", 5)) {
                /* found unescaped mbox header from next msg in file, done */
                fprintf(tempfp, "%s", linebuf);
                break;
            }
        } while (p);
    }
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    funlockfile(mboxfp);
    fclose(mboxfp);
    unlink(fpath);
    fclose(tempfp);
    rename(tempfn, fpath);
    return 1;
}

int mbox_save_msg(const char *fn, const char *hdr, const char *savefn)
{
    FILE *mboxfp, *savefp, *tempfp;
    char *p, *f, linebuf[MAX_MSG_LINE_SIZE];
    char fpath[MAX_PATH_SIZE*2], tempfn[MAX_PATH_SIZE*2];
    int fd;

    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "r");
    if (mboxfp == NULL)
        return 0;
    snprintf(tempfn, sizeof(tempfn), "%s/temp.mbox.XXXXXX", mbox_dir_path);
    fd = mkstemp(tempfn);
    if (fd == -1) {
        fclose(mboxfp);
        return 0;
    }
    tempfp = fdopen(fd, "r+");
    if (tempfp == NULL) {
        close(fd);
        fclose(mboxfp);
        return 0;
    }
    savefp = fopen(savefn, "w");
    if (savefp == NULL) {
        fclose(mboxfp);
        fclose(tempfp);
        return 0;
    }
    flockfile(mboxfp);
    /* find matching header in file */
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p && strncmp(linebuf, hdr, strlen(hdr))) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    if (p) {
        /* got it, set 'S' flag and write header to temp file */
        while (*p && *p != '\n')
            ++p;
        if (*p && *(p - 4) == ' ')
            *(p - 1) = 'S';
        fprintf(tempfp, "%s", linebuf);
        /* start printing to save file */
        fprintf(savefp, "%s", linebuf);
        do {
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
            if (p && !strncmp(p, "From ", 5)) {
                /* unescaped mbox header from next msg in file,
                   save it to temp file and stop */
                fprintf(tempfp, "%s", linebuf);
                break;
            } else {
                /* check for 'From ' char sequence escaped with '>' char(s) */
                f = linebuf;
                while (*f && *f == '>')
                    ++f;
                if (f > linebuf && !strncmp(f, "From ", 5))
                    /* found one, unescape line by removing first '>' char */
                    fprintf(savefp, "%s", &linebuf[1]);
                else
                    fprintf(savefp, "%s", linebuf);
            }
            /* write into temp file */
            fprintf(tempfp, "%s", linebuf);
        } while (p);
    }
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    funlockfile(mboxfp);
    fclose(mboxfp);
    fclose(savefp);
    fclose(tempfp);
    unlink(fpath);
    rename(tempfn, fpath);
    return 1;
}

int mbox_read_msg(char *msgbuffer, size_t msgbufsize,
                                    const char *fn, const char *hdr)
{
    FILE *mboxfp, *tempfp;
    size_t len, cnt = 0;
    int fd, numlines = 0;
    char *p, linebuf[MAX_MSG_LINE_SIZE];
    char fpath[MAX_PATH_SIZE*2], tempfn[MAX_PATH_SIZE*2];

    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "r");
    if (mboxfp == NULL)
        return 0;
    snprintf(tempfn, sizeof(tempfn), "%s/temp.mbox.XXXXXX", mbox_dir_path);
    fd = mkstemp(tempfn);
    if (fd == -1) {
        fclose(mboxfp);
        return 0;
    }
    tempfp = fdopen(fd, "r+");
    if (tempfp == NULL) {
        close(fd);
        fclose(mboxfp);
        return 0;
    }
    flockfile(mboxfp);
    memset(msgbuffer, 0, msgbufsize);
    /* find matching header in file */
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p && strncmp(linebuf, hdr, strlen(hdr))) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    if (p) {
        /* got it, set flag and write header to temp file */
        while (*p && *p != '\n')
            ++p;
        if (*p && *(p - 4) == ' ')
            *(p - 3) = 'R';
        fprintf(tempfp, "%s", linebuf);
        /* extract message into buffer */
        do {
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
            if (p) {
                len = strlen(linebuf);
                /* check for 'From ' char sequence escaped with '>' char(s) */
                while (*p && *p == '>')
                    ++p;
                if (p > linebuf && !strncmp(p, "From ", 5)) {
                    /* found one, unescape line by removing first '>' char */
                    if ((cnt + len - 1) < msgbufsize) {
                        strncat(msgbuffer, &linebuf[1],  msgbufsize - cnt - 1);
                        cnt += len - 1;
                        ++numlines;
                        /* convert CRLF line endings */
                        if (cnt > 1 && msgbuffer[cnt - 2] == '\r' && msgbuffer[cnt - 1] == '\n') {
                            msgbuffer[cnt - 2] = '\n';
                            msgbuffer[cnt - 1] = '\0';
                            --cnt;
                        }
                    }
                } else if (!strncmp(p, "From ", 5)) {
                    /* unescaped mbox header from next msg in file,
                       save it to temp file and stop */
                    fprintf(tempfp, "%s", linebuf);
                    break;
                } else {
                    if ((cnt + len) < msgbufsize) {
                        strncat(msgbuffer, linebuf, msgbufsize - cnt - 1);
                        cnt += len;
                        ++numlines;
                        /* convert CRLF line endings */
                        if (cnt > 1 && msgbuffer[cnt - 2] == '\r' && msgbuffer[cnt - 1] == '\n') {
                            msgbuffer[cnt - 2] = '\n';
                            msgbuffer[cnt - 1] = '\0';
                            --cnt;
                        }
                    }
                }
                /* write into temp file */
                fprintf(tempfp, "%s", linebuf);
            }
        } while (p);
        /* remove empty line added when stored to mbox file */
        if (msgbuffer[cnt - 1] == '\n' && msgbuffer[cnt - 2] == '\n')
            msgbuffer[cnt - 2] = '\0';
    }
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    funlockfile(mboxfp);
    fclose(mboxfp);
    fclose(tempfp);
    unlink(fpath);
    rename(tempfn, fpath);
    return numlines;
}

int mbox_fwd_msg(char *msgbuffer, size_t msgbufsize, const char *fn, const char *hdr)
{
    FILE *mboxfp, *tempfp;
    size_t len, cnt = 0;
    int fd;
    char *p, linebuf[MAX_MSG_LINE_SIZE];
    char fpath[MAX_PATH_SIZE*2], tempfn[MAX_PATH_SIZE*2];

    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "r");
    if (mboxfp == NULL)
        return 0;
    snprintf(tempfn, sizeof(tempfn), "%s/temp.mbox.XXXXXX", mbox_dir_path);
    fd = mkstemp(tempfn);
    if (fd == -1) {
        fclose(mboxfp);
        return 0;
    }
    tempfp = fdopen(fd, "r+");
    if (tempfp == NULL) {
        close(fd);
        fclose(mboxfp);
        return 0;
    }
    flockfile(mboxfp);
    memset(msgbuffer, 0, msgbufsize);
    /* find matching header in file */
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p && strncmp(linebuf, hdr, strlen(hdr))) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    if (p) {
        /* got it, set flag and write header to temp file */
        while (*p && *p != '\n')
            ++p;
        if (*p && *(p - 4) == ' ')
            *(p - 2) = 'F';
        fprintf(tempfp, "%s", linebuf);
        /* read headers */
        do {
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
            fprintf(tempfp, "%s", linebuf);
            if (p && *p == '\n') {
                /* found blank line between headers and body, stop */
                break;
            }
        } while (p);
        /* now copy body of message into msg buffer */
        do {
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
            if (p) {
                len = strlen(linebuf);
                /* check for 'From ' char sequence escaped with '>' char(s) */
                while (*p && *p == '>')
                    ++p;
                if (p > linebuf && !strncmp(p, "From ", 5)) {
                    /* found one, unescape line by removing first '>' char */
                    if ((cnt + len - 1) < msgbufsize) {
                        strncat(msgbuffer, &linebuf[1], msgbufsize - cnt - 1);
                        cnt += len - 1;
                    }
                } else if (!strncmp(p, "From ", 5)) {
                    /* unescaped mbox header from next msg in file,
                       save it to temp file and stop */
                    fprintf(tempfp, "%s", linebuf);
                    break;
                } else {
                    /* write into msg buffer */
                    if ((cnt + len) < msgbufsize) {
                        strncat(msgbuffer, linebuf, msgbufsize - cnt - 1);
                        cnt += len;
                    }
                }
                /* write into temp file */
                fprintf(tempfp, "%s", linebuf);
            }
        } while (p);
        /* remove empty line added when stored to mbox file */
        if (msgbuffer[cnt - 1] == '\n' && msgbuffer[cnt - 2] == '\n')
            msgbuffer[cnt - 2] = '\0';
    }
    /* now copy the rest of the mbox file into the temp file */
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    funlockfile(mboxfp);
    fclose(mboxfp);
    unlink(fpath);
    fclose(tempfp);
    rename(tempfn, fpath);
    return 1;
}

int mbox_send_msg(char *msgbuffer, size_t msgbufsize,
                char *to_call, size_t to_call_size, const char *fn, const char *hdr)
{
    FILE *mboxfp, *tempfp;
    size_t len, cnt = 0;
    int fd;
    char *p, *s, *e, linebuf[MAX_MSG_LINE_SIZE];
    char fpath[MAX_PATH_SIZE*2], tempfn[MAX_PATH_SIZE*2];

    snprintf(fpath, sizeof(fpath), "%s/%s", mbox_dir_path, fn);
    mboxfp = fopen(fpath, "r");
    if (mboxfp == NULL)
        return 0;
    snprintf(tempfn, sizeof(tempfn), "%s/temp.mbox.XXXXXX", mbox_dir_path);
    fd = mkstemp(tempfn);
    if (fd == -1) {
        fclose(mboxfp);
        return 0;
    }
    tempfp = fdopen(fd, "r+");
    if (tempfp == NULL) {
        close(fd);
        fclose(mboxfp);
        return 0;
    }
    flockfile(mboxfp);
    memset(to_call, 0, to_call_size);
    memset(msgbuffer, 0, msgbufsize);
    /* find matching header in file */
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p && strncmp(linebuf, hdr, strlen(hdr))) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    if (p) {
        /* got it, read headers and extract to: address */
        do {
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
            if (p && !strncmp(p, "To:", 3)) {
                s = p + 3;
                while (*s == ' ')
                    ++s;
                e = s;
                while (*e && *e != ' ' && *e != '\n')
                    ++e;
                *e = '\0';
                snprintf(to_call, to_call_size, "%s", s);
            } else if (p && *p == '\n') {
                /* found blank line between headers and body, stop */
                break;
            }
        } while (p);
        /* now copy body of message into msg buffer but not into temp file */
        do {
            p = fgets(linebuf, sizeof(linebuf), mboxfp);
            if (p) {
                len = strlen(linebuf);
                /* check for 'From ' char sequence escaped with '>' char(s) */
                while (*p && *p == '>')
                    ++p;
                if (p > linebuf && !strncmp(p, "From ", 5)) {
                    /* found one, unescape line by removing first '>' char */
                    if ((cnt + len - 1) < msgbufsize) {
                        strncat(msgbuffer, &linebuf[1], msgbufsize - cnt - 1);
                        cnt += len - 1;
                    }
                } else if (p && !strncmp(p, "From ", 5)) {
                    /* unescaped mbox header from next msg in file,
                       save it to temp file and stop */
                    fprintf(tempfp, "%s", linebuf);
                    break;
                } else {
                    /* write into msg buffer */
                    if ((cnt + len) < msgbufsize) {
                        strncat(msgbuffer, linebuf, msgbufsize - cnt - 1);
                        cnt += len;
                    }
                }
            }
        } while (p);
        /* remove empty line added when stored to mbox file */
        if (msgbuffer[cnt - 1] == '\n' && msgbuffer[cnt - 2] == '\n')
            msgbuffer[cnt - 2] = '\0';
    }
    /* now copy the rest of the mbox file into the temp file */
    p = fgets(linebuf, sizeof(linebuf), mboxfp);
    while (p) {
        /* write into temp file */
        fprintf(tempfp, "%s", linebuf);
        p = fgets(linebuf, sizeof(linebuf), mboxfp);
    }
    funlockfile(mboxfp);
    fclose(mboxfp);
    unlink(fpath);
    fclose(tempfp);
    rename(tempfn, fpath);
    return 1;
}

int mbox_init()
{
    FILE *tempfp;
    char file_path[MAX_PATH_SIZE*2];
#ifndef PORTABLE_BIN
    FILE *srcfp;
    char *p, linebuf[MAX_MSG_LINE_SIZE];
#endif
    int result;

    snprintf(mbox_dir_path, sizeof(mbox_dir_path), "%s", g_arim_path);
    snprintf(file_path, sizeof(file_path), "%s/%s", mbox_dir_path, MBOX_INBOX_FNAME);
    result = access(file_path, F_OK);
    if (result != 0) {
        if (errno == ENOENT) {
            tempfp = fopen(file_path, "a");
            if (tempfp == NULL) {
                return 0;
            } else {
#ifndef PORTABLE_BIN
                snprintf(file_path, sizeof(file_path), ARIM_FILESDIR "/" MBOX_INBOX_FNAME);
                srcfp = fopen(file_path, "r");
                if (srcfp == NULL) {
                    fclose(tempfp);
                    return 0;
                }
                p = fgets(linebuf, sizeof(linebuf), srcfp);
                while (p) {
                    fprintf(tempfp, "%s", linebuf);
                    p = fgets(linebuf, sizeof(linebuf), srcfp);
                }
                fclose(srcfp);
#endif
                fclose(tempfp);
            }
        } else {
            return 0;
        }
    }
    snprintf(file_path, sizeof(file_path), "%s/%s", mbox_dir_path, MBOX_OUTBOX_FNAME);
    result = access(file_path, F_OK);
    if (result != 0) {
        if (errno == ENOENT) {
            tempfp = fopen(file_path, "a");
            if (tempfp == NULL)
                return 0;
            else
                fclose(tempfp);
        } else {
            return 0;
        }
    }
    snprintf(file_path, sizeof(file_path), "%s/%s", mbox_dir_path, MBOX_SENTBOX_FNAME);
    result = access(file_path, F_OK);
    if (result != 0) {
        if (errno == ENOENT) {
            tempfp = fopen(file_path, "a");
            if (tempfp == NULL)
                return 0;
            else
                fclose(tempfp);
        } else {
            return 0;
        }
    }
    return 1;
}

