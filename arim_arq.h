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

#ifndef _ARIM_ARQ_H_INCLUDED_
#define _ARIM_ARQ_H_INCLUDED_

extern int arim_send_arq_conn_req(const char *repeats, const char *to_call);
extern int arim_send_arq_conn_req_ptt(int ptt_true);
extern int arim_send_arq_conn_req_pp();
extern int arim_on_arq_target(void);
extern int arim_on_arq_connected();
extern int arim_send_arq_disconn_req();
extern int arim_on_arq_disconnected();
extern size_t arim_on_arq_cmd(const char *cmd, size_t size);
extern size_t arim_on_arq_resp(const char *resp, size_t size);
extern int arim_on_arq_file_get(void);
extern int arim_on_arq_file_put(const char *cmd, const char *destdir, int use_zoption);
extern int arim_on_arq_file_rcv_frame(const char *data, size_t size);
extern int arim_on_arq_file_send_cmd(void);
extern size_t arim_on_arq_file_send_buffer(size_t size);
extern int arim_on_arq_msg_rcv_frame(const char *data, size_t size);
extern int arim_on_arq_msg_put(const char *data, int use_zoption);
extern int arim_on_arq_msg_send_cmd(void);
extern size_t arim_on_arq_msg_send_buffer(size_t size);

#endif

