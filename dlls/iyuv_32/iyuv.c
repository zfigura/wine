/*
 * Copyright 2018 Zebediah Figura
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "windef.h"
#include "wingdi.h"
#include "mmsystem.h"
#include "vfw.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(iyuv);

LRESULT CALLBACK IYUV_DriverProc(DWORD_PTR id, HDRVR driver, UINT msg, LPARAM lparam1, LPARAM lparam2)
{
    TRACE("(%#lx, %p, %#x, %#lx, %#lx)\n", id, driver, msg, lparam1, lparam2);
    switch (msg) {
    default:
        FIXME("unsupported message %#x (%#lx, %#lx)\n", msg, lparam1, lparam2);
        return ICERR_UNSUPPORTED;
    }
}
