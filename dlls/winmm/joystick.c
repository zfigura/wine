/*
 * joystick functions
 *
 * Copyright 1997 Andreas Mohr
 * Copyright 2000 Wolfgang Schwotzer
 * Copyright 2000 Eric Pouech
 * Copyright 2020 Zebediah Figura for CodeWeavers
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "windef.h"
#include "winbase.h"
#include "mmsystem.h"
#include "wingdi.h"
#include "winuser.h"
#include "winnls.h"
#include "winreg.h"
#include "setupapi.h"
#include "ddk/hidsdi.h"
#include "initguid.h"
#include "ddk/hidclass.h"

#include "mmddk.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(winmm);

#define DECLARE_CRITICAL_SECTION(cs) \
    static CRITICAL_SECTION cs; \
    static CRITICAL_SECTION_DEBUG cs##_debug = \
    { 0, 0, &cs, { &cs##_debug.ProcessLocksList, &cs##_debug.ProcessLocksList }, \
      0, 0, { (DWORD_PTR)(__FILE__ ": " # cs) }}; \
    static CRITICAL_SECTION cs = { &cs##_debug, -1, 0, 0, 0, 0 };

DECLARE_CRITICAL_SECTION(joystick_cs);

#define MAX_JOYSTICKS 16
#define MAX_BUTTONS 32
#define MAX_AXES 6

#define JOY_PERIOD_MIN	(10)	/* min Capture time period */
#define JOY_PERIOD_MAX	(1000)	/* max Capture time period */

struct joystick
{
    HANDLE file;
    WCHAR *path;
    PHIDP_PREPARSED_DATA data;
    UINT button_count;
    USHORT report_length;
    char *report;

    struct axis
    {
        USAGE page, usage;
        LONG min, range;
    } axes[MAX_AXES];

    BOOL has_pov;

    HWND hCapture;
    UINT_PTR wTimer;
    BOOL bChanged;
    DWORD threshold;
    JOYINFO ji;
};

enum axis_index
{
    AXIS_X = 0,
    AXIS_Y = 1,
    AXIS_Z = 2,
    AXIS_R = 3,
    AXIS_U = 4,
    AXIS_V = 5,
};

static struct joystick joysticks[MAX_JOYSTICKS];

static void map_axis(struct joystick *joystick, const HIDP_VALUE_CAPS *caps, USAGE usage)
{
    enum axis_index axis;

    if (caps->UsagePage == HID_USAGE_PAGE_GENERIC)
    {
        switch (usage)
        {
            case HID_USAGE_GENERIC_X:
            case HID_USAGE_GENERIC_WHEEL:
                axis = AXIS_X;
                break;

            case HID_USAGE_GENERIC_Y:
                axis = AXIS_Y;
                break;

            case HID_USAGE_GENERIC_Z:
            case HID_USAGE_GENERIC_SLIDER:
                axis = AXIS_Z;
                break;

            case HID_USAGE_GENERIC_RX:
                axis = AXIS_U;
                break;

            case HID_USAGE_GENERIC_RY:
                axis = AXIS_V;
                break;

            case HID_USAGE_GENERIC_RZ:
            case HID_USAGE_GENERIC_DIAL:
                axis = AXIS_R;
                break;

            case HID_USAGE_GENERIC_HATSWITCH:
                joystick->has_pov = TRUE;
                return;

            default:
                FIXME("Not mapping generic usage %#02x.\n", usage);
                return;
        }
    }
    else if (caps->UsagePage == HID_USAGE_PAGE_SIMULATION)
    {
        switch (usage)
        {
            case HID_USAGE_SIMULATION_STEERING:
                axis = AXIS_X;
                break;

            case HID_USAGE_SIMULATION_ACCELLERATOR:
                axis = AXIS_Y;
                break;

            case HID_USAGE_SIMULATION_THROTTLE:
            case HID_USAGE_SIMULATION_BRAKE:
                axis = AXIS_Z;
                break;

            case HID_USAGE_SIMULATION_RUDDER:
                axis = AXIS_R;
                break;

            default:
                FIXME("Not mapping simulation usage %#02x.\n", usage);
                return;
        }
    }
    else
    {
        FIXME("Not mapping usage %#02x/%#02x.\n", caps->UsagePage, usage);
        return;
    }

    if (joystick->axes[axis].page)
    {
        FIXME("Usage %#02x/%#02x is already mapped to axis %u; ignoring %#x/%#x.\n",
                joystick->axes[axis].page, joystick->axes[axis].usage, axis, caps->UsagePage, usage);
        return;
    }

    joystick->axes[axis].page = caps->UsagePage;
    joystick->axes[axis].usage = usage;
    joystick->axes[axis].min = caps->PhysicalMin;
    joystick->axes[axis].range = caps->PhysicalMax - caps->PhysicalMin;
}

static BOOL is_joystick(USAGE page, USAGE usage)
{
    if (page != HID_USAGE_PAGE_GENERIC)
        return FALSE;
    return usage == HID_USAGE_GENERIC_JOYSTICK
            || usage == HID_USAGE_GENERIC_GAMEPAD
            || usage == HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER;
}

static void add_joystick(HDEVINFO set, SP_DEVICE_INTERFACE_DATA *iface)
{
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
    HIDP_BUTTON_CAPS button_caps[MAX_BUTTONS];
    HIDP_VALUE_CAPS *value_caps;
    struct joystick *joystick;
    unsigned int i, j;
    HIDP_CAPS caps;
    USHORT count;
    DWORD size;

    SetupDiGetDeviceInterfaceDetailW(set, iface, NULL, 0, &size, NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        ERR("Failed to get device path, error %#x.\n", GetLastError());
        return;
    }

    if (!(detail = malloc(size)))
    {
        ERR("Failed to allocate memory.\n");
        return;
    }

    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    SetupDiGetDeviceInterfaceDetailW(set, iface, detail, size, NULL, NULL);

    TRACE("Found HID device %s.\n", debugstr_w(detail->DevicePath));

    for (i = 0; i < MAX_JOYSTICKS; ++i)
    {
        if (joysticks[i].path && !wcscmp(joysticks[i].path, detail->DevicePath))
        {
            free(detail);
            return;
        }
    }

    for (i = 0; i < MAX_JOYSTICKS; ++i)
    {
        if (!joysticks[i].path)
        {
            joystick = &joysticks[i];
            break;
        }
    }

    if (i == MAX_JOYSTICKS)
    {
        WARN("No free slots for %s.\n", debugstr_w(detail->DevicePath));
        return;
    }

    joystick->path = wcsdup(detail->DevicePath);
    free(detail);
    if (!joystick->path)
        return;

    if ((joystick->file = CreateFileW(joystick->path, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE)
    {
        WARN("Failed to open %s, error %u.\n", debugstr_w(joystick->path), GetLastError());
        free(joystick->path);
        joystick->path = NULL;
        return;
    }

    if (!HidD_GetPreparsedData(joystick->file, &joystick->data))
        ERR("Failed to get preparsed data.\n");

    HidP_GetCaps(joystick->data, &caps);

    if (!is_joystick(caps.UsagePage, caps.Usage))
    {
        TRACE("%s is not a joystick (page %#x, usage %#x).\n",
                debugstr_w(joystick->path), caps.UsagePage, caps.Usage);
        CloseHandle(joystick->file);
        free(joystick->path);
        joystick->path = NULL;
        return;
    }

    joystick->report_length = caps.InputReportByteLength;
    if (!(joystick->report = malloc(joystick->report_length)))
    {
        free(joystick->path);
        joystick->path = NULL;
        CloseHandle(joystick->file);
        return;
    }

    count = ARRAY_SIZE(button_caps);
    HidP_GetButtonCaps(HidP_Input, button_caps, &count, joystick->data);

    joystick->button_count = 0;
    for (i = 0; i < count; ++i)
    {
        if (button_caps[i].UsagePage == HID_USAGE_PAGE_BUTTON)
            joystick->button_count = max(joystick->button_count,
                    button_caps[i].IsRange ? button_caps[i].Range.UsageMax : button_caps[i].NotRange.Usage);
        else
            WARN("Skipping button with usage page %#x.\n", button_caps[i].UsagePage);
    }
    joystick->button_count = min(joystick->button_count, MAX_BUTTONS);

    count = caps.NumberInputValueCaps;
    value_caps = malloc(count * sizeof(*value_caps));
    HidP_GetValueCaps(HidP_Input, value_caps, &count, joystick->data);

    /* FIXME: The following is taken roughly from the old implementation of
     * winejoystick.drv, but does not seem to match Windows 10. More testing is
     * needed. */
    for (i = 0; i < count; ++i)
    {
        if (value_caps[i].IsRange)
        {
            for (j = value_caps[i].Range.UsageMin; j <= value_caps[i].Range.UsageMax; ++j)
                map_axis(joystick, &value_caps[i], j);
        }
        else
        {
            map_axis(joystick, &value_caps[i], value_caps[i].NotRange.Usage);
        }
    }

    TRACE("Added joystick %u.\n", joystick - joysticks);
}

static void remove_joystick(struct joystick *joystick)
{
    free(joystick->report);
    free(joystick->path);
    joystick->path = NULL;
    joystick->has_pov = FALSE;
    CloseHandle(joystick->file);
}

static void find_joysticks(void)
{
    SP_DEVICE_INTERFACE_DATA iface = {sizeof(iface)};
    static ULONGLONG last_check;
    HDEVINFO set;
    DWORD idx;

    if (GetTickCount64() - last_check < 2000)
        return;
    last_check = GetTickCount64();

    set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_HID, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

    for (idx = 0; SetupDiEnumDeviceInterfaces(set, NULL, &GUID_DEVINTERFACE_HID, idx, &iface); ++idx)
    {
        add_joystick(set, &iface);
    }

    SetupDiDestroyDeviceInfoList(set);
}

/**************************************************************************
 * 				JOY_Timer		[internal]
 */
static	void	CALLBACK	JOY_Timer(HWND hWnd, UINT wMsg, UINT_PTR wTimer, DWORD dwTime)
{
    struct joystick *joy;
    int			i;
    MMRESULT		res;
    JOYINFO		ji;
    LONG		pos;
    unsigned 		buttonChange;

    EnterCriticalSection(&joystick_cs);

    for (i = 0; i < MAX_JOYSTICKS; ++i)
    {
        joy = &joysticks[i];

	if (joy->hCapture != hWnd) continue;

	res = joyGetPos(i, &ji);
	if (res != JOYERR_NOERROR) {
	    WARN("joyGetPos failed: %08x\n", res);
	    continue;
	}

	pos = MAKELONG(ji.wXpos, ji.wYpos);

	if (!joy->bChanged ||
	    abs(joy->ji.wXpos - ji.wXpos) > joy->threshold ||
	    abs(joy->ji.wYpos - ji.wYpos) > joy->threshold) {
	    SendMessageA(joy->hCapture, MM_JOY1MOVE + i, ji.wButtons, pos);
	    joy->ji.wXpos = ji.wXpos;
	    joy->ji.wYpos = ji.wYpos;
	}
	if (!joy->bChanged ||
	    abs(joy->ji.wZpos - ji.wZpos) > joy->threshold) {
	    SendMessageA(joy->hCapture, MM_JOY1ZMOVE + i, ji.wButtons, pos);
	    joy->ji.wZpos = ji.wZpos;
	}
	if ((buttonChange = joy->ji.wButtons ^ ji.wButtons) != 0) {
	    if (ji.wButtons & buttonChange)
		SendMessageA(joy->hCapture, MM_JOY1BUTTONDOWN + i,
			     (buttonChange << 8) | (ji.wButtons & buttonChange), pos);
	    if (joy->ji.wButtons & buttonChange)
		SendMessageA(joy->hCapture, MM_JOY1BUTTONUP + i,
			     (buttonChange << 8) | (joy->ji.wButtons & buttonChange), pos);
	    joy->ji.wButtons = ji.wButtons;
	}
    }

    LeaveCriticalSection(&joystick_cs);
}

/**************************************************************************
 *                              joyConfigChanged        [WINMM.@]
 */
MMRESULT WINAPI joyConfigChanged(DWORD flags)
{
    FIXME("(%x) - stub\n", flags);

    if (flags)
	return JOYERR_PARMS;

    return JOYERR_NOERROR;
}

/**************************************************************************
 * 				joyGetNumDevs		[WINMM.@]
 */
UINT WINAPI DECLSPEC_HOTPATCH joyGetNumDevs(void)
{
    return 16;
}

/**************************************************************************
 * 				joyGetDevCapsW		[WINMM.@]
 */
MMRESULT WINAPI DECLSPEC_HOTPATCH joyGetDevCapsW(UINT_PTR id, JOYCAPSW *caps, UINT size)
{
    struct joystick *joystick;
    HIDD_ATTRIBUTES attr;
    unsigned int i;

    TRACE("id %Iu, caps %p, size %u.\n", id, caps, size);

    if (id >= MAX_JOYSTICKS)
        return JOYERR_PARMS;

    EnterCriticalSection(&joystick_cs);

    find_joysticks();

    joystick = &joysticks[id];
    if (!joystick->path)
    {
        LeaveCriticalSection(&joystick_cs);
        return JOYERR_PARMS;
    }

    attr.Size = sizeof(HIDD_ATTRIBUTES);
    if (!HidD_GetAttributes(joystick->file, &attr))
        ERR("Failed to get attributes, error %u.\n", GetLastError());

    caps->wMid = attr.VendorID;
    caps->wPid = attr.ProductID;
    wcscpy(caps->szPname, L"Wine HID joystick driver");
    caps->wXmin = caps->wYmin = caps->wZmin = 0;
    caps->wXmax = caps->wYmax = caps->wZmax = 65535;
    caps->wNumButtons = joystick->button_count;
    caps->wPeriodMin = JOY_PERIOD_MIN;
    caps->wPeriodMax = JOY_PERIOD_MAX;

    if (size >= sizeof(JOYCAPSW))
    {
        caps->wRmin = caps->wUmin = caps->wVmin = 0;
        caps->wRmax = caps->wUmax = caps->wVmax = 65535;

        caps->wCaps = 0;
        if (joystick->axes[AXIS_Z].page)
            caps->wCaps |= JOYCAPS_HASZ;
        if (joystick->axes[AXIS_R].page)
            caps->wCaps |= JOYCAPS_HASR;
        if (joystick->axes[AXIS_U].page)
            caps->wCaps |= JOYCAPS_HASU;
        if (joystick->axes[AXIS_V].page)
            caps->wCaps |= JOYCAPS_HASV;
        if (joystick->has_pov)
            caps->wCaps |= JOYCAPS_HASPOV | JOYCAPS_POV4DIR;
        /* FIXME: What translates to JOYCAPS_POVCTS? */

        caps->wMaxAxes = MAX_AXES;
        caps->wNumAxes = 0;
        for (i = 0; i < MAX_AXES; ++i)
        {
            if (joystick->axes[i].page)
                ++caps->wNumAxes;
        }
        caps->wMaxButtons = MAX_BUTTONS;
        caps->szRegKey[0] = 0;
        caps->szOEMVxD[0] = 0;
    }

    LeaveCriticalSection(&joystick_cs);

    return JOYERR_NOERROR;
}

/**************************************************************************
 * 				joyGetDevCapsA		[WINMM.@]
 */
MMRESULT WINAPI DECLSPEC_HOTPATCH joyGetDevCapsA(UINT_PTR wID, LPJOYCAPSA lpCaps, UINT wSize)
{
    JOYCAPSW	jcw;
    MMRESULT	ret;

    if (lpCaps == NULL) return MMSYSERR_INVALPARAM;

    ret = joyGetDevCapsW(wID, &jcw, sizeof(jcw));

    if (ret == JOYERR_NOERROR)
    {
        lpCaps->wMid = jcw.wMid;
        lpCaps->wPid = jcw.wPid;
        WideCharToMultiByte( CP_ACP, 0, jcw.szPname, -1, lpCaps->szPname,
                             sizeof(lpCaps->szPname), NULL, NULL );
        lpCaps->wXmin = jcw.wXmin;
        lpCaps->wXmax = jcw.wXmax;
        lpCaps->wYmin = jcw.wYmin;
        lpCaps->wYmax = jcw.wYmax;
        lpCaps->wZmin = jcw.wZmin;
        lpCaps->wZmax = jcw.wZmax;
        lpCaps->wNumButtons = jcw.wNumButtons;
        lpCaps->wPeriodMin = jcw.wPeriodMin;
        lpCaps->wPeriodMax = jcw.wPeriodMax;

        if (wSize >= sizeof(JOYCAPSA)) { /* Win95 extensions ? */
            lpCaps->wRmin = jcw.wRmin;
            lpCaps->wRmax = jcw.wRmax;
            lpCaps->wUmin = jcw.wUmin;
            lpCaps->wUmax = jcw.wUmax;
            lpCaps->wVmin = jcw.wVmin;
            lpCaps->wVmax = jcw.wVmax;
            lpCaps->wCaps = jcw.wCaps;
            lpCaps->wMaxAxes = jcw.wMaxAxes;
            lpCaps->wNumAxes = jcw.wNumAxes;
            lpCaps->wMaxButtons = jcw.wMaxButtons;
            WideCharToMultiByte( CP_ACP, 0, jcw.szRegKey, -1, lpCaps->szRegKey,
                                 sizeof(lpCaps->szRegKey), NULL, NULL );
            WideCharToMultiByte( CP_ACP, 0, jcw.szOEMVxD, -1, lpCaps->szOEMVxD,
                                 sizeof(lpCaps->szOEMVxD), NULL, NULL );
        }
    }

    return ret;
}

static DWORD get_axis_value(struct joystick *joystick, enum axis_index axis)
{
    uint64_t value64;
    NTSTATUS status;
    LONG value;

    if (!joystick->axes[axis].page)
        return 0;

    if ((status = HidP_GetScaledUsageValue(HidP_Input, joystick->axes[axis].page,
            0, joystick->axes[axis].usage, &value, joystick->data, joystick->report,
            joystick->report_length)) != HIDP_STATUS_SUCCESS)
    {
        ERR("Failed to get usage value, status %#x.\n", status);
        return 0;
    }

    value64 = value - joystick->axes[axis].min;
    return (value64 * 65535) / joystick->axes[axis].range;
}

/**************************************************************************
 *                              joyGetPosEx             [WINMM.@]
 */
MMRESULT WINAPI DECLSPEC_HOTPATCH joyGetPosEx(UINT id, JOYINFOEX *pos)
{
    struct joystick *joystick;
    NTSTATUS status;

    TRACE("id %u, pos %p.\n", id, pos);

    if (!pos)
        return MMSYSERR_INVALPARAM;

    if (id >= MAX_JOYSTICKS || pos->dwSize < sizeof(JOYINFOEX))
        return JOYERR_PARMS;

    pos->dwXpos = 0;
    pos->dwYpos = 0;
    pos->dwZpos = 0;
    pos->dwRpos = 0;
    pos->dwUpos = 0;
    pos->dwVpos = 0;
    pos->dwButtons = 0;
    pos->dwButtonNumber = 0;
    pos->dwPOV = JOY_POVCENTERED;
    pos->dwReserved1 = 0;
    pos->dwReserved2 = 0;

    EnterCriticalSection(&joystick_cs);

    find_joysticks();

    joystick = &joysticks[id];
    if (!joystick->path)
    {
        LeaveCriticalSection(&joystick_cs);
        return JOYERR_PARMS;
    }

    joystick->report[0] = 0;
    if (!HidD_GetInputReport(joystick->file, joystick->report, joystick->report_length))
    {
        if (GetLastError() == ERROR_ACCESS_DENIED)
        {
            remove_joystick(joystick);
            LeaveCriticalSection(&joystick_cs);
            return JOYERR_PARMS;
        }
        ERR("Failed to get input report, error %u.\n", GetLastError());
    }

    if (pos->dwFlags & JOY_RETURNX)
        pos->dwXpos = get_axis_value(joystick, AXIS_X);
    if (pos->dwFlags & JOY_RETURNY)
        pos->dwYpos = get_axis_value(joystick, AXIS_Y);
    if (pos->dwFlags & JOY_RETURNZ)
        pos->dwZpos = get_axis_value(joystick, AXIS_Z);
    if (pos->dwFlags & JOY_RETURNR)
        pos->dwRpos = get_axis_value(joystick, AXIS_R);
    if (pos->dwFlags & JOY_RETURNU)
        pos->dwUpos = get_axis_value(joystick, AXIS_U);
    if (pos->dwFlags & JOY_RETURNV)
        pos->dwVpos = get_axis_value(joystick, AXIS_V);

    if (pos->dwFlags & JOY_RETURNBUTTONS)
    {
        ULONG count = MAX_BUTTONS, i;
        USAGE buttons[MAX_BUTTONS];

        if ((status = HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0, buttons, &count,
                joystick->data, joystick->report, joystick->report_length)) == HIDP_STATUS_SUCCESS)
        {
            for (i = 0; i < count; ++i)
                pos->dwButtons |= 1 << (buttons[i] - 1);
            pos->dwButtonNumber = count;
        }
        else
        {
            ERR("Failed to get button values, status %#x.\n", status);
        }
    }

    if ((pos->dwFlags & JOY_RETURNPOV) && joystick->has_pov)
    {
        ULONG value;
        if ((status = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC,
                0, HID_USAGE_GENERIC_HATSWITCH, &value, joystick->data,
                joystick->report, joystick->report_length)) == HIDP_STATUS_SUCCESS)
        {
            pos->dwPOV = value ? (value - 1) * 4500 : JOY_POVCENTERED;
        }
        else
            ERR("Failed to get hatswitch value, status %#x.\n", status);
    }

    LeaveCriticalSection(&joystick_cs);

    return JOYERR_NOERROR;
}

/**************************************************************************
 * 				joyGetPos	       	[WINMM.@]
 */
MMRESULT WINAPI joyGetPos(UINT id, JOYINFO *pos)
{
    JOYINFOEX ex;
    MMRESULT res;

    TRACE("id %u, pos %p.\n", id, pos);

    if (!pos)
        return MMSYSERR_INVALPARAM;

    ex.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNZ | JOY_RETURNBUTTONS;
    if (!(res = joyGetPosEx(id, &ex)))
    {
        pos->wXpos = ex.dwXpos;
        pos->wYpos = ex.dwYpos;
        pos->wZpos = ex.dwZpos;
        pos->wButtons = ex.dwButtons;
    }
    return res;
}

/**************************************************************************
 * 				joyGetThreshold		[WINMM.@]
 */
MMRESULT WINAPI joyGetThreshold(UINT id, UINT *threshold)
{
    TRACE("id %u, threshold %p.\n", id, threshold);

    if (id >= MAX_JOYSTICKS)
        return JOYERR_PARMS;

    EnterCriticalSection(&joystick_cs);
    *threshold = joysticks[id].threshold;
    LeaveCriticalSection(&joystick_cs);

    return JOYERR_NOERROR;
}

/**************************************************************************
 * 				joyReleaseCapture	[WINMM.@]
 */
MMRESULT WINAPI joyReleaseCapture(UINT id)
{
    TRACE("id %u.\n", id);

    if (id >= MAX_JOYSTICKS)
        return JOYERR_PARMS;

    EnterCriticalSection(&joystick_cs);

    if (joysticks[id].hCapture)
    {
        KillTimer(joysticks[id].hCapture, joysticks[id].wTimer);
        joysticks[id].hCapture = 0;
        joysticks[id].wTimer = 0;
    }
    else
        TRACE("Joystick is not captured, ignoring request.\n");

    LeaveCriticalSection(&joystick_cs);

    return JOYERR_NOERROR;
}

/**************************************************************************
 * 				joySetCapture		[WINMM.@]
 */
MMRESULT WINAPI joySetCapture(HWND window, UINT id, UINT period, BOOL changed)
{
    TRACE("window %p, id %u, period %u, changed %d.\n", window, id, period, changed);

    if (id >= MAX_JOYSTICKS || !window)
        return JOYERR_PARMS;

    if (period < JOY_PERIOD_MIN)
        period = JOY_PERIOD_MIN;
    if (period > JOY_PERIOD_MAX)
        period = JOY_PERIOD_MAX;

    EnterCriticalSection(&joystick_cs);

    if (joysticks[id].hCapture || !IsWindow(window))
    {
        LeaveCriticalSection(&joystick_cs);
        return JOYERR_NOCANDO; /* FIXME: what should be returned? */
    }

    if (joyGetPos(id, &joysticks[id].ji) != JOYERR_NOERROR)
    {
        LeaveCriticalSection(&joystick_cs);
        return JOYERR_UNPLUGGED;
    }

    if (!(joysticks[id].wTimer = SetTimer(window, 0, period, JOY_Timer)))
    {
        LeaveCriticalSection(&joystick_cs);
        return JOYERR_NOCANDO;
    }

    joysticks[id].hCapture = window;
    joysticks[id].bChanged = changed;

    LeaveCriticalSection(&joystick_cs);
    return JOYERR_NOERROR;
}

/**************************************************************************
 * 				joySetThreshold		[WINMM.@]
 */
MMRESULT WINAPI joySetThreshold(UINT id, UINT threshold)
{
    TRACE("id %u, threshold %u.\n", id, threshold);

    if (id >= MAX_JOYSTICKS || threshold > 65535)
        return MMSYSERR_INVALPARAM;

    EnterCriticalSection(&joystick_cs);

    joysticks[id].threshold = threshold;

    LeaveCriticalSection(&joystick_cs);
    return JOYERR_NOERROR;
}
