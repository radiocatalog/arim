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

#ifndef _ARIM_ARQ_MSG_H_INCLUDED_
#define _ARIM_ARQ_MSG_H_INCLUDED_

extern int arim_arq_msg_on_mput(char *cmd, size_t size, char *eol);
extern int arim_arq_msg_on_send_msg(void);
extern int arim_arq_msg_on_send_cmd(const char *data, int use_zoption);
extern size_t arim_arq_msg_on_send_buffer(size_t size);
extern int arim_arq_msg_on_ok(void);
extern int arim_arq_msg_on_rcv_frame(const char *data, size_t size);

#endif

