/*
 * Unit tests for newdev.dll
 *
 * Copyright 2019 Zebediah Figura
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

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "newdev.h"
#include "wine/test.h"

static void load_resource(const char *name, const char *filename)
{
    DWORD written;
    HANDLE file;
    HRSRC res;
    void *ptr;

    file = CreateFileA(filename, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "file creation failed, at %s, error %d\n", filename, GetLastError());

    res = FindResourceA(NULL, name, "TESTDLL");
    ok( res != 0, "couldn't find resource\n" );
    ptr = LockResource( LoadResource( GetModuleHandleA(NULL), res ));
    WriteFile( file, ptr, SizeofResource( GetModuleHandleA(NULL), res ), &written, NULL );
    ok( written == SizeofResource( GetModuleHandleA(NULL), res ), "couldn't write resource\n" );
    CloseHandle( file );
}

static void ok_callback(const char *file, int line, int condition, const char *msg)
{
    ok_(file, line)(condition, msg);
}

static const GUID device_class = {0x12344321};

static void test_update_driver(void)
{
    static const char inf_data[] = "[Version]\n"
            "Signature=\"$Chicago$\"\n"
            "ClassGuid={12344321-0000-0000-0000-000000000000}\n"
            "[Manufacturer]\n"
            "mfg1=mfg1_key\n"
            "[mfg1_key]\n"
            "desc1=dev1,bogus_hardware_id\n"
            "[mfg1_key]\n"
            "desc1=dev1,bogus_hardware_id\n"
            "[dev1]\n"
            "[dev1.Services]\n"
            "AddService=,2\n";
    static const char hardware_id[] = "bogus_hardware_id\0";
    static const char regdata[] = "winetest_coinst.dll\0";
    void (**pok_callback)(const char *file, int line, int condition, const char *msg);
    SP_DEVINFO_DATA device = {sizeof(device)};
    char inf_path[MAX_PATH];
    BOOL ret, reboot;
    HMODULE coinst;
    HKEY class_key;
    HDEVINFO set;
    HANDLE file;
    DWORD size;
    LONG res;

    GetTempPathA(MAX_PATH, inf_path);
    strcat(inf_path, "newdev_test.inf");
    file = CreateFileA(inf_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "Failed to create %s, error %u.\n", inf_path, GetLastError());
    ret = WriteFile(file, inf_data, strlen(inf_data), &size, NULL);
    ok(ret && size == strlen(inf_data), "Failed to write INF file, error %u.\n", GetLastError());
    CloseHandle(file);

    load_resource("coinst.dll", "C:\\windows\\system32\\winetest_coinst.dll");

    coinst = LoadLibraryA("winetest_coinst.dll");
    pok_callback = (void *)GetProcAddress(coinst, "ok_callback");
    *pok_callback = ok_callback;

    res = RegCreateKeyA(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Control\\Class"
            "\\{12344321-0000-0000-0000-000000000000}", &class_key);
    ok(!res, "Failed to create class key, error %u.\n", res);
    res = RegSetValueExA(class_key, "Installer32", 0, REG_SZ, (BYTE *)regdata, sizeof(regdata));
    ok(!res, "Failed to set registry value, error %u.\n", res);

    set = SetupDiCreateDeviceInfoList(&device_class, NULL);
    ok(set != INVALID_HANDLE_VALUE, "Failed to create device list, error %#x.\n", GetLastError());

    ret = SetupDiCreateDeviceInfoA(set, "root\\bogus\\0000", &device_class, NULL, NULL, 0, &device);
    ok(ret, "Failed to create device, error %#x.\n", GetLastError());

    ret = SetupDiSetDeviceRegistryPropertyA(set, &device, SPDRP_HARDWAREID, (const BYTE *)hardware_id, sizeof(hardware_id));
    ok(ret, "Failed to set hardware ID, error %#x.\n", GetLastError());

    ret = SetupDiRegisterDeviceInfo(set, &device, 0, NULL, NULL, NULL);
    ok(ret, "Failed to register device, error %#x.\n", GetLastError());

    ret = UpdateDriverForPlugAndPlayDevicesA(NULL, hardware_id, inf_path, 0, &reboot);
    ok(ret, "UpdateDriverForPlugAndPlayDevices() failed, error %#x.\n", GetLastError());

    ret = SetupDiRemoveDevice(set, &device);
    ok(ret, "Failed to remove device, error %#x.\n", GetLastError());

    SetupDiDestroyDeviceInfoList(set);

    FreeLibrary(coinst);

    ret = DeleteFileA("C:\\windows\\system32\\winetest_coinst.dll");
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
    ret = DeleteFileA(inf_path);
    ok(ret, "Failed to delete %s, error %u.\n", inf_path, GetLastError());
    res = RegDeleteKeyA(class_key, "");
    ok(!res, "Failed to delete class key, error %u.\n", res);
    RegCloseKey(class_key);
}

START_TEST(newdev)
{
    test_update_driver();
}
