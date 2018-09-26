/*
 * Unit tests for DSound Renderer functions
 *
 * Copyright (C) 2010 Maarten Lankhorst for CodeWeavers
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
#include "initguid.h"
#include "dsound.h"
#include "amaudio.h"

static const WCHAR sink_id[] = {'A','u','d','i','o',' ','I','n','p','u','t',' ','p','i','n',' ','(','r','e','n','d','e','r','e','d',')',0};

static IBaseFilter *create_dsound_render(void)
{
    IBaseFilter *filter = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_DSoundRender, NULL, CLSCTX_INPROC_SERVER,
        &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    return filter;
}

#define check_interface(a, b, c) check_interface_(__LINE__, a, b, c)
static void check_interface_(unsigned int line, void *iface_ptr, REFIID iid, BOOL supported)
{
    IUnknown *iface = iface_ptr;
    HRESULT hr, expected_hr;
    IUnknown *unk;

    expected_hr = supported ? S_OK : E_NOINTERFACE;

    hr = IUnknown_QueryInterface(iface, iid, (void **)&unk);
    ok_(__FILE__, line)(hr == expected_hr, "Got hr %#x, expected %#x.\n", hr, expected_hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(unk);
}

static void test_interfaces(void)
{
    IBaseFilter *filter = create_dsound_render();

    check_interface(filter, &IID_IAMDirectSound, TRUE);
    check_interface(filter, &IID_IBaseFilter, TRUE);
    check_interface(filter, &IID_IBasicAudio, TRUE);
todo_wine
    check_interface(filter, &IID_IDirectSound3DBuffer, TRUE);
    check_interface(filter, &IID_IMediaPosition, TRUE);
    check_interface(filter, &IID_IMediaSeeking, TRUE);
todo_wine
    check_interface(filter, &IID_IPersistPropertyBag, TRUE);
    check_interface(filter, &IID_IQualityControl, TRUE);
    check_interface(filter, &IID_IReferenceClock, TRUE);

todo_wine
    check_interface(filter, &IID_IAMFilterMiscFlags, FALSE);
    check_interface(filter, &IID_IPin, FALSE);
    check_interface(filter, &IID_IVideoWindow, FALSE);

    IBaseFilter_Release(filter);
}

static void test_property_bag(void)
{
    IPersistPropertyBag *ppb;
    ICreateDevEnum *devenum;
    IEnumMoniker *enummon;
    IPropertyBag *propbag;
    IMoniker *mon;
    HRESULT hr;

    CoCreateInstance(&CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
        &IID_ICreateDevEnum, (void **)&devenum);
    ICreateDevEnum_CreateClassEnumerator(devenum, &CLSID_AudioRendererCategory, &enummon, 0);

    while (IEnumMoniker_Next(enummon, 1, &mon, NULL) == S_OK)
    {
        hr = CoCreateInstance(&CLSID_DSoundRender, NULL, CLSCTX_INPROC_SERVER,
            &IID_IPersistPropertyBag, (void **)&ppb);
todo_wine
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        if (hr != S_OK) break;

        IMoniker_BindToStorage(mon, NULL, NULL, &IID_IPropertyBag, (void **)&propbag);

        hr = IPersistPropertyBag_InitNew(ppb);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        hr = IPersistPropertyBag_Load(ppb, propbag, NULL);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        IPersistPropertyBag_Release(ppb);
        IPropertyBag_Release(propbag);
        IMoniker_Release(mon);
    }

    IEnumMoniker_Release(enummon);
    ICreateDevEnum_Release(devenum);
}

static void test_enum_pins(void)
{
    IBaseFilter *filter = create_dsound_render();
    IEnumPins *enum1, *enum2;
    IPin *pins[2];
    ULONG count;
    HRESULT hr;

    hr = IBaseFilter_EnumPins(filter, &enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IPin_Release(pins[0]);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumPins_Reset(enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum1, 1, pins, &count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 1, "Got count %u.\n", count);
    IPin_Release(pins[0]);

    hr = IEnumPins_Next(enum1, 1, pins, &count);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(!count, "Got count %u.\n", count);

    hr = IEnumPins_Reset(enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum1, 2, pins, NULL);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum1, 2, pins, &count);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(count == 1, "Got count %u.\n", count);
    IPin_Release(pins[0]);

    hr = IEnumPins_Reset(enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Clone(enum1, &enum2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Skip(enum1, 2);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumPins_Skip(enum1, 1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Skip(enum1, 1);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum2, 1, pins, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IPin_Release(pins[0]);

    IEnumPins_Release(enum2);
    IEnumPins_Release(enum1);
    IBaseFilter_Release(filter);
}

static void test_find_pin(void)
{
    static const WCHAR inW[] = {'I','n',0};
    static const WCHAR input_pinW[] = {'i','n','p','u','t',' ','p','i','n',0};
    IBaseFilter *filter = create_dsound_render();
    IEnumPins *enum_pins;
    IPin *pin, *pin2;
    HRESULT hr;
    ULONG ref;

    hr = IBaseFilter_FindPin(filter, inW, &pin);
    ok(hr == VFW_E_NOT_FOUND, "Got hr %#x.\n", hr);

    hr = IBaseFilter_FindPin(filter, input_pinW, &pin);
    ok(hr == VFW_E_NOT_FOUND, "Got hr %#x.\n", hr);

    hr = IBaseFilter_FindPin(filter, sink_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_EnumPins(filter, &enum_pins);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum_pins, 1, &pin2, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(pin == pin2, "Expected pin %p, got %p.\n", pin2, pin);
    IPin_Release(pin);
    IPin_Release(pin2);

    IEnumPins_Release(enum_pins);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

static void test_pin(IPin *pin)
{
    IMemInputPin *mpin = NULL;

    IPin_QueryInterface(pin, &IID_IMemInputPin, (void **)&mpin);

    ok(mpin != NULL, "No IMemInputPin found!\n");
    if (mpin)
    {
        ok(IMemInputPin_ReceiveCanBlock(mpin) == S_OK, "Receive can't block for pin!\n");
        ok(IMemInputPin_NotifyAllocator(mpin, NULL, 0) == E_POINTER, "NotifyAllocator likes a NULL pointer argument\n");
        IMemInputPin_Release(mpin);
    }
    /* TODO */
}

static void test_basefilter(void)
{
    IEnumPins *pin_enum = NULL;
    IBaseFilter *base = create_dsound_render();
    IPin *pins[2];
    ULONG ref;
    HRESULT hr;

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

START_TEST(dsoundrender)
{
    IBaseFilter *filter;
    HRESULT hr;

    CoInitialize(NULL);

    hr = CoCreateInstance(&CLSID_DSoundRender, NULL, CLSCTX_INPROC_SERVER,
        &IID_IBaseFilter, (void **)&filter);
    if (hr == VFW_E_NO_AUDIO_HARDWARE)
    {
        skip("No audio hardware.\n");
        CoUninitialize();
        return;
    }
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IBaseFilter_Release(filter);

    test_property_bag();
    test_interfaces();
    test_enum_pins();
    test_find_pin();
    test_basefilter();

    CoUninitialize();
}
