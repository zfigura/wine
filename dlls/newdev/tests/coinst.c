/*
 * Test class installer DLL
 *
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

#include <stdio.h>
#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "setupapi.h"

void (*ok_callback)(const char *file, int line, int condition, const char *msg);

static void ok_(const char *file, int line, int condition, const char *format, ...)
{
    va_list args;
    char buffer[300];
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    (*ok_callback)(file, line, condition, buffer);
    va_end(args);
}
#define ok(condition, ...) ok_(__FILE__, __LINE__, condition, __VA_ARGS__)

static const DWORD msg_list[] =
{
    DIF_SELECTBESTCOMPATDRV,
    DIF_ALLOW_INSTALL,
    DIF_INSTALLDEVICEFILES,
    DIF_REGISTER_COINSTALLERS,
    DIF_INSTALLINTERFACES,
    DIF_INSTALLDEVICE,
    DIF_NEWDEVICEWIZARD_FINISHINSTALL,
    DIF_DESTROYPRIVATEDATA,
};

static unsigned int msg_index;

DWORD WINAPI ClassInstall(DI_FUNCTION msg, HDEVINFO set, SP_DEVINFO_DATA *device)
{
    ok(msg == msg_list[msg_index], "%d: Expected message %#x, got %#x.\n", msg_index, msg_list[msg_index], msg);
    msg_index++;
    return ERROR_DI_DO_DEFAULT;
}
