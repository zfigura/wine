/*
 * Unit tests for Direct Show functions
 *
 * Copyright (C) 2004 Christian Costa
 * Copyright (C) 2008 Alexander Dorofeyev
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

#define COBJMACROS
#define CONST_VTABLE

#include "wine/test.h"
#include "dshow.h"
#include "control.h"
#include "wine/strmbase.h"
#ifdef WINE_CROSSTEST
enum __wine_debug_class
{
    __WINE_DBCL_FIXME,
    __WINE_DBCL_ERR,
    __WINE_DBCL_WARN,
    __WINE_DBCL_TRACE,

    __WINE_DBCL_INIT = 7  /* lazy init flag */
};

struct __wine_debug_channel
{
    unsigned char flags;
    char name[15];
};

#if defined(__x86_64__) && defined(__GNUC__) && defined(__WINE_USE_MSVCRT)
# define __winetest_va_start(list,arg) __builtin_ms_va_start(list,arg)
# define __winetest_va_end(list) __builtin_ms_va_end(list)
#else
# define __winetest_va_start(list,arg) va_start(list,arg)
# define __winetest_va_end(list) va_end(list)
#endif

int wine_dbg_log( enum __wine_debug_class cls, struct __wine_debug_channel *channel,
                  const char *func, const char *format, ... )
{
    va_list valist;
    printf( "%04x:%s:%s ", GetCurrentThreadId(), channel->name, func);
    va_start(valist, format);
    vprintf(format, valist);
    va_end(valist);
}

/* printf with temp buffer allocation */
const char *wine_dbg_sprintf( const char *format, ... )
{
    static const int max_size = 200;
    static char ret[200];
    int len;
    va_list valist;

    va_start(valist, format);
    len = vsnprintf( ret, max_size, format, valist );
    if (len == -1 || len >= max_size) ret[max_size-1] = 0;
    va_end(valist);
    return ret;
}
#endif

static const WCHAR avifile[] = {'t','e','s','t','.','a','v','i',0};
static const WCHAR mpegfile[] = {'t','e','s','t','.','m','p','g',0};

static WCHAR *load_resource(const WCHAR *name)
{
    static WCHAR pathW[MAX_PATH];
    DWORD written;
    HANDLE file;
    HRSRC res;
    void *ptr;

    GetTempPathW(ARRAY_SIZE(pathW), pathW);
    lstrcatW(pathW, name);

    file = CreateFileW(pathW, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "file creation failed, at %s, error %d\n", wine_dbgstr_w(pathW),
        GetLastError());

    res = FindResourceW(NULL, name, (LPCWSTR)RT_RCDATA);
    ok( res != 0, "couldn't find resource\n" );
    ptr = LockResource( LoadResource( GetModuleHandleA(NULL), res ));
    WriteFile( file, ptr, SizeofResource( GetModuleHandleA(NULL), res ), &written, NULL );
    ok( written == SizeofResource( GetModuleHandleA(NULL), res ), "couldn't write resource\n" );
    CloseHandle( file );

    return pathW;
}

static IFilterGraph2 *create_graph(void)
{
    IFilterGraph2 *ret;
    HRESULT hr;
    hr = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IFilterGraph2, (void **)&ret);
    ok(hr == S_OK, "Failed to create FilterGraph: %#x\n", hr);
    return ret;
}

static void test_basic_video(IFilterGraph2 *graph)
{
    IBasicVideo* pbv;
    LONG video_width, video_height, window_width;
    LONG left, top, width, height;
    HRESULT hr;

    hr = IFilterGraph2_QueryInterface(graph, &IID_IBasicVideo, (void **)&pbv);
    ok(hr==S_OK, "Cannot get IBasicVideo interface returned: %x\n", hr);

    /* test get video size */
    hr = IBasicVideo_GetVideoSize(pbv, NULL, NULL);
    ok(hr==E_POINTER, "IBasicVideo_GetVideoSize returned: %x\n", hr);
    hr = IBasicVideo_GetVideoSize(pbv, &video_width, NULL);
    ok(hr==E_POINTER, "IBasicVideo_GetVideoSize returned: %x\n", hr);
    hr = IBasicVideo_GetVideoSize(pbv, NULL, &video_height);
    ok(hr==E_POINTER, "IBasicVideo_GetVideoSize returned: %x\n", hr);
    hr = IBasicVideo_GetVideoSize(pbv, &video_width, &video_height);
    ok(hr==S_OK, "Cannot get video size returned: %x\n", hr);

    /* test source position */
    hr = IBasicVideo_GetSourcePosition(pbv, NULL, NULL, NULL, NULL);
    ok(hr == E_POINTER, "IBasicVideo_GetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_GetSourcePosition(pbv, &left, &top, NULL, NULL);
    ok(hr == E_POINTER, "IBasicVideo_GetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_GetSourcePosition(pbv, NULL, NULL, &width, &height);
    ok(hr == E_POINTER, "IBasicVideo_GetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_GetSourcePosition(pbv, &left, &top, &width, &height);
    ok(hr == S_OK, "Cannot get source position returned: %x\n", hr);
    ok(left == 0, "expected 0, got %d\n", left);
    ok(top == 0, "expected 0, got %d\n", top);
    ok(width == video_width, "expected %d, got %d\n", video_width, width);
    ok(height == video_height, "expected %d, got %d\n", video_height, height);

    hr = IBasicVideo_SetSourcePosition(pbv, 0, 0, 0, 0);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_SetSourcePosition(pbv, 0, 0, video_width*2, video_height*2);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_put_SourceTop(pbv, -1);
    ok(hr==E_INVALIDARG, "IBasicVideo_put_SourceTop returned: %x\n", hr);
    hr = IBasicVideo_put_SourceTop(pbv, 0);
    ok(hr==S_OK, "Cannot put source top returned: %x\n", hr);
    hr = IBasicVideo_put_SourceTop(pbv, 1);
    ok(hr==E_INVALIDARG, "IBasicVideo_put_SourceTop returned: %x\n", hr);

    hr = IBasicVideo_SetSourcePosition(pbv, video_width, 0, video_width, video_height);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_SetSourcePosition(pbv, 0, video_height, video_width, video_height);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_SetSourcePosition(pbv, -1, 0, video_width, video_height);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_SetSourcePosition(pbv, 0, -1, video_width, video_height);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_SetSourcePosition(pbv, video_width/2, video_height/2, video_width, video_height);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_SetSourcePosition(pbv, video_width/2, video_height/2, video_width, video_height);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetSourcePosition returned: %x\n", hr);

    hr = IBasicVideo_SetSourcePosition(pbv, 0, 0, video_width, video_height+1);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetSourcePosition returned: %x\n", hr);
    hr = IBasicVideo_SetSourcePosition(pbv, 0, 0, video_width+1, video_height);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetSourcePosition returned: %x\n", hr);

    hr = IBasicVideo_SetSourcePosition(pbv, video_width/2, video_height/2, video_width/3+1, video_height/3+1);
    ok(hr==S_OK, "Cannot set source position returned: %x\n", hr);

    hr = IBasicVideo_get_SourceLeft(pbv, &left);
    ok(hr==S_OK, "Cannot get source left returned: %x\n", hr);
    ok(left==video_width/2, "expected %d, got %d\n", video_width/2, left);
    hr = IBasicVideo_get_SourceTop(pbv, &top);
    ok(hr==S_OK, "Cannot get source top returned: %x\n", hr);
    ok(top==video_height/2, "expected %d, got %d\n", video_height/2, top);
    hr = IBasicVideo_get_SourceWidth(pbv, &width);
    ok(hr==S_OK, "Cannot get source width returned: %x\n", hr);
    ok(width==video_width/3+1, "expected %d, got %d\n", video_width/3+1, width);
    hr = IBasicVideo_get_SourceHeight(pbv, &height);
    ok(hr==S_OK, "Cannot get source height returned: %x\n", hr);
    ok(height==video_height/3+1, "expected %d, got %d\n", video_height/3+1, height);

    hr = IBasicVideo_put_SourceLeft(pbv, video_width/3);
    ok(hr==S_OK, "Cannot put source left returned: %x\n", hr);
    hr = IBasicVideo_GetSourcePosition(pbv, &left, &top, &width, &height);
    ok(hr == S_OK, "Cannot get source position returned: %x\n", hr);
    ok(left == video_width/3, "expected %d, got %d\n", video_width/3, left);
    ok(width == video_width/3+1, "expected %d, got %d\n", video_width/3+1, width);

    hr = IBasicVideo_put_SourceTop(pbv, video_height/3);
    ok(hr==S_OK, "Cannot put source top returned: %x\n", hr);
    hr = IBasicVideo_GetSourcePosition(pbv, &left, &top, &width, &height);
    ok(hr == S_OK, "Cannot get source position returned: %x\n", hr);
    ok(top == video_height/3, "expected %d, got %d\n", video_height/3, top);
    ok(height == video_height/3+1, "expected %d, got %d\n", video_height/3+1, height);

    hr = IBasicVideo_put_SourceWidth(pbv, video_width/4+1);
    ok(hr==S_OK, "Cannot put source width returned: %x\n", hr);
    hr = IBasicVideo_GetSourcePosition(pbv, &left, &top, &width, &height);
    ok(hr == S_OK, "Cannot get source position returned: %x\n", hr);
    ok(left == video_width/3, "expected %d, got %d\n", video_width/3, left);
    ok(width == video_width/4+1, "expected %d, got %d\n", video_width/4+1, width);

    hr = IBasicVideo_put_SourceHeight(pbv, video_height/4+1);
    ok(hr==S_OK, "Cannot put source height returned: %x\n", hr);
    hr = IBasicVideo_GetSourcePosition(pbv, &left, &top, &width, &height);
    ok(hr == S_OK, "Cannot get source position returned: %x\n", hr);
    ok(top == video_height/3, "expected %d, got %d\n", video_height/3, top);
    ok(height == video_height/4+1, "expected %d, got %d\n", video_height/4+1, height);

    /* test destination rectangle */
    window_width = max(video_width, GetSystemMetrics(SM_CXMIN) - 2 * GetSystemMetrics(SM_CXFRAME));

    hr = IBasicVideo_GetDestinationPosition(pbv, NULL, NULL, NULL, NULL);
    ok(hr == E_POINTER, "IBasicVideo_GetDestinationPosition returned: %x\n", hr);
    hr = IBasicVideo_GetDestinationPosition(pbv, &left, &top, NULL, NULL);
    ok(hr == E_POINTER, "IBasicVideo_GetDestinationPosition returned: %x\n", hr);
    hr = IBasicVideo_GetDestinationPosition(pbv, NULL, NULL, &width, &height);
    ok(hr == E_POINTER, "IBasicVideo_GetDestinationPosition returned: %x\n", hr);
    hr = IBasicVideo_GetDestinationPosition(pbv, &left, &top, &width, &height);
    ok(hr == S_OK, "Cannot get destination position returned: %x\n", hr);
    ok(left == 0, "expected 0, got %d\n", left);
    ok(top == 0, "expected 0, got %d\n", top);
    todo_wine ok(width == window_width, "expected %d, got %d\n", window_width, width);
    todo_wine ok(height == video_height, "expected %d, got %d\n", video_height, height);

    hr = IBasicVideo_SetDestinationPosition(pbv, 0, 0, 0, 0);
    ok(hr==E_INVALIDARG, "IBasicVideo_SetDestinationPosition returned: %x\n", hr);
    hr = IBasicVideo_SetDestinationPosition(pbv, 0, 0, video_width*2, video_height*2);
    ok(hr==S_OK, "Cannot put destination position returned: %x\n", hr);

    hr = IBasicVideo_put_DestinationLeft(pbv, -1);
    ok(hr==S_OK, "Cannot put destination left returned: %x\n", hr);
    hr = IBasicVideo_put_DestinationLeft(pbv, 0);
    ok(hr==S_OK, "Cannot put destination left returned: %x\n", hr);
    hr = IBasicVideo_put_DestinationLeft(pbv, 1);
    ok(hr==S_OK, "Cannot put destination left returned: %x\n", hr);

    hr = IBasicVideo_SetDestinationPosition(pbv, video_width, 0, video_width, video_height);
    ok(hr==S_OK, "Cannot set destinaiton position returned: %x\n", hr);
    hr = IBasicVideo_SetDestinationPosition(pbv, 0, video_height, video_width, video_height);
    ok(hr==S_OK, "Cannot set destinaiton position returned: %x\n", hr);
    hr = IBasicVideo_SetDestinationPosition(pbv, -1, 0, video_width, video_height);
    ok(hr==S_OK, "Cannot set destination position returned: %x\n", hr);
    hr = IBasicVideo_SetDestinationPosition(pbv, 0, -1, video_width, video_height);
    ok(hr==S_OK, "Cannot set destination position returned: %x\n", hr);
    hr = IBasicVideo_SetDestinationPosition(pbv, video_width/2, video_height/2, video_width, video_height);
    ok(hr==S_OK, "Cannot set destination position returned: %x\n", hr);
    hr = IBasicVideo_SetDestinationPosition(pbv, video_width/2, video_height/2, video_width, video_height);
    ok(hr==S_OK, "Cannot set destination position returned: %x\n", hr);

    hr = IBasicVideo_SetDestinationPosition(pbv, 0, 0, video_width, video_height+1);
    ok(hr==S_OK, "Cannot set destination position returned: %x\n", hr);
    hr = IBasicVideo_SetDestinationPosition(pbv, 0, 0, video_width+1, video_height);
    ok(hr==S_OK, "Cannot set destination position returned: %x\n", hr);

    hr = IBasicVideo_SetDestinationPosition(pbv, video_width/2, video_height/2, video_width/3+1, video_height/3+1);
    ok(hr==S_OK, "Cannot set destination position returned: %x\n", hr);

    hr = IBasicVideo_get_DestinationLeft(pbv, &left);
    ok(hr==S_OK, "Cannot get destination left returned: %x\n", hr);
    ok(left==video_width/2, "expected %d, got %d\n", video_width/2, left);
    hr = IBasicVideo_get_DestinationTop(pbv, &top);
    ok(hr==S_OK, "Cannot get destination top returned: %x\n", hr);
    ok(top==video_height/2, "expected %d, got %d\n", video_height/2, top);
    hr = IBasicVideo_get_DestinationWidth(pbv, &width);
    ok(hr==S_OK, "Cannot get destination width returned: %x\n", hr);
    ok(width==video_width/3+1, "expected %d, got %d\n", video_width/3+1, width);
    hr = IBasicVideo_get_DestinationHeight(pbv, &height);
    ok(hr==S_OK, "Cannot get destination height returned: %x\n", hr);
    ok(height==video_height/3+1, "expected %d, got %d\n", video_height/3+1, height);

    hr = IBasicVideo_put_DestinationLeft(pbv, video_width/3);
    ok(hr==S_OK, "Cannot put destination left returned: %x\n", hr);
    hr = IBasicVideo_GetDestinationPosition(pbv, &left, &top, &width, &height);
    ok(hr == S_OK, "Cannot get source position returned: %x\n", hr);
    ok(left == video_width/3, "expected %d, got %d\n", video_width/3, left);
    ok(width == video_width/3+1, "expected %d, got %d\n", video_width/3+1, width);

    hr = IBasicVideo_put_DestinationTop(pbv, video_height/3);
    ok(hr==S_OK, "Cannot put destination top returned: %x\n", hr);
    hr = IBasicVideo_GetDestinationPosition(pbv, &left, &top, &width, &height);
    ok(hr == S_OK, "Cannot get source position returned: %x\n", hr);
    ok(top == video_height/3, "expected %d, got %d\n", video_height/3, top);
    ok(height == video_height/3+1, "expected %d, got %d\n", video_height/3+1, height);

    hr = IBasicVideo_put_DestinationWidth(pbv, video_width/4+1);
    ok(hr==S_OK, "Cannot put destination width returned: %x\n", hr);
    hr = IBasicVideo_GetDestinationPosition(pbv, &left, &top, &width, &height);
    ok(hr == S_OK, "Cannot get source position returned: %x\n", hr);
    ok(left == video_width/3, "expected %d, got %d\n", video_width/3, left);
    ok(width == video_width/4+1, "expected %d, got %d\n", video_width/4+1, width);

    hr = IBasicVideo_put_DestinationHeight(pbv, video_height/4+1);
    ok(hr==S_OK, "Cannot put destination height returned: %x\n", hr);
    hr = IBasicVideo_GetDestinationPosition(pbv, &left, &top, &width, &height);
    ok(hr == S_OK, "Cannot get source position returned: %x\n", hr);
    ok(top == video_height/3, "expected %d, got %d\n", video_height/3, top);
    ok(height == video_height/4+1, "expected %d, got %d\n", video_height/4+1, height);

    /* reset source rectangle */
    hr = IBasicVideo_SetDefaultSourcePosition(pbv);
    ok(hr==S_OK, "IBasicVideo_SetDefaultSourcePosition returned: %x\n", hr);

    /* reset destination position */
    hr = IBasicVideo_SetDestinationPosition(pbv, 0, 0, video_width, video_height);
    ok(hr==S_OK, "Cannot set destination position returned: %x\n", hr);

    IBasicVideo_Release(pbv);
}

static void test_media_seeking(IFilterGraph2 *graph)
{
    IMediaSeeking *seeking;
    IMediaFilter *filter;
    LONGLONG pos, stop, duration;
    GUID format;
    HRESULT hr;

    IFilterGraph2_SetDefaultSyncSource(graph);
    hr = IFilterGraph2_QueryInterface(graph, &IID_IMediaSeeking, (void **)&seeking);
    ok(hr == S_OK, "QueryInterface(IMediaControl) failed: %08x\n", hr);

    hr = IFilterGraph2_QueryInterface(graph, &IID_IMediaFilter, (void **)&filter);
    ok(hr == S_OK, "QueryInterface(IMediaFilter) failed: %08x\n", hr);

    format = GUID_NULL;
    hr = IMediaSeeking_GetTimeFormat(seeking, &format);
    ok(hr == S_OK, "GetTimeFormat failed: %#x\n", hr);
    ok(IsEqualGUID(&format, &TIME_FORMAT_MEDIA_TIME), "got %s\n", wine_dbgstr_guid(&format));

    pos = 0xdeadbeef;
    hr = IMediaSeeking_ConvertTimeFormat(seeking, &pos, NULL, 0x123456789a, NULL);
    ok(hr == S_OK, "ConvertTimeFormat failed: %#x\n", hr);
    ok(pos == 0x123456789a, "got %s\n", wine_dbgstr_longlong(pos));

    pos = 0xdeadbeef;
    hr = IMediaSeeking_ConvertTimeFormat(seeking, &pos, &TIME_FORMAT_MEDIA_TIME, 0x123456789a, NULL);
    ok(hr == S_OK, "ConvertTimeFormat failed: %#x\n", hr);
    ok(pos == 0x123456789a, "got %s\n", wine_dbgstr_longlong(pos));

    pos = 0xdeadbeef;
    hr = IMediaSeeking_ConvertTimeFormat(seeking, &pos, NULL, 0x123456789a, &TIME_FORMAT_MEDIA_TIME);
    ok(hr == S_OK, "ConvertTimeFormat failed: %#x\n", hr);
    ok(pos == 0x123456789a, "got %s\n", wine_dbgstr_longlong(pos));

    hr = IMediaSeeking_GetCurrentPosition(seeking, &pos);
    ok(hr == S_OK, "GetCurrentPosition failed: %#x\n", hr);
    ok(pos == 0, "got %s\n", wine_dbgstr_longlong(pos));

    hr = IMediaSeeking_GetDuration(seeking, &duration);
    ok(hr == S_OK, "GetDuration failed: %#x\n", hr);
    ok(duration > 0, "got %s\n", wine_dbgstr_longlong(duration));

    hr = IMediaSeeking_GetStopPosition(seeking, &stop);
    ok(hr == S_OK, "GetCurrentPosition failed: %08x\n", hr);
    ok(stop == duration || stop == duration + 1, "expected %s, got %s\n",
        wine_dbgstr_longlong(duration), wine_dbgstr_longlong(stop));

    hr = IMediaSeeking_SetPositions(seeking, NULL, AM_SEEKING_ReturnTime, NULL, AM_SEEKING_NoPositioning);
    ok(hr == S_OK, "SetPositions failed: %#x\n", hr);
    hr = IMediaSeeking_SetPositions(seeking, NULL, AM_SEEKING_NoPositioning, NULL, AM_SEEKING_ReturnTime);
    ok(hr == S_OK, "SetPositions failed: %#x\n", hr);

    pos = 0;
    hr = IMediaSeeking_SetPositions(seeking, &pos, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
    ok(hr == S_OK, "SetPositions failed: %08x\n", hr);

    IMediaFilter_SetSyncSource(filter, NULL);
    pos = 0xdeadbeef;
    hr = IMediaSeeking_GetCurrentPosition(seeking, &pos);
    ok(hr == S_OK, "GetCurrentPosition failed: %08x\n", hr);
    ok(pos == 0, "Position != 0 (%s)\n", wine_dbgstr_longlong(pos));
    IFilterGraph2_SetDefaultSyncSource(graph);

    IMediaSeeking_Release(seeking);
    IMediaFilter_Release(filter);
}

static void test_state_change(IFilterGraph2 *graph)
{
    IMediaControl *control;
    OAFilterState state;
    HRESULT hr;

    hr = IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    ok(hr == S_OK, "QueryInterface(IMediaControl) failed: %x\n", hr);

    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "GetState() failed: %x\n", hr);
    ok(state == State_Stopped, "wrong state %d\n", state);

    hr = IMediaControl_Run(control);
    ok(SUCCEEDED(hr), "Run() failed: %x\n", hr);
    hr = IMediaControl_GetState(control, INFINITE, &state);
    ok(SUCCEEDED(hr), "GetState() failed: %x\n", hr);
    ok(state == State_Running, "wrong state %d\n", state);

    hr = IMediaControl_Stop(control);
    ok(SUCCEEDED(hr), "Stop() failed: %x\n", hr);
    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "GetState() failed: %x\n", hr);
    ok(state == State_Stopped, "wrong state %d\n", state);

    hr = IMediaControl_Pause(control);
    ok(SUCCEEDED(hr), "Pause() failed: %x\n", hr);
    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "GetState() failed: %x\n", hr);
    ok(state == State_Paused, "wrong state %d\n", state);

    hr = IMediaControl_Run(control);
    ok(SUCCEEDED(hr), "Run() failed: %x\n", hr);
    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "GetState() failed: %x\n", hr);
    ok(state == State_Running, "wrong state %d\n", state);

    hr = IMediaControl_Pause(control);
    ok(SUCCEEDED(hr), "Pause() failed: %x\n", hr);
    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "GetState() failed: %x\n", hr);
    ok(state == State_Paused, "wrong state %d\n", state);

    hr = IMediaControl_Stop(control);
    ok(SUCCEEDED(hr), "Stop() failed: %x\n", hr);
    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "GetState() failed: %x\n", hr);
    ok(state == State_Stopped, "wrong state %d\n", state);

    IMediaControl_Release(control);
}

static void test_media_event(IFilterGraph2 *graph)
{
    IMediaEvent *media_event;
    IMediaSeeking *seeking;
    IMediaControl *control;
    IMediaFilter *filter;
    LONG_PTR lparam1, lparam2;
    LONGLONG current, stop;
    OAFilterState state;
    int got_eos = 0;
    HANDLE event;
    HRESULT hr;
    LONG code;

    hr = IFilterGraph2_QueryInterface(graph, &IID_IMediaFilter, (void **)&filter);
    ok(hr == S_OK, "QueryInterface(IMediaFilter) failed: %#x\n", hr);

    hr = IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    ok(hr == S_OK, "QueryInterface(IMediaControl) failed: %#x\n", hr);

    hr = IFilterGraph2_QueryInterface(graph, &IID_IMediaEvent, (void **)&media_event);
    ok(hr == S_OK, "QueryInterface(IMediaEvent) failed: %#x\n", hr);

    hr = IFilterGraph2_QueryInterface(graph, &IID_IMediaSeeking, (void **)&seeking);
    ok(hr == S_OK, "QueryInterface(IMediaEvent) failed: %#x\n", hr);

    hr = IMediaControl_Stop(control);
    ok(SUCCEEDED(hr), "Stop() failed: %#x\n", hr);
    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "GetState() timed out\n");

    hr = IMediaSeeking_GetDuration(seeking, &stop);
    ok(hr == S_OK, "GetDuration() failed: %#x\n", hr);
    current = 0;
    hr = IMediaSeeking_SetPositions(seeking, &current, AM_SEEKING_AbsolutePositioning, &stop, AM_SEEKING_AbsolutePositioning);
    ok(hr == S_OK, "SetPositions() failed: %#x\n", hr);

    hr = IMediaFilter_SetSyncSource(filter, NULL);
    ok(hr == S_OK, "SetSyncSource() failed: %#x\n", hr);

    hr = IMediaEvent_GetEventHandle(media_event, (OAEVENT *)&event);
    ok(hr == S_OK, "GetEventHandle() failed: %#x\n", hr);

    /* flush existing events */
    while ((hr = IMediaEvent_GetEvent(media_event, &code, &lparam1, &lparam2, 0)) == S_OK);

    ok(WaitForSingleObject(event, 0) == WAIT_TIMEOUT, "event should not be signaled\n");

    hr = IMediaControl_Run(control);
    ok(SUCCEEDED(hr), "Run() failed: %#x\n", hr);

    while (!got_eos)
    {
        if (WaitForSingleObject(event, 1000) == WAIT_TIMEOUT)
            break;

        while ((hr = IMediaEvent_GetEvent(media_event, &code, &lparam1, &lparam2, 0)) == S_OK)
        {
            if (code == EC_COMPLETE)
            {
                got_eos = 1;
                break;
            }
        }
    }
    ok(got_eos, "didn't get EOS\n");

    hr = IMediaSeeking_GetCurrentPosition(seeking, &current);
    ok(hr == S_OK, "GetCurrentPosition() failed: %#x\n", hr);
todo_wine
    ok(current == stop, "expected %s, got %s\n", wine_dbgstr_longlong(stop), wine_dbgstr_longlong(current));

    hr = IMediaControl_Stop(control);
    ok(SUCCEEDED(hr), "Run() failed: %#x\n", hr);
    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "GetState() timed out\n");

    hr = IFilterGraph2_SetDefaultSyncSource(graph);
    ok(hr == S_OK, "SetDefaultSinkSource() failed: %#x\n", hr);

    IMediaSeeking_Release(seeking);
    IMediaEvent_Release(media_event);
    IMediaControl_Release(control);
    IMediaFilter_Release(filter);
}

static void rungraph(IFilterGraph2 *graph)
{
    test_basic_video(graph);
    test_media_seeking(graph);
    test_state_change(graph);
    test_media_event(graph);
}

static HRESULT test_graph_builder_connect(WCHAR *filename)
{
    static const WCHAR outputW[] = {'O','u','t','p','u','t',0};
    static const WCHAR inW[] = {'I','n',0};
    IBaseFilter *source_filter, *video_filter;
    IPin *pin_in, *pin_out;
    IFilterGraph2 *graph;
    IVideoWindow *window;
    HRESULT hr;

    graph = create_graph();

    hr = CoCreateInstance(&CLSID_VideoRenderer, NULL, CLSCTX_INPROC_SERVER, &IID_IVideoWindow, (void **)&window);
    ok(hr == S_OK, "Failed to create VideoRenderer: %#x\n", hr);

    hr = IFilterGraph2_AddSourceFilter(graph, filename, NULL, &source_filter);
    ok(hr == S_OK, "AddSourceFilter failed: %#x\n", hr);

    hr = IVideoWindow_QueryInterface(window, &IID_IBaseFilter, (void **)&video_filter);
    ok(hr == S_OK, "QueryInterface(IBaseFilter) failed: %#x\n", hr);
    hr = IFilterGraph2_AddFilter(graph, video_filter, NULL);
    ok(hr == S_OK, "AddFilter failed: %#x\n", hr);

    hr = IBaseFilter_FindPin(source_filter, outputW, &pin_out);
    ok(hr == S_OK, "FindPin failed: %#x\n", hr);
    hr = IBaseFilter_FindPin(video_filter, inW, &pin_in);
    ok(hr == S_OK, "FindPin failed: %#x\n", hr);
    hr = IFilterGraph2_Connect(graph, pin_out, pin_in);

    if (SUCCEEDED(hr))
        rungraph(graph);

    IPin_Release(pin_in);
    IPin_Release(pin_out);
    IBaseFilter_Release(source_filter);
    IBaseFilter_Release(video_filter);
    IVideoWindow_Release(window);
    IFilterGraph2_Release(graph);

    return hr;
}

static void test_render_run(const WCHAR *file)
{
    IFilterGraph2 *graph;
    HANDLE h;
    HRESULT hr;
    LONG refs;
    WCHAR *filename = load_resource(file);

    h = CreateFileW(filename, 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        skip("Could not read test file %s, skipping test\n", wine_dbgstr_w(file));
        DeleteFileW(filename);
        return;
    }
    CloseHandle(h);

    trace("running %s\n", wine_dbgstr_w(file));

    graph = create_graph();

    hr = IFilterGraph2_RenderFile(graph, filename, NULL);
    if (FAILED(hr))
    {
        skip("%s: codec not supported; skipping test\n", wine_dbgstr_w(file));

        refs = IFilterGraph2_Release(graph);
        ok(!refs, "Graph has %u references\n", refs);

        hr = test_graph_builder_connect(filename);
todo_wine
        ok(hr == VFW_E_CANNOT_CONNECT, "got %#x\n", hr);
    }
    else
    {
        ok(hr == S_OK || hr == VFW_S_AUDIO_NOT_RENDERED, "RenderFile failed: %x\n", hr);
        rungraph(graph);

        refs = IFilterGraph2_Release(graph);
        ok(!refs, "Graph has %u references\n", refs);

        hr = test_graph_builder_connect(filename);
        ok(hr == S_OK || hr == VFW_S_PARTIAL_RENDER, "got %#x\n", hr);
    }

    /* check reference leaks */
    h = CreateFileW(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(h != INVALID_HANDLE_VALUE, "CreateFile failed: err=%d\n", GetLastError());
    CloseHandle(h);

    DeleteFileW(filename);
}

static DWORD WINAPI call_RenderFile_multithread(LPVOID lParam)
{
    WCHAR *filename = load_resource(avifile);
    IFilterGraph2 *graph = lParam;
    HRESULT hr;

    hr = IFilterGraph2_RenderFile(graph, filename, NULL);
todo_wine
    ok(SUCCEEDED(hr), "RenderFile failed: %x\n", hr);

    if (SUCCEEDED(hr))
        rungraph(graph);

    return 0;
}

static void test_render_with_multithread(void)
{
    IFilterGraph2 *graph;
    HANDLE thread;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    graph = create_graph();

    thread = CreateThread(NULL, 0, call_RenderFile_multithread, graph, 0, NULL);

    ok(WaitForSingleObject(thread, 1000) == WAIT_OBJECT_0, "wait failed\n");
    IFilterGraph2_Release(graph);
    CloseHandle(thread);
    CoUninitialize();
}

static void test_graph_builder(void)
{
    HRESULT hr;
    IGraphBuilder *pgraph;
    IBaseFilter *pF = NULL;
    IBaseFilter *pF2 = NULL;
    IPin *pIn = NULL;
    IEnumPins *pEnum = NULL;
    PIN_DIRECTION dir;
    static const WCHAR testFilterW[] = {'t','e','s','t','F','i','l','t','e','r',0};
    static const WCHAR fooBarW[] = {'f','o','o','B','a','r',0};

    pgraph = (IGraphBuilder *)create_graph();

    /* create video filter */
    hr = CoCreateInstance(&CLSID_VideoRenderer, NULL, CLSCTX_INPROC_SERVER,
            &IID_IBaseFilter, (LPVOID*)&pF);
    ok(hr == S_OK, "CoCreateInstance failed with %x\n", hr);
    ok(pF != NULL, "pF is NULL\n");

    hr = IGraphBuilder_AddFilter(pgraph, NULL, testFilterW);
    ok(hr == E_POINTER, "IGraphBuilder_AddFilter returned %x\n", hr);

    /* add the two filters to the graph */
    hr = IGraphBuilder_AddFilter(pgraph, pF, testFilterW);
    ok(hr == S_OK, "failed to add pF to the graph: %x\n", hr);

    /* find the pins */
    hr = IBaseFilter_EnumPins(pF, &pEnum);
    ok(hr == S_OK, "IBaseFilter_EnumPins failed for pF: %x\n", hr);
    ok(pEnum != NULL, "pEnum is NULL\n");
    hr = IEnumPins_Next(pEnum, 1, &pIn, NULL);
    ok(hr == S_OK, "IEnumPins_Next failed for pF: %x\n", hr);
    ok(pIn != NULL, "pIn is NULL\n");
    hr = IPin_QueryDirection(pIn, &dir);
    ok(hr == S_OK, "IPin_QueryDirection failed: %x\n", hr);
    ok(dir == PINDIR_INPUT, "pin has wrong direction\n");

    hr = IGraphBuilder_FindFilterByName(pgraph, fooBarW, &pF2);
    ok(hr == VFW_E_NOT_FOUND, "IGraphBuilder_FindFilterByName returned %x\n", hr);
    ok(pF2 == NULL, "IGraphBuilder_FindFilterByName returned %p\n", pF2);
    hr = IGraphBuilder_FindFilterByName(pgraph, testFilterW, &pF2);
    ok(hr == S_OK, "IGraphBuilder_FindFilterByName returned %x\n", hr);
    ok(pF2 != NULL, "IGraphBuilder_FindFilterByName returned NULL\n");
    hr = IGraphBuilder_FindFilterByName(pgraph, testFilterW, NULL);
    ok(hr == E_POINTER, "IGraphBuilder_FindFilterByName returned %x\n", hr);

    hr = IGraphBuilder_Connect(pgraph, NULL, pIn);
    ok(hr == E_POINTER, "IGraphBuilder_Connect returned %x\n", hr);

    hr = IGraphBuilder_Connect(pgraph, pIn, NULL);
    ok(hr == E_POINTER, "IGraphBuilder_Connect returned %x\n", hr);

    hr = IGraphBuilder_Connect(pgraph, pIn, pIn);
    ok(hr == VFW_E_CANNOT_CONNECT, "IGraphBuilder_Connect returned %x\n", hr);

    if (pIn) IPin_Release(pIn);
    if (pEnum) IEnumPins_Release(pEnum);
    if (pF) IBaseFilter_Release(pF);
    if (pF2) IBaseFilter_Release(pF2);
    IGraphBuilder_Release(pgraph);
}

struct priority_pin
{
    union {
        BasePin pin;
        BaseInputPin input;
        BaseOutputPin output;
    };
    GUID subtype;
};

static const IPinVtbl priority_input_vtbl =
{
    BaseInputPinImpl_QueryInterface,
    BasePinImpl_AddRef,
    BaseInputPinImpl_Release,
    BaseInputPinImpl_Connect,
    BaseInputPinImpl_ReceiveConnection,
    BasePinImpl_Disconnect,
    BasePinImpl_ConnectedTo,
    BasePinImpl_ConnectionMediaType,
    BasePinImpl_QueryPinInfo,
    BasePinImpl_QueryDirection,
    BasePinImpl_QueryId,
    BasePinImpl_QueryAccept,
    BasePinImpl_EnumMediaTypes,
    BasePinImpl_QueryInternalConnections,
    BaseInputPinImpl_EndOfStream,
    BaseInputPinImpl_BeginFlush,
    BaseInputPinImpl_EndFlush,
    BasePinImpl_NewSegment
};

static HRESULT WINAPI priority_pin_CheckMediaType(BasePin *pin, const AM_MEDIA_TYPE *mt)
{
    struct priority_pin *This = CONTAINING_RECORD(pin, struct priority_pin, pin);

    if (IsEqualIID(&mt->majortype, &MEDIATYPE_Video) && (IsEqualIID(&This->subtype, &mt->subtype) ||
                                                         IsEqualIID(&This->subtype, &GUID_NULL)))
        return S_OK;
    else
        return VFW_E_TYPE_NOT_ACCEPTED;
}

static HRESULT WINAPI priority_pin_GetMediaType(BasePin *pin, int i, AM_MEDIA_TYPE *mt)
{
    struct priority_pin *This = CONTAINING_RECORD(pin, struct priority_pin, pin);

    if (i == 0)
    {
        memset(mt, 0, sizeof(*mt));
        mt->majortype = MEDIATYPE_Video;
        mt->subtype = This->subtype;
        return S_OK;
    }
    return VFW_S_NO_MORE_ITEMS;
}

static const BaseInputPinFuncTable priority_input_functable =
{
    {
        priority_pin_CheckMediaType,
        NULL,
        BasePinImpl_GetMediaTypeVersion,
        priority_pin_GetMediaType,
    },
    NULL
};

static const IPinVtbl priority_output_vtbl =
{
    BaseOutputPinImpl_QueryInterface,
    BasePinImpl_AddRef,
    BaseOutputPinImpl_Release,
    BaseOutputPinImpl_Connect,
    BaseOutputPinImpl_ReceiveConnection,
    BaseOutputPinImpl_Disconnect,
    BasePinImpl_ConnectedTo,
    BasePinImpl_ConnectionMediaType,
    BasePinImpl_QueryPinInfo,
    BasePinImpl_QueryDirection,
    BasePinImpl_QueryId,
    BasePinImpl_QueryAccept,
    BasePinImpl_EnumMediaTypes,
    BasePinImpl_QueryInternalConnections,
    BaseOutputPinImpl_EndOfStream,
    BaseOutputPinImpl_BeginFlush,
    BaseOutputPinImpl_EndFlush,
    BasePinImpl_NewSegment
};

static HRESULT WINAPI priority_pin_DecideAllocator(BaseOutputPin *This, IMemInputPin *pin, IMemAllocator **alloc)
{
    *alloc = NULL;
    return S_OK;
}

static const BaseOutputPinFuncTable priority_output_functable =
{
    {
        priority_pin_CheckMediaType,
        BaseOutputPinImpl_AttemptConnection,
        BasePinImpl_GetMediaTypeVersion,
        priority_pin_GetMediaType,
    },
    NULL,
    priority_pin_DecideAllocator,
    NULL
};

/* Test filter implementation - a filter that has few predefined pins with single media type
 * that accept only this single media type. Enough for Render(). */

struct priority_filter
{
    BaseFilter base;
    IPin **pins;
    UINT nPins;
};

typedef struct TestFilterPinData
{
PIN_DIRECTION pinDir;
const GUID *mediasubtype;
} TestFilterPinData;

static inline struct priority_filter *impl_from_IBaseFilter(IBaseFilter *iface)
{
    return CONTAINING_RECORD(iface, struct priority_filter, base.IBaseFilter_iface);
}

static ULONG WINAPI priority_filter_Release(IBaseFilter * iface)
{
    struct priority_filter *This = impl_from_IBaseFilter(iface);
    ULONG refCount = InterlockedDecrement(&This->base.refCount);

    if (!refCount)
    {
        ULONG i;

        for (i = 0; i < This->nPins; i++)
        {
            IPin *pConnectedTo;

            if (SUCCEEDED(IPin_ConnectedTo(This->pins[i], &pConnectedTo)))
            {
                IPin_Disconnect(pConnectedTo);
                IPin_Release(pConnectedTo);
            }
            IPin_Disconnect(This->pins[i]);

            IPin_Release(This->pins[i]);
        }

        BaseFilter_Destroy(&This->base);

        CoTaskMemFree(This->pins);
        CoTaskMemFree(This);
        return 0;
    }
    else
        return refCount;
}

/** IMediaFilter methods **/

static HRESULT WINAPI priority_filter_Stop(IBaseFilter * iface)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI priority_filter_Pause(IBaseFilter * iface)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI priority_filter_Run(IBaseFilter * iface, REFERENCE_TIME tStart)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI priority_filter_FindPin(IBaseFilter * iface, LPCWSTR Id, IPin **ppPin)
{
    return E_NOTIMPL;
}

static const IBaseFilterVtbl priority_filter_vtbl =
{
    BaseFilterImpl_QueryInterface,
    BaseFilterImpl_AddRef,
    priority_filter_Release,
    BaseFilterImpl_GetClassID,
    priority_filter_Stop,
    priority_filter_Pause,
    priority_filter_Run,
    BaseFilterImpl_GetState,
    BaseFilterImpl_SetSyncSource,
    BaseFilterImpl_GetSyncSource,
    BaseFilterImpl_EnumPins,
    priority_filter_FindPin,
    BaseFilterImpl_QueryFilterInfo,
    BaseFilterImpl_JoinFilterGraph,
    BaseFilterImpl_QueryVendorInfo
};

static IPin *WINAPI priority_filter_GetPin(BaseFilter *base, int i)
{
    struct priority_filter *This;
    trace("%p\n", base);

    This = CONTAINING_RECORD(base, struct priority_filter, base);

    if (i >= This->nPins)
        return NULL;

    IPin_AddRef(This->pins[i]);
    return This->pins[i];
}

static LONG WINAPI priority_filter_GetPinCount(BaseFilter *base)
{
    struct priority_filter *This = CONTAINING_RECORD(base, struct priority_filter, base);

    return This->nPins;
}

static const BaseFilterFuncTable priority_filter_functable = {
    priority_filter_GetPin,
    priority_filter_GetPinCount
};

static HRESULT createtestfilter(const CLSID* clsid, const TestFilterPinData *pinData,
        struct priority_filter **tf)
{
    static const WCHAR wcsInputPinName[] = {'i','n','p','u','t',' ','p','i','n',0};
    static const WCHAR wcsOutputPinName[] = {'o','u','t','p','u','t',' ','p','i','n',0};
    PIN_INFO pinInfo;
    struct priority_filter *filter = NULL;
    UINT nPins, i;

    filter = CoTaskMemAlloc(sizeof(*filter));
    if (!filter) return E_OUTOFMEMORY;

    BaseFilter_Init(&filter->base, &priority_filter_vtbl, clsid, 0, &priority_filter_functable);

    nPins = 0;
    while(pinData[nPins].mediasubtype) ++nPins;

    filter->pins = CoTaskMemAlloc(nPins * sizeof(IPin *));
    ZeroMemory(filter->pins, nPins * sizeof(IPin *));

    for (i = 0; i < nPins; i++)
    {
        struct priority_pin *pin;

        pinInfo.dir = pinData[i].pinDir;
        pinInfo.pFilter = &filter->base.IBaseFilter_iface;
        if (pinInfo.dir == PINDIR_INPUT)
        {
            lstrcpynW(pinInfo.achName, wcsInputPinName, ARRAY_SIZE(pinInfo.achName));
            BaseInputPin_Construct(&priority_input_vtbl, sizeof(struct priority_pin), &pinInfo,
                &priority_input_functable, &filter->base.csFilter, NULL, &filter->pins[i]);
        }
        else
        {
            lstrcpynW(pinInfo.achName, wcsOutputPinName, ARRAY_SIZE(pinInfo.achName));
            BaseOutputPin_Construct(&priority_output_vtbl, sizeof(struct priority_pin), &pinInfo,
                &priority_output_functable, &filter->base.csFilter, &filter->pins[i]);
        }
        pin = (struct priority_pin *)filter->pins[i];
        pin->subtype = *pinData[i].mediasubtype;
    }

    filter->nPins = nPins;
    *tf = filter;
    return S_OK;
}

/* IClassFactory implementation */

typedef struct TestClassFactoryImpl
{
    IClassFactory IClassFactory_iface;
    const TestFilterPinData *filterPinData;
    const CLSID *clsid;
} TestClassFactoryImpl;

static inline TestClassFactoryImpl *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, TestClassFactoryImpl, IClassFactory_iface);
}

static HRESULT WINAPI Test_IClassFactory_QueryInterface(
    LPCLASSFACTORY iface,
    REFIID riid,
    LPVOID *ppvObj)
{
    if (ppvObj == NULL) return E_POINTER;

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IClassFactory))
    {
        *ppvObj = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }

    *ppvObj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI Test_IClassFactory_AddRef(LPCLASSFACTORY iface)
{
    return 2; /* non-heap-based object */
}

static ULONG WINAPI Test_IClassFactory_Release(LPCLASSFACTORY iface)
{
    return 1; /* non-heap-based object */
}

static HRESULT WINAPI Test_IClassFactory_CreateInstance(
    LPCLASSFACTORY iface,
    LPUNKNOWN pUnkOuter,
    REFIID riid,
    LPVOID *ppvObj)
{
    TestClassFactoryImpl *This = impl_from_IClassFactory(iface);
    HRESULT hr;
    struct priority_filter *testfilter;

    *ppvObj = NULL;

    if (pUnkOuter) return CLASS_E_NOAGGREGATION;

    hr = createtestfilter(This->clsid, This->filterPinData, &testfilter);
    if (SUCCEEDED(hr)) {
        hr = IBaseFilter_QueryInterface(&testfilter->base.IBaseFilter_iface, riid, ppvObj);
        IBaseFilter_Release(&testfilter->base.IBaseFilter_iface);
    }
    return hr;
}

static HRESULT WINAPI Test_IClassFactory_LockServer(
    LPCLASSFACTORY iface,
    BOOL fLock)
{
    return S_OK;
}

static IClassFactoryVtbl TestClassFactory_Vtbl =
{
    Test_IClassFactory_QueryInterface,
    Test_IClassFactory_AddRef,
    Test_IClassFactory_Release,
    Test_IClassFactory_CreateInstance,
    Test_IClassFactory_LockServer
};

static HRESULT get_connected_filter_name(struct priority_filter *pFilter, char *FilterName)
{
    IPin *pin = NULL;
    PIN_INFO pinInfo;
    FILTER_INFO filterInfo;
    HRESULT hr;

    FilterName[0] = 0;

    hr = IPin_ConnectedTo(pFilter->pins[0], &pin);
    ok(hr == S_OK, "IPin_ConnectedTo failed with %x\n", hr);

    hr = IPin_QueryPinInfo(pin, &pinInfo);
    ok(hr == S_OK, "IPin_QueryPinInfo failed with %x\n", hr);
    IPin_Release(pin);

    SetLastError(0xdeadbeef);
    hr = IBaseFilter_QueryFilterInfo(pinInfo.pFilter, &filterInfo);
    if (hr == S_OK && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        IBaseFilter_Release(pinInfo.pFilter);
        return E_NOTIMPL;
    }
    ok(hr == S_OK, "IBaseFilter_QueryFilterInfo failed with %x\n", hr);
    IBaseFilter_Release(pinInfo.pFilter);

    IFilterGraph_Release(filterInfo.pGraph);

    WideCharToMultiByte(CP_ACP, 0, filterInfo.achName, -1, FilterName, MAX_FILTER_NAME, NULL, NULL);

    return S_OK;
}

static void test_render_filter_priority(void)
{
    /* Tests filter choice priorities in Render(). */
    DWORD cookie1 = 0, cookie2 = 0, cookie3 = 0;
    HRESULT hr;
    IFilterGraph2* pgraph2 = NULL;
    IFilterMapper2 *pMapper2 = NULL;
    struct priority_filter *ptestfilter = NULL;
    struct priority_filter *ptestfilter2 = NULL;
    static const CLSID CLSID_TestFilter2 = {
        0x37a4edb0,
        0x4d13,
        0x11dd,
        {0xe8, 0x9b, 0x00, 0x19, 0x66, 0x2f, 0xf0, 0xce}
    };
    static const CLSID CLSID_TestFilter3 = {
        0x37a4f2d8,
        0x4d13,
        0x11dd,
        {0xe8, 0x9b, 0x00, 0x19, 0x66, 0x2f, 0xf0, 0xce}
    };
    static const CLSID CLSID_TestFilter4 = {
        0x37a4f3b4,
        0x4d13,
        0x11dd,
        {0xe8, 0x9b, 0x00, 0x19, 0x66, 0x2f, 0xf0, 0xce}
    };
    static const GUID mediasubtype1 = {
        0x37a4f51c,
        0x4d13,
        0x11dd,
        {0xe8, 0x9b, 0x00, 0x19, 0x66, 0x2f, 0xf0, 0xce}
    };
    static const GUID mediasubtype2 = {
        0x37a4f5c6,
        0x4d13,
        0x11dd,
        {0xe8, 0x9b, 0x00, 0x19, 0x66, 0x2f, 0xf0, 0xce}
    };
    static const TestFilterPinData PinData1[] = {
            { PINDIR_OUTPUT, &mediasubtype1 },
            { 0, 0 }
        };
    static const TestFilterPinData PinData2[] = {
            { PINDIR_INPUT,  &mediasubtype1 },
            { 0, 0 }
        };
    static const TestFilterPinData PinData3[] = {
            { PINDIR_INPUT,  &GUID_NULL },
            { 0, 0 }
        };
    static const TestFilterPinData PinData4[] = {
            { PINDIR_INPUT,  &mediasubtype1 },
            { PINDIR_OUTPUT, &mediasubtype2 },
            { 0, 0 }
        };
    static const TestFilterPinData PinData5[] = {
            { PINDIR_INPUT,  &mediasubtype2 },
            { 0, 0 }
        };
    TestClassFactoryImpl Filter1ClassFactory = {
            { &TestClassFactory_Vtbl },
            PinData2, &CLSID_TestFilter2
        };
    TestClassFactoryImpl Filter2ClassFactory = {
            { &TestClassFactory_Vtbl },
            PinData4, &CLSID_TestFilter3
        };
    TestClassFactoryImpl Filter3ClassFactory = {
            { &TestClassFactory_Vtbl },
            PinData5, &CLSID_TestFilter4
        };
    char ConnectedFilterName1[MAX_FILTER_NAME];
    char ConnectedFilterName2[MAX_FILTER_NAME];
    REGFILTER2 rgf2;
    REGFILTERPINS2 rgPins2[2];
    REGPINTYPES rgPinType[2];
    static const WCHAR wszFilterInstanceName1[] = {'T', 'e', 's', 't', 'f', 'i', 'l', 't', 'e', 'r', 'I',
                                                        'n', 's', 't', 'a', 'n', 'c', 'e', '1', 0 };
    static const WCHAR wszFilterInstanceName2[] = {'T', 'e', 's', 't', 'f', 'i', 'l', 't', 'e', 'r', 'I',
                                                        'n', 's', 't', 'a', 'n', 'c', 'e', '2', 0 };
    static const WCHAR wszFilterInstanceName3[] = {'T', 'e', 's', 't', 'f', 'i', 'l', 't', 'e', 'r', 'I',
                                                        'n', 's', 't', 'a', 'n', 'c', 'e', '3', 0 };
    static const WCHAR wszFilterInstanceName4[] = {'T', 'e', 's', 't', 'f', 'i', 'l', 't', 'e', 'r', 'I',
                                                        'n', 's', 't', 'a', 'n', 'c', 'e', '4', 0 };

    /* Test which renderer of two already added to the graph will be chosen
     * (one is "exact" match, other is "wildcard" match. Seems to depend
     * on the order in which filters are added to the graph, thus indicating
     * no preference given to exact match. */
    pgraph2 = create_graph();

    hr = createtestfilter(&GUID_NULL, PinData1, &ptestfilter);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter->base.IBaseFilter_iface, wszFilterInstanceName1);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    hr = createtestfilter(&GUID_NULL, PinData2, &ptestfilter2);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter2->base.IBaseFilter_iface, wszFilterInstanceName2);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    IBaseFilter_Release(&ptestfilter2->base.IBaseFilter_iface);

    hr = createtestfilter(&GUID_NULL, PinData3, &ptestfilter2);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter2->base.IBaseFilter_iface, wszFilterInstanceName3);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    hr = IFilterGraph2_Render(pgraph2, ptestfilter->pins[0]);
    ok(hr == S_OK, "IFilterGraph2_Render failed with %08x\n", hr);

    hr = get_connected_filter_name(ptestfilter, ConnectedFilterName1);
    IFilterGraph2_Release(pgraph2);

    IBaseFilter_Release(&ptestfilter->base.IBaseFilter_iface);
    IBaseFilter_Release(&ptestfilter2->base.IBaseFilter_iface);

    pgraph2 = create_graph();

    hr = createtestfilter(&GUID_NULL, PinData1, &ptestfilter);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter->base.IBaseFilter_iface, wszFilterInstanceName1);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    hr = createtestfilter(&GUID_NULL, PinData3, &ptestfilter2);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter2->base.IBaseFilter_iface, wszFilterInstanceName3);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    IBaseFilter_Release(&ptestfilter2->base.IBaseFilter_iface);

    hr = createtestfilter(&GUID_NULL, PinData2, &ptestfilter2);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter2->base.IBaseFilter_iface, wszFilterInstanceName2);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    hr = IFilterGraph2_Render(pgraph2, ptestfilter->pins[0]);
    ok(hr == S_OK, "IFilterGraph2_Render failed with %08x\n", hr);

    hr = IFilterGraph2_Disconnect(pgraph2, NULL);
    ok(hr == E_POINTER, "IFilterGraph2_Disconnect failed. Expected E_POINTER, received %08x\n", hr);

    get_connected_filter_name(ptestfilter, ConnectedFilterName2);
    ok(strcmp(ConnectedFilterName1, ConnectedFilterName2),
        "expected connected filters to be different but got %s both times\n", ConnectedFilterName1);

    IFilterGraph2_Release(pgraph2);
    IBaseFilter_Release(&ptestfilter->base.IBaseFilter_iface);
    IBaseFilter_Release(&ptestfilter2->base.IBaseFilter_iface);

    /* Test if any preference is given to existing renderer which renders the pin directly vs
       an existing renderer which renders the pin indirectly, through an additional middle filter,
       again trying different orders of creation. Native appears not to give a preference. */

    pgraph2 = create_graph();

    hr = createtestfilter(&GUID_NULL, PinData1, &ptestfilter);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter->base.IBaseFilter_iface, wszFilterInstanceName1);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    hr = createtestfilter(&GUID_NULL, PinData2, &ptestfilter2);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter2->base.IBaseFilter_iface, wszFilterInstanceName2);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    IBaseFilter_Release(&ptestfilter2->base.IBaseFilter_iface);

    hr = createtestfilter(&GUID_NULL, PinData4, &ptestfilter2);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter2->base.IBaseFilter_iface, wszFilterInstanceName3);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    IBaseFilter_Release(&ptestfilter2->base.IBaseFilter_iface);

    hr = createtestfilter(&GUID_NULL, PinData5, &ptestfilter2);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter2->base.IBaseFilter_iface, wszFilterInstanceName4);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    hr = IFilterGraph2_Render(pgraph2, ptestfilter->pins[0]);
    ok(hr == S_OK, "IFilterGraph2_Render failed with %08x\n", hr);

    get_connected_filter_name(ptestfilter, ConnectedFilterName1);
    ok(!strcmp(ConnectedFilterName1, "TestfilterInstance3") || !strcmp(ConnectedFilterName1, "TestfilterInstance2"),
            "unexpected connected filter: %s\n", ConnectedFilterName1);
return;
    IFilterGraph2_Release(pgraph2);
    IBaseFilter_Release(&ptestfilter->base.IBaseFilter_iface);
    IBaseFilter_Release(&ptestfilter2->base.IBaseFilter_iface);

    pgraph2 = create_graph();

    hr = createtestfilter(&GUID_NULL, PinData1, &ptestfilter);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter->base.IBaseFilter_iface, wszFilterInstanceName1);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    hr = createtestfilter(&GUID_NULL, PinData4, &ptestfilter2);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter2->base.IBaseFilter_iface, wszFilterInstanceName3);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    IBaseFilter_Release(&ptestfilter2->base.IBaseFilter_iface);

    hr = createtestfilter(&GUID_NULL, PinData5, &ptestfilter2);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter2->base.IBaseFilter_iface, wszFilterInstanceName4);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    IBaseFilter_Release(&ptestfilter2->base.IBaseFilter_iface);

    hr = createtestfilter(&GUID_NULL, PinData2, &ptestfilter2);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter2->base.IBaseFilter_iface, wszFilterInstanceName2);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    hr = IFilterGraph2_Render(pgraph2, ptestfilter->pins[0]);
    ok(hr == S_OK, "IFilterGraph2_Render failed with %08x\n", hr);

    get_connected_filter_name(ptestfilter, ConnectedFilterName2);
    ok(!strcmp(ConnectedFilterName2, "TestfilterInstance3") || !strcmp(ConnectedFilterName2, "TestfilterInstance2"),
            "unexpected connected filter: %s\n", ConnectedFilterName2);
    ok(strcmp(ConnectedFilterName1, ConnectedFilterName2),
        "expected connected filters to be different but got %s both times\n", ConnectedFilterName1);

    IFilterGraph2_Release(pgraph2);
    IBaseFilter_Release(&ptestfilter->base.IBaseFilter_iface);
    IBaseFilter_Release(&ptestfilter2->base.IBaseFilter_iface);

    /* Test if renderers are tried before non-renderers (intermediary filters). */
    pgraph2 = create_graph();

    hr = CoCreateInstance(&CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER, &IID_IFilterMapper2, (LPVOID*)&pMapper2);
    ok(hr == S_OK, "CoCreateInstance failed with %08x\n", hr);

    hr = createtestfilter(&GUID_NULL, PinData1, &ptestfilter);
    ok(hr == S_OK, "createtestfilter failed with %08x\n", hr);

    hr = IFilterGraph2_AddFilter(pgraph2, &ptestfilter->base.IBaseFilter_iface, wszFilterInstanceName1);
    ok(hr == S_OK, "IFilterGraph2_AddFilter failed with %08x\n", hr);

    /* Register our filters with COM and with Filtermapper. */
    hr = CoRegisterClassObject(Filter1ClassFactory.clsid,
            (IUnknown *)&Filter1ClassFactory.IClassFactory_iface, CLSCTX_INPROC_SERVER,
            REGCLS_MULTIPLEUSE, &cookie1);
    ok(hr == S_OK, "CoRegisterClassObject failed with %08x\n", hr);
    hr = CoRegisterClassObject(Filter2ClassFactory.clsid,
            (IUnknown *)&Filter2ClassFactory.IClassFactory_iface, CLSCTX_INPROC_SERVER,
            REGCLS_MULTIPLEUSE, &cookie2);
    ok(hr == S_OK, "CoRegisterClassObject failed with %08x\n", hr);
    hr = CoRegisterClassObject(Filter3ClassFactory.clsid,
            (IUnknown *)&Filter3ClassFactory.IClassFactory_iface, CLSCTX_INPROC_SERVER,
            REGCLS_MULTIPLEUSE, &cookie3);
    ok(hr == S_OK, "CoRegisterClassObject failed with %08x\n", hr);

    rgf2.dwVersion = 2;
    rgf2.dwMerit = MERIT_UNLIKELY;
    S2(U(rgf2)).cPins2 = 1;
    S2(U(rgf2)).rgPins2 = rgPins2;
    rgPins2[0].dwFlags = REG_PINFLAG_B_RENDERER;
    rgPins2[0].cInstances = 1;
    rgPins2[0].nMediaTypes = 1;
    rgPins2[0].lpMediaType = &rgPinType[0];
    rgPins2[0].nMediums = 0;
    rgPins2[0].lpMedium = NULL;
    rgPins2[0].clsPinCategory = NULL;
    rgPinType[0].clsMajorType = &MEDIATYPE_Video;
    rgPinType[0].clsMinorType = &mediasubtype1;

    hr = IFilterMapper2_RegisterFilter(pMapper2, &CLSID_TestFilter2, wszFilterInstanceName2, NULL,
                    &CLSID_LegacyAmFilterCategory, NULL, &rgf2);
    if (hr == E_ACCESSDENIED)
        skip("Not authorized to register filters\n");
    else
    {
        ok(hr == S_OK, "IFilterMapper2_RegisterFilter failed with %x\n", hr);

        rgf2.dwMerit = MERIT_PREFERRED;
        rgPinType[0].clsMinorType = &mediasubtype2;

        hr = IFilterMapper2_RegisterFilter(pMapper2, &CLSID_TestFilter4, wszFilterInstanceName4, NULL,
                    &CLSID_LegacyAmFilterCategory, NULL, &rgf2);
        ok(hr == S_OK, "IFilterMapper2_RegisterFilter failed with %x\n", hr);

        S2(U(rgf2)).cPins2 = 2;
        rgPins2[0].dwFlags = 0;
        rgPinType[0].clsMinorType = &mediasubtype1;

        rgPins2[1].dwFlags = REG_PINFLAG_B_OUTPUT;
        rgPins2[1].cInstances = 1;
        rgPins2[1].nMediaTypes = 1;
        rgPins2[1].lpMediaType = &rgPinType[1];
        rgPins2[1].nMediums = 0;
        rgPins2[1].lpMedium = NULL;
        rgPins2[1].clsPinCategory = NULL;
        rgPinType[1].clsMajorType = &MEDIATYPE_Video;
        rgPinType[1].clsMinorType = &mediasubtype2;

        hr = IFilterMapper2_RegisterFilter(pMapper2, &CLSID_TestFilter3, wszFilterInstanceName3, NULL,
                    &CLSID_LegacyAmFilterCategory, NULL, &rgf2);
        ok(hr == S_OK, "IFilterMapper2_RegisterFilter failed with %x\n", hr);

        hr = IFilterGraph2_Render(pgraph2, ptestfilter->pins[0]);
        ok(hr == S_OK, "IFilterGraph2_Render failed with %08x\n", hr);

        get_connected_filter_name(ptestfilter, ConnectedFilterName1);
        ok(!strcmp(ConnectedFilterName1, "TestfilterInstance3"),
           "unexpected connected filter: %s\n", ConnectedFilterName1);

        hr = IFilterMapper2_UnregisterFilter(pMapper2, &CLSID_LegacyAmFilterCategory, NULL,
                &CLSID_TestFilter2);
        ok(hr == S_OK, "IFilterMapper2_UnregisterFilter failed with %x\n", hr);
        hr = IFilterMapper2_UnregisterFilter(pMapper2, &CLSID_LegacyAmFilterCategory, NULL,
                &CLSID_TestFilter3);
        ok(hr == S_OK, "IFilterMapper2_UnregisterFilter failed with %x\n", hr);
        hr = IFilterMapper2_UnregisterFilter(pMapper2, &CLSID_LegacyAmFilterCategory, NULL,
                 &CLSID_TestFilter4);
        ok(hr == S_OK, "IFilterMapper2_UnregisterFilter failed with %x\n", hr);
    }

    IBaseFilter_Release(&ptestfilter->base.IBaseFilter_iface);
    IFilterGraph2_Release(pgraph2);
    IFilterMapper2_Release(pMapper2);

    hr = CoRevokeClassObject(cookie1);
    ok(hr == S_OK, "CoRevokeClassObject failed with %08x\n", hr);
    hr = CoRevokeClassObject(cookie2);
    ok(hr == S_OK, "CoRevokeClassObject failed with %08x\n", hr);
    hr = CoRevokeClassObject(cookie3);
    ok(hr == S_OK, "CoRevokeClassObject failed with %08x\n", hr);
}

typedef struct IUnknownImpl
{
    IUnknown IUnknown_iface;
    int AddRef_called;
    int Release_called;
} IUnknownImpl;

static IUnknownImpl *IUnknownImpl_from_iface(IUnknown * iface)
{
    return CONTAINING_RECORD(iface, IUnknownImpl, IUnknown_iface);
}

static HRESULT WINAPI IUnknownImpl_QueryInterface(IUnknown * iface, REFIID riid, LPVOID * ppv)
{
    ok(0, "QueryInterface should not be called for %s\n", wine_dbgstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI IUnknownImpl_AddRef(IUnknown * iface)
{
    IUnknownImpl *This = IUnknownImpl_from_iface(iface);
    This->AddRef_called++;
    return 2;
}

static ULONG WINAPI IUnknownImpl_Release(IUnknown * iface)
{
    IUnknownImpl *This = IUnknownImpl_from_iface(iface);
    This->Release_called++;
    return 1;
}

static CONST_VTBL IUnknownVtbl IUnknownImpl_Vtbl =
{
    IUnknownImpl_QueryInterface,
    IUnknownImpl_AddRef,
    IUnknownImpl_Release
};

static void test_aggregate_filter_graph(void)
{
    HRESULT hr;
    IUnknown *pgraph;
    IUnknown *punk;
    IUnknownImpl unk_outer = { { &IUnknownImpl_Vtbl }, 0, 0 };

    hr = CoCreateInstance(&CLSID_FilterGraph, &unk_outer.IUnknown_iface, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown, (void **)&pgraph);
    ok(hr == S_OK, "CoCreateInstance returned %x\n", hr);
    ok(pgraph != &unk_outer.IUnknown_iface, "pgraph = %p, expected not %p\n", pgraph, &unk_outer.IUnknown_iface);

    hr = IUnknown_QueryInterface(pgraph, &IID_IUnknown, (void **)&punk);
    ok(hr == S_OK, "CoCreateInstance returned %x\n", hr);
    ok(punk != &unk_outer.IUnknown_iface, "punk = %p, expected not %p\n", punk, &unk_outer.IUnknown_iface);
    IUnknown_Release(punk);

    ok(unk_outer.AddRef_called == 0, "IUnknownImpl_AddRef called %d times\n", unk_outer.AddRef_called);
    ok(unk_outer.Release_called == 0, "IUnknownImpl_Release called %d times\n", unk_outer.Release_called);
    unk_outer.AddRef_called = 0;
    unk_outer.Release_called = 0;

    hr = IUnknown_QueryInterface(pgraph, &IID_IFilterMapper, (void **)&punk);
    ok(hr == S_OK, "CoCreateInstance returned %x\n", hr);
    ok(punk != &unk_outer.IUnknown_iface, "punk = %p, expected not %p\n", punk, &unk_outer.IUnknown_iface);
    IUnknown_Release(punk);

    ok(unk_outer.AddRef_called == 1, "IUnknownImpl_AddRef called %d times\n", unk_outer.AddRef_called);
    ok(unk_outer.Release_called == 1, "IUnknownImpl_Release called %d times\n", unk_outer.Release_called);
    unk_outer.AddRef_called = 0;
    unk_outer.Release_called = 0;

    hr = IUnknown_QueryInterface(pgraph, &IID_IFilterMapper2, (void **)&punk);
    ok(hr == S_OK, "CoCreateInstance returned %x\n", hr);
    ok(punk != &unk_outer.IUnknown_iface, "punk = %p, expected not %p\n", punk, &unk_outer.IUnknown_iface);
    IUnknown_Release(punk);

    ok(unk_outer.AddRef_called == 1, "IUnknownImpl_AddRef called %d times\n", unk_outer.AddRef_called);
    ok(unk_outer.Release_called == 1, "IUnknownImpl_Release called %d times\n", unk_outer.Release_called);
    unk_outer.AddRef_called = 0;
    unk_outer.Release_called = 0;

    hr = IUnknown_QueryInterface(pgraph, &IID_IFilterMapper3, (void **)&punk);
    ok(hr == S_OK, "CoCreateInstance returned %x\n", hr);
    ok(punk != &unk_outer.IUnknown_iface, "punk = %p, expected not %p\n", punk, &unk_outer.IUnknown_iface);
    IUnknown_Release(punk);

    ok(unk_outer.AddRef_called == 1, "IUnknownImpl_AddRef called %d times\n", unk_outer.AddRef_called);
    ok(unk_outer.Release_called == 1, "IUnknownImpl_Release called %d times\n", unk_outer.Release_called);

    IUnknown_Release(pgraph);
}

START_TEST(filtergraph)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    test_render_run(avifile);
    test_render_run(mpegfile);
    test_graph_builder();
    test_render_filter_priority();
    test_aggregate_filter_graph();
    CoUninitialize();
    test_render_with_multithread();
}
