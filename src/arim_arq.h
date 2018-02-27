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

#ifndef _ARIM_ARQ_H_INCLUDED_
#define _ARIM_ARQ_H_INCLUDED_

#define ARQ_CLIENT_STN  0
#define ARQ_SERVER_STN  1

extern int arim_arq_send_conn_req(const char *repeats, const char *to_call);
extern int arim_arq_send_conn_req_ptt(int ptt_true);
extern int arim_arq_send_conn_req_pp(void);
extern int arim_arq_on_target(void);
extern int arim_arq_on_connected(void);
extern int arim_arq_send_disconn_req(void);
extern int arim_arq_on_disconnected(void);
extern int arim_arq_on_conn_timeout(void);
extern int arim_arq_on_conn_fail(void);
extern int arim_arq_on_data(char *data, size_t size);
extern size_t arim_arq_on_cmd(const char *cmd, size_t size);
extern size_t arim_arq_on_resp(const char *resp, size_t size);
extern size_t arim_arq_send_remote(const char *msg);
extern void arim_arq_cache_cmd(const char *cmd);
extern void arim_arq_run_cached_cmd(void);

#endif

