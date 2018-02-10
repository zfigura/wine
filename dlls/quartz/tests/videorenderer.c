/*
 * Unit tests for Video Renderer functions
 *
 * Copyright (C) 2007 Google (Lei Zhang)
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

#include "wine/test.h"
#include "dshow.h"

#define QI_SUCCEED(iface, riid, ppv) hr = IUnknown_QueryInterface(iface, &riid, (LPVOID*)&ppv); \
    ok(hr == S_OK, "IUnknown_QueryInterface returned %x\n", hr); \
    ok(ppv != NULL, "Pointer is NULL\n");

#define RELEASE_EXPECT(iface, num) if (iface) { \
    hr = IUnknown_Release((IUnknown*)iface); \
    ok(hr == num, "IUnknown_Release should return %d, got %d\n", num, hr); \
}

static IUnknown *pVideoRenderer = NULL;

static int create_video_renderer(void)
{
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_VideoRenderer, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown, (LPVOID*)&pVideoRenderer);
    return (hr == S_OK && pVideoRenderer != NULL);
}

static void release_video_renderer(void)
{
    HRESULT hr;

    hr = IUnknown_Release(pVideoRenderer);
    ok(hr == 0, "IUnknown_Release failed with %x\n", hr);
}

static void test_query_interface(void)
{
    HRESULT hr;
    IBaseFilter *pBaseFilter = NULL;
    IBasicVideo *pBasicVideo = NULL;
    IDirectDrawVideo *pDirectDrawVideo = NULL;
    IKsPropertySet *pKsPropertySet = NULL;
    IMediaPosition *pMediaPosition = NULL;
    IMediaSeeking *pMediaSeeking = NULL;
    IQualityControl *pQualityControl = NULL;
    IQualProp *pQualProp = NULL;
    IVideoWindow *pVideoWindow = NULL;

    QI_SUCCEED(pVideoRenderer, IID_IBaseFilter, pBaseFilter);
    RELEASE_EXPECT(pBaseFilter, 1);
    QI_SUCCEED(pVideoRenderer, IID_IBasicVideo, pBasicVideo);
    RELEASE_EXPECT(pBasicVideo, 1);
    QI_SUCCEED(pVideoRenderer, IID_IMediaSeeking, pMediaSeeking);
    RELEASE_EXPECT(pMediaSeeking, 1);
    QI_SUCCEED(pVideoRenderer, IID_IQualityControl, pQualityControl);
    RELEASE_EXPECT(pQualityControl, 1);
    todo_wine {
    QI_SUCCEED(pVideoRenderer, IID_IDirectDrawVideo, pDirectDrawVideo);
    RELEASE_EXPECT(pDirectDrawVideo, 1);
    QI_SUCCEED(pVideoRenderer, IID_IKsPropertySet, pKsPropertySet);
    RELEASE_EXPECT(pKsPropertySet, 1);
    QI_SUCCEED(pVideoRenderer, IID_IQualProp, pQualProp);
    RELEASE_EXPECT(pQualProp, 1);
    }
    QI_SUCCEED(pVideoRenderer, IID_IMediaPosition, pMediaPosition);
    RELEASE_EXPECT(pMediaPosition, 1);
    QI_SUCCEED(pVideoRenderer, IID_IVideoWindow, pVideoWindow);
    RELEASE_EXPECT(pVideoWindow, 1);
}

static const struct {
    const GUID *subtype;
    BOOL todo;
} vr_subtype_tests[] =
{
    {&MEDIASUBTYPE_RGB1, TRUE},
    {&MEDIASUBTYPE_RGB4, TRUE},
    {&MEDIASUBTYPE_RGB8},
    {&MEDIASUBTYPE_RGB565},
    {&MEDIASUBTYPE_RGB555, TRUE},
    {&MEDIASUBTYPE_RGB24},
    {&MEDIASUBTYPE_RGB32},
    {&MEDIASUBTYPE_ARGB32, TRUE},
};

static void test_pin(IPin *pin)
{
    IMemInputPin *mpin = NULL;
    VIDEOINFOHEADER vih = {0};
    AM_MEDIA_TYPE mt = {0};
    HRESULT hr;
    int i;

    IPin_QueryInterface(pin, &IID_IMemInputPin, (void **)&mpin);

    ok(mpin != NULL, "No IMemInputPin found!\n");
    if (mpin)
    {
        ok(IMemInputPin_ReceiveCanBlock(mpin) == S_OK, "Receive can't block for pin!\n");
        ok(IMemInputPin_NotifyAllocator(mpin, NULL, 0) == E_POINTER, "NotifyAllocator likes a NULL pointer argument\n");
        IMemInputPin_Release(mpin);
    }
    /* TODO */

    vih.bmiHeader.biSize = sizeof(vih.bmiHeader);
    vih.bmiHeader.biWidth = 320;
    vih.bmiHeader.biHeight = 240;
    vih.bmiHeader.biPlanes = 1;
    vih.bmiHeader.biCompression = BI_RGB;
    mt.majortype = MEDIATYPE_Video;
    mt.formattype = FORMAT_VideoInfo;
    mt.cbFormat = sizeof(vih);
    mt.pbFormat = (BYTE *)&vih;

    for (i = 0; i < sizeof(vr_subtype_tests)/sizeof(vr_subtype_tests[0]); i++)
    {
        mt.subtype = *vr_subtype_tests[i].subtype;
        hr = IPin_QueryAccept(pin, &mt);
        todo_wine_if(vr_subtype_tests[i].todo)
        ok(hr == S_OK, "got %#x for %s\n", hr, wine_dbgstr_guid(vr_subtype_tests[i].subtype));
    }
}

static void test_basefilter(void)
{
    IEnumPins *pin_enum = NULL;
    IBaseFilter *base = NULL;
    IPin *pins[2];
    ULONG ref;
    HRESULT hr;

    IUnknown_QueryInterface(pVideoRenderer, &IID_IBaseFilter, (void **)&base);
    if (base == NULL)
    {
        /* test_query_interface handles this case */
        skip("No IBaseFilter\n");
        return;
    }

    hr = IBaseFilter_EnumPins(base, NULL);
    ok(hr == E_POINTER, "hr = %08x and not E_POINTER\n", hr);

    hr= IBaseFilter_EnumPins(base, &pin_enum);
    ok(hr == S_OK, "hr = %08x and not S_OK\n", hr);

    hr = IEnumPins_Next(pin_enum, 1, NULL, NULL);
    ok(hr == E_POINTER, "hr = %08x and not E_POINTER\n", hr);

    hr = IEnumPins_Next(pin_enum, 2, pins, NULL);
    ok(hr == E_INVALIDARG, "hr = %08x and not E_INVALIDARG\n", hr);

    pins[0] = (void *)0xdead;
    pins[1] = (void *)0xdeed;

    hr = IEnumPins_Next(pin_enum, 2, pins, &ref);
    ok(hr == S_FALSE, "hr = %08x instead of S_FALSE\n", hr);
    ok(pins[0] != (void *)0xdead && pins[0] != NULL, "pins[0] = %p\n", pins[0]);
    if (pins[0] != (void *)0xdead && pins[0] != NULL)
    {
        test_pin(pins[0]);
        IPin_Release(pins[0]);
    }

    ok(pins[1] == (void *)0xdeed, "pins[1] = %p\n", pins[1]);

    ref = IEnumPins_Release(pin_enum);
    ok(ref == 0, "ref is %u and not 0!\n", ref);

    IBaseFilter_Release(base);
}

START_TEST(videorenderer)
{
    CoInitialize(NULL);
    if (!create_video_renderer())
        return;

    test_query_interface();
    test_basefilter();

    release_video_renderer();

    CoUninitialize();
}
