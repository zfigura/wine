/*
 * DLL for testing namespace providers
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

#include <stdarg.h>
#include <stdio.h>
#include "windef.h"
#include "winbase.h"
#include "ws2spi.h"

typedef void (*OK_CALLBACK)(int todo, const char *file, int line, int condition, const char *message);
static OK_CALLBACK ok_callback;

static void ok_(int todo, const char *file, int line, int condition, const char *msg, ...)
{
    static char buffer[2000];
    va_list valist;

    va_start(valist, msg);
    vsprintf(buffer, msg, valist);
    va_end(valist);

    ok_callback(todo, file, line, condition, buffer);
}
#define ok(condition, ...)           ok_(0, __FILE__, __LINE__, condition, __VA_ARGS__)
#define todo_wine_ok(condition, ...) ok_(1, __FILE__, __LINE__, condition, __VA_ARGS__)

void set_ok_callback(OK_CALLBACK func)
{
    ok_callback = func;
}

static GUID GUID_TEST_NAMESPACE = {0x1de3efaa,0xce19,0x4ec4,{0xad,0xae,0xd0,0xc8,0xd7,0x0f,0x3b,0xfe}};

int WINAPI NSPStartup(GUID *provider, NSP_ROUTINE *routines)
{
    ok(IsEqualGUID(provider, &GUID_TEST_NAMESPACE), "GUIDs did not match\n");

    return NO_ERROR;
}
