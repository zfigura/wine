/*
 * DirectSound renderer filter unit tests
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
#include <limits.h>
#include <math.h>
#include "dshow.h"
#include "initguid.h"
#include "dsound.h"
#include "amaudio.h"
#include "wine/heap.h"
#include "wine/strmbase.h"
#include "wine/test.h"

static const WCHAR sink_id[] = {'A','u','d','i','o',' ','I','n','p','u','t',' ','p','i','n',' ','(','r','e','n','d','e','r','e','d',')',0};

static IBaseFilter *create_dsound_render(void)
{
    IBaseFilter *filter = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_DSoundRender, NULL, CLSCTX_INPROC_SERVER,
            &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    return filter;
}

static inline BOOL compare_media_types(const AM_MEDIA_TYPE *a, const AM_MEDIA_TYPE *b)
{
    return !memcmp(a, b, offsetof(AM_MEDIA_TYPE, pbFormat))
        && !memcmp(a->pbFormat, b->pbFormat, a->cbFormat);
}

static IFilterGraph2 *create_graph(void)
{
    IFilterGraph2 *ret;
    HRESULT hr;
    hr = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IFilterGraph2, (void **)&ret);
    ok(hr == S_OK, "Failed to create FilterGraph: %#x\n", hr);
    return ret;
}

static ULONG get_refcount(void *iface)
{
    IUnknown *unknown = iface;
    IUnknown_AddRef(unknown);
    return IUnknown_Release(unknown);
}

static HRESULT WINAPI property_bag_QueryInterface(IPropertyBag *iface, REFIID iid, void **out)
{
    ok(0, "Unexpected call (iid %s).\n", wine_dbgstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI property_bag_AddRef(IPropertyBag *iface)
{
    ok(0, "Unexpected call.\n");
    return 2;
}

static ULONG WINAPI property_bag_Release(IPropertyBag *iface)
{
    ok(0, "Unexpected call.\n");
    return 1;
}

static HRESULT WINAPI property_bag_Read(IPropertyBag *iface, const WCHAR *name, VARIANT *var, IErrorLog *log)
{
    static const WCHAR dsguidW[] = {'D','S','G','u','i','d',0};
    WCHAR guidstr[39];

    ok(!lstrcmpW(name, dsguidW), "Got unexpected name %s.\n", wine_dbgstr_w(name));
    ok(V_VT(var) == VT_BSTR, "Got unexpected type %u.\n", V_VT(var));
    StringFromGUID2(&DSDEVID_DefaultPlayback, guidstr, ARRAY_SIZE(guidstr));
    V_BSTR(var) = SysAllocString(guidstr);
    return S_OK;
}

static HRESULT WINAPI property_bag_Write(IPropertyBag *iface, const WCHAR *name, VARIANT *var)
{
    ok(0, "Unexpected call (name %s).\n", wine_dbgstr_w(name));
    return E_FAIL;
}

static const IPropertyBagVtbl property_bag_vtbl =
{
    property_bag_QueryInterface,
    property_bag_AddRef,
    property_bag_Release,
    property_bag_Read,
    property_bag_Write,
};

static void test_property_bag(void)
{
    IPropertyBag property_bag = {&property_bag_vtbl};
    IPersistPropertyBag *ppb;
    HRESULT hr;
    ULONG ref;

    hr = CoCreateInstance(&CLSID_DSoundRender, NULL, CLSCTX_INPROC_SERVER,
            &IID_IPersistPropertyBag, (void **)&ppb);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
    if (hr != S_OK) return;

    hr = IPersistPropertyBag_InitNew(ppb);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPersistPropertyBag_Load(ppb, &property_bag, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    ref = IPersistPropertyBag_Release(ppb);
    ok(!ref, "Got unexpected refcount %d.\n", ref);
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
    IPin *pin;

    check_interface(filter, &IID_IAMDirectSound, TRUE);
    check_interface(filter, &IID_IBaseFilter, TRUE);
    check_interface(filter, &IID_IBasicAudio, TRUE);
    todo_wine check_interface(filter, &IID_IDirectSound3DBuffer, TRUE);
    check_interface(filter, &IID_IMediaFilter, TRUE);
    check_interface(filter, &IID_IMediaPosition, TRUE);
    check_interface(filter, &IID_IMediaSeeking, TRUE);
    check_interface(filter, &IID_IPersist, TRUE);
    todo_wine check_interface(filter, &IID_IPersistPropertyBag, TRUE);
    check_interface(filter, &IID_IQualityControl, TRUE);
    check_interface(filter, &IID_IReferenceClock, TRUE);
    check_interface(filter, &IID_IUnknown, TRUE);

    check_interface(filter, &IID_IAMFilterMiscFlags, FALSE);
    check_interface(filter, &IID_IBasicVideo, FALSE);
    check_interface(filter, &IID_IDispatch, FALSE);
    check_interface(filter, &IID_IKsPropertySet, FALSE);
    check_interface(filter, &IID_IPin, FALSE);
    check_interface(filter, &IID_IQualProp, FALSE);
    check_interface(filter, &IID_IVideoWindow, FALSE);

    IBaseFilter_FindPin(filter, sink_id, &pin);

    check_interface(pin, &IID_IPin, TRUE);
    check_interface(pin, &IID_IMemInputPin, TRUE);
    todo_wine check_interface(pin, &IID_IQualityControl, TRUE);
    check_interface(pin, &IID_IUnknown, TRUE);

    check_interface(pin, &IID_IAsyncReader, FALSE);
    check_interface(pin, &IID_IMediaPosition, FALSE);
    todo_wine check_interface(pin, &IID_IMediaSeeking, FALSE);

    IPin_Release(pin);
    IBaseFilter_Release(filter);
}

static const GUID test_iid = {0x33333333};
static LONG outer_ref = 1;

static HRESULT WINAPI outer_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IBaseFilter)
            || IsEqualGUID(iid, &test_iid))
    {
        *out = (IUnknown *)0xdeadbeef;
        return S_OK;
    }
    ok(0, "unexpected call %s\n", wine_dbgstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI outer_AddRef(IUnknown *iface)
{
    return InterlockedIncrement(&outer_ref);
}

static ULONG WINAPI outer_Release(IUnknown *iface)
{
    return InterlockedDecrement(&outer_ref);
}

static const IUnknownVtbl outer_vtbl =
{
    outer_QueryInterface,
    outer_AddRef,
    outer_Release,
};

static IUnknown test_outer = {&outer_vtbl};

static void test_aggregation(void)
{
    IBaseFilter *filter, *filter2;
    IUnknown *unk, *unk2;
    HRESULT hr;
    ULONG ref;

    filter = (IBaseFilter *)0xdeadbeef;
    hr = CoCreateInstance(&CLSID_DSoundRender, &test_outer, CLSCTX_INPROC_SERVER,
            &IID_IBaseFilter, (void **)&filter);
    ok(hr == E_NOINTERFACE, "Got hr %#x.\n", hr);
    ok(!filter, "Got interface %p.\n", filter);

    hr = CoCreateInstance(&CLSID_DSoundRender, &test_outer, CLSCTX_INPROC_SERVER,
            &IID_IUnknown, (void **)&unk);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(outer_ref == 1, "Got unexpected refcount %d.\n", outer_ref);
    ok(unk != &test_outer, "Returned IUnknown should not be outer IUnknown.\n");
    ref = get_refcount(unk);
    ok(ref == 1, "Got unexpected refcount %d.\n", ref);

    ref = IUnknown_AddRef(unk);
    ok(ref == 2, "Got unexpected refcount %d.\n", ref);
    ok(outer_ref == 1, "Got unexpected refcount %d.\n", outer_ref);

    ref = IUnknown_Release(unk);
    ok(ref == 1, "Got unexpected refcount %d.\n", ref);
    ok(outer_ref == 1, "Got unexpected refcount %d.\n", outer_ref);

    hr = IUnknown_QueryInterface(unk, &IID_IUnknown, (void **)&unk2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(unk2 == unk, "Got unexpected IUnknown %p.\n", unk2);
    IUnknown_Release(unk2);

    hr = IUnknown_QueryInterface(unk, &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_QueryInterface(filter, &IID_IUnknown, (void **)&unk2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(unk2 == (IUnknown *)0xdeadbeef, "Got unexpected IUnknown %p.\n", unk2);

    hr = IBaseFilter_QueryInterface(filter, &IID_IBaseFilter, (void **)&filter2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(filter2 == (IBaseFilter *)0xdeadbeef, "Got unexpected IBaseFilter %p.\n", filter2);

    hr = IUnknown_QueryInterface(unk, &test_iid, (void **)&unk2);
    ok(hr == E_NOINTERFACE, "Got hr %#x.\n", hr);
    ok(!unk2, "Got unexpected IUnknown %p.\n", unk2);

    hr = IBaseFilter_QueryInterface(filter, &test_iid, (void **)&unk2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(unk2 == (IUnknown *)0xdeadbeef, "Got unexpected IUnknown %p.\n", unk2);

    IBaseFilter_Release(filter);
    ref = IUnknown_Release(unk);
    ok(!ref, "Got unexpected refcount %d.\n", ref);
    ok(outer_ref == 1, "Got unexpected refcount %d.\n", outer_ref);
}

static void test_enum_pins(void)
{
    IBaseFilter *filter = create_dsound_render();
    IEnumPins *enum1, *enum2;
    ULONG count, ref;
    IPin *pins[2];
    HRESULT hr;

    ref = get_refcount(filter);
    ok(ref == 1, "Got unexpected refcount %d.\n", ref);

    hr = IBaseFilter_EnumPins(filter, NULL);
    ok(hr == E_POINTER, "Got hr %#x.\n", hr);

    hr = IBaseFilter_EnumPins(filter, &enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ref = get_refcount(filter);
    ok(ref == 2, "Got unexpected refcount %d.\n", ref);
    ref = get_refcount(enum1);
    ok(ref == 1, "Got unexpected refcount %d.\n", ref);

    hr = IEnumPins_Next(enum1, 1, NULL, NULL);
    ok(hr == E_POINTER, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ref = get_refcount(filter);
    ok(ref == 3, "Got unexpected refcount %d.\n", ref);
    ref = get_refcount(pins[0]);
    ok(ref == 3, "Got unexpected refcount %d.\n", ref);
    ref = get_refcount(enum1);
    ok(ref == 1, "Got unexpected refcount %d.\n", ref);
    IPin_Release(pins[0]);
    ref = get_refcount(filter);
    ok(ref == 2, "Got unexpected refcount %d.\n", ref);

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
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
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

static void test_pin_info(void)
{
    IBaseFilter *filter = create_dsound_render();
    PIN_DIRECTION dir;
    PIN_INFO info;
    HRESULT hr;
    WCHAR *id;
    ULONG ref;
    IPin *pin;

    hr = IBaseFilter_FindPin(filter, sink_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ref = get_refcount(filter);
    ok(ref == 2, "Got unexpected refcount %d.\n", ref);
    ref = get_refcount(pin);
    ok(ref == 2, "Got unexpected refcount %d.\n", ref);

    hr = IPin_QueryPinInfo(pin, &info);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(info.pFilter == filter, "Expected filter %p, got %p.\n", filter, info.pFilter);
    ok(info.dir == PINDIR_INPUT, "Got direction %d.\n", info.dir);
    ok(!lstrcmpW(info.achName, sink_id), "Got name %s.\n", wine_dbgstr_w(info.achName));
    ref = get_refcount(filter);
    ok(ref == 3, "Got unexpected refcount %d.\n", ref);
    ref = get_refcount(pin);
    ok(ref == 3, "Got unexpected refcount %d.\n", ref);
    IBaseFilter_Release(info.pFilter);

    hr = IPin_QueryDirection(pin, &dir);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(dir == PINDIR_INPUT, "Got direction %d.\n", dir);

    hr = IPin_QueryId(pin, &id);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!lstrcmpW(id, sink_id), "Got id %s.\n", wine_dbgstr_w(id));
    CoTaskMemFree(id);

    hr = IPin_QueryInternalConnections(pin, NULL, NULL);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);

    IPin_Release(pin);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

static void test_basic_audio(void)
{
    IBaseFilter *filter = create_dsound_render();
    LONG balance, volume;
    ITypeInfo *typeinfo;
    IBasicAudio *audio;
    TYPEATTR *typeattr;
    ULONG ref, count;
    HRESULT hr;

    hr = IBaseFilter_QueryInterface(filter, &IID_IBasicAudio, (void **)&audio);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBasicAudio_get_Balance(audio, NULL);
    ok(hr == E_POINTER, "Got hr %#x.\n", hr);

    hr = IBasicAudio_get_Balance(audio, &balance);
    if (hr != VFW_E_MONO_AUDIO_HW)
    {
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(balance == 0, "Got balance %d.\n", balance);

        hr = IBasicAudio_put_Balance(audio, DSBPAN_LEFT - 1);
        ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);

        hr = IBasicAudio_put_Balance(audio, DSBPAN_LEFT);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        hr = IBasicAudio_get_Balance(audio, &balance);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(balance == DSBPAN_LEFT, "Got balance %d.\n", balance);
    }

    hr = IBasicAudio_get_Volume(audio, &volume);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(volume == 0, "Got volume %d.\n", volume);

    hr = IBasicAudio_put_Volume(audio, DSBVOLUME_MIN - 1);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);

    hr = IBasicAudio_put_Volume(audio, DSBVOLUME_MIN);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBasicAudio_get_Volume(audio, &volume);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(volume == DSBVOLUME_MIN, "Got volume %d.\n", volume);

    hr = IBasicAudio_GetTypeInfoCount(audio, &count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 1, "Got count %u.\n", count);

    hr = IBasicAudio_GetTypeInfo(audio, 0, 0, &typeinfo);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = ITypeInfo_GetTypeAttr(typeinfo, &typeattr);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(typeattr->typekind == TKIND_DISPATCH, "Got kind %u.\n", typeattr->typekind);
    ok(IsEqualGUID(&typeattr->guid, &IID_IBasicAudio), "Got IID %s.\n", wine_dbgstr_guid(&typeattr->guid));
    ITypeInfo_ReleaseTypeAttr(typeinfo, typeattr);
    ITypeInfo_Release(typeinfo);

    IBasicAudio_Release(audio);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

static void test_enum_media_types(void)
{
    IBaseFilter *filter = create_dsound_render();
    IEnumMediaTypes *enum1, *enum2;
    AM_MEDIA_TYPE *mts[2];
    ULONG ref, count;
    HRESULT hr;
    IPin *pin;

    IBaseFilter_FindPin(filter, sink_id, &pin);

    hr = IPin_EnumMediaTypes(pin, &enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Next(enum1, 1, mts, NULL);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
    if (hr == S_OK) CoTaskMemFree(mts[0]->pbFormat);
    if (hr == S_OK) CoTaskMemFree(mts[0]);

    hr = IEnumMediaTypes_Next(enum1, 1, mts, NULL);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Reset(enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Next(enum1, 1, mts, &count);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
    todo_wine ok(count == 1, "Got count %u.\n", count);
    if (hr == S_OK) CoTaskMemFree(mts[0]->pbFormat);
    if (hr == S_OK) CoTaskMemFree(mts[0]);

    hr = IEnumMediaTypes_Next(enum1, 1, mts, &count);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(!count, "Got count %u.\n", count);

    hr = IEnumMediaTypes_Reset(enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Next(enum1, 2, mts, &count);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    todo_wine ok(count == 1, "Got count %u.\n", count);
    if (count > 0) CoTaskMemFree(mts[0]->pbFormat);
    if (count > 0) CoTaskMemFree(mts[0]);

    hr = IEnumMediaTypes_Reset(enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Clone(enum1, &enum2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Skip(enum1, 2);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Skip(enum1, 1);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Reset(enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Skip(enum1, 1);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Skip(enum1, 1);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Next(enum1, 1, mts, NULL);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Next(enum2, 1, mts, NULL);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
    if (hr == S_OK) CoTaskMemFree(mts[0]->pbFormat);
    if (hr == S_OK) CoTaskMemFree(mts[0]);

    IEnumMediaTypes_Release(enum1);
    IEnumMediaTypes_Release(enum2);
    IPin_Release(pin);

    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

struct testfilter
{
    struct strmbase_filter filter;
    struct strmbase_source source;
};

static const IBaseFilterVtbl testfilter_vtbl =
{
    BaseFilterImpl_QueryInterface,
    BaseFilterImpl_AddRef,
    BaseFilterImpl_Release,
    BaseFilterImpl_GetClassID,
    BaseFilterImpl_Stop,
    BaseFilterImpl_Pause,
    BaseFilterImpl_Run,
    BaseFilterImpl_GetState,
    BaseFilterImpl_SetSyncSource,
    BaseFilterImpl_GetSyncSource,
    BaseFilterImpl_EnumPins,
    BaseFilterImpl_FindPin,
    BaseFilterImpl_QueryFilterInfo,
    BaseFilterImpl_JoinFilterGraph,
    BaseFilterImpl_QueryVendorInfo,
};

static inline struct testfilter *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct testfilter, filter);
}

static IPin *testfilter_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct testfilter *filter = impl_from_strmbase_filter(iface);
    if (!index)
        return &filter->source.pin.IPin_iface;
    return NULL;
}

static void testfilter_destroy(struct strmbase_filter *iface)
{
    struct testfilter *filter = impl_from_strmbase_filter(iface);
    strmbase_source_cleanup(&filter->source);
    strmbase_filter_cleanup(&filter->filter);
}

static const struct strmbase_filter_ops testfilter_ops =
{
    .filter_get_pin = testfilter_get_pin,
    .filter_destroy = testfilter_destroy,
};

static const IPinVtbl testsource_vtbl =
{
    BaseOutputPinImpl_QueryInterface,
    BasePinImpl_AddRef,
    BasePinImpl_Release,
    BaseOutputPinImpl_Connect,
    BaseOutputPinImpl_ReceiveConnection,
    BasePinImpl_Disconnect,
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
    BasePinImpl_NewSegment,
};

static HRESULT testsource_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    return S_OK;
}

static HRESULT WINAPI testsource_AttemptConnection(struct strmbase_source *iface,
        IPin *peer, const AM_MEDIA_TYPE *mt)
{
    HRESULT hr;

    iface->pin.peer = peer;
    IPin_AddRef(peer);
    CopyMediaType(&iface->pin.mtCurrent, mt);

    if (FAILED(hr = IPin_ReceiveConnection(peer, &iface->pin.IPin_iface, mt)))
    {
        ok(hr == VFW_E_TYPE_NOT_ACCEPTED, "Got hr %#x.\n", hr);
        IPin_Release(peer);
        iface->pin.peer = NULL;
        FreeMediaType(&iface->pin.mtCurrent);
    }

    return hr;
}

static const struct strmbase_source_ops testsource_ops =
{
    .base.pin_query_accept = testsource_query_accept,
    .base.pfnGetMediaType = BasePinImpl_GetMediaType,
    .pfnAttemptConnection = testsource_AttemptConnection,
};

static void testfilter_init(struct testfilter *filter)
{
    static const GUID clsid = {0xabacab};
    static const WCHAR name[] = {0};
    strmbase_filter_init(&filter->filter, &testfilter_vtbl, NULL, &clsid, &testfilter_ops);
    strmbase_source_init(&filter->source, &testsource_vtbl, &filter->filter, name, &testsource_ops);
}

static void test_allocator(IMemInputPin *input)
{
    IMemAllocator *req_allocator, *ret_allocator;
    ALLOCATOR_PROPERTIES props;
    HRESULT hr;

    hr = IMemInputPin_GetAllocatorRequirements(input, &props);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);

    hr = IMemInputPin_GetAllocator(input, &ret_allocator);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    if (hr == S_OK)
    {
        hr = IMemAllocator_GetProperties(ret_allocator, &props);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(!props.cBuffers, "Got %d buffers.\n", props.cBuffers);
        ok(!props.cbBuffer, "Got size %d.\n", props.cbBuffer);
        ok(!props.cbAlign, "Got alignment %d.\n", props.cbAlign);
        ok(!props.cbPrefix, "Got prefix %d.\n", props.cbPrefix);

        hr = IMemInputPin_NotifyAllocator(input, ret_allocator, TRUE);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        IMemAllocator_Release(ret_allocator);
    }

    hr = IMemInputPin_NotifyAllocator(input, NULL, TRUE);
    ok(hr == E_POINTER, "Got hr %#x.\n", hr);

    CoCreateInstance(&CLSID_MemoryAllocator, NULL, CLSCTX_INPROC_SERVER,
            &IID_IMemAllocator, (void **)&req_allocator);

    hr = IMemInputPin_NotifyAllocator(input, req_allocator, TRUE);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemInputPin_GetAllocator(input, &ret_allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(ret_allocator == req_allocator, "Allocators didn't match.\n");

    IMemAllocator_Release(req_allocator);
    IMemAllocator_Release(ret_allocator);
}

struct frame_thread_params
{
    IMemInputPin *sink;
    IMediaSample *sample;
};

static DWORD WINAPI frame_thread(void *arg)
{
    struct frame_thread_params *params = arg;
    HRESULT hr;

    if (winetest_debug > 1) trace("%04x: Sending frame.\n", GetCurrentThreadId());
    hr = IMemInputPin_Receive(params->sink, params->sample);
    if (winetest_debug > 1) trace("%04x: Returned %#x.\n", GetCurrentThreadId(), hr);
    IMediaSample_Release(params->sample);
    heap_free(params);
    return hr;
}

static HRESULT send_frame_params(IMemInputPin *sink, REFERENCE_TIME start_time,
        REFERENCE_TIME length, BOOL discontinuity, double frequency)
{
    struct frame_thread_params *params = heap_alloc(sizeof(params));
    IMemAllocator *allocator;
    REFERENCE_TIME end_time;
    unsigned short *words;
    IMediaSample *sample;
    HANDLE thread;
    HRESULT hr;
    BYTE *data;
    DWORD ret;

    hr = IMemInputPin_GetAllocator(sink, &allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetPointer(sample, &data);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    words = (unsigned short *)data;
    for (int i = 0; i < 44100 * 2; i += 2)
        words[i] = words[i+1] = sin(M_PI * i * frequency / 22050.0) * 0x7fff;

    hr = IMediaSample_SetActualDataLength(sample, (LONGLONG)44100 * 4 * length / 10000000);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_SetDiscontinuity(sample, discontinuity);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    end_time = start_time + length;
    hr = IMediaSample_SetTime(sample, &start_time, &end_time);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    params->sink = sink;
    params->sample = sample;
    thread = CreateThread(NULL, 0, frame_thread, params, 0, NULL);
    ok(!WaitForSingleObject(thread, 500), "Wait failed.\n");
    GetExitCodeThread(thread, &ret);
    CloseHandle(thread);

    IMemAllocator_Release(allocator);
    return ret;
}

static HRESULT send_frame(IMemInputPin *sink)
{
    return send_frame_params(sink, 0, 10000000, FALSE, 440.0);
}

static void test_filter_state(IMemInputPin *input, IFilterGraph2 *graph)
{
    IMemAllocator *allocator;
    IMediaControl *control;
    IMediaSample *sample;
    OAFilterState state;
    HRESULT hr;

    hr = IMemInputPin_GetAllocator(input, &allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);

    hr = send_frame(input);
    ok(hr == VFW_E_WRONG_STATE, "Got hr %#x.\n", hr);

    /* The renderer is not fully paused until it receives a sample. The
     * DirectSound renderer never blocks in Receive(), despite returning S_OK
     * from ReceiveCanBlock(). Instead it holds on to each sample until its
     * presentation time, then writes it into the buffer. This is more work
     * than it's worth to emulate, so for now, we'll ignore this behaviour. */

    hr = IMediaControl_Pause(control);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    /* It's possible to queue multiple samples while paused. The number of
     * samples that can be queued depends on the length of each sample, but
     * it's not particularly clear how. */

    hr = IMediaControl_GetState(control, 0, &state);
    todo_wine ok(hr == VFW_S_STATE_INTERMEDIATE, "Got hr %#x.\n", hr);

    hr = send_frame(input);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, AM_GBF_NOWAIT);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
    if (hr == S_OK) IMediaSample_Release(sample);

    hr = send_frame(input);
    ok(hr == VFW_E_WRONG_STATE, "Got hr %#x.\n", hr);

    hr = IMediaControl_Pause(control);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    todo_wine ok(hr == VFW_S_STATE_INTERMEDIATE, "Got hr %#x.\n", hr);

    hr = send_frame(input);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_Run(control);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = send_frame(input);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = send_frame(input);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_Run(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_Pause(control);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    /* The DirectSound renderer will silently refuse to transition to running
     * if it hasn't finished pausing yet. Once it does it reports itself as
     * completely paused. This confuses the filter graph, which returns
     * VFW_S_STATE_INTERMEDIATE until a new state is given. */

    IMediaControl_Release(control);
    IMemAllocator_Release(allocator);
}

static void test_flushing(IPin *pin, IMemInputPin *input, IFilterGraph2 *graph)
{
    IMediaControl *control;
    OAFilterState state;
    HRESULT hr;

    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);

    hr = IMediaControl_Pause(control);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = send_frame(input);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_BeginFlush(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = send_frame(input);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IPin_EndFlush(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_Run(control);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_BeginFlush(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = send_frame(input);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IPin_EndFlush(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = send_frame(input);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IMediaControl_Release(control);
}

static unsigned int check_ec_complete(IMediaEvent *eventsrc)
{
    unsigned int ret = FALSE;
    LONG_PTR param1, param2;
    HRESULT hr;
    LONG code;

    while ((hr = IMediaEvent_GetEvent(eventsrc, &code, &param1, &param2, 50)) == S_OK)
    {
        if (code == EC_COMPLETE)
        {
            ok(param1 == S_OK, "Got param1 %#lx.\n", param1);
            ok(!param2, "Got param2 %#lx.\n", param2);
            ret++;
        }
        IMediaEvent_FreeEventParams(eventsrc, code, param1, param2);
    }
    ok(hr == E_ABORT, "Got hr %#x.\n", hr);

    return ret;
}

static void test_eos(IPin *pin, IMemInputPin *input, IFilterGraph2 *graph)
{
    IMediaControl *control;
    IMediaEvent *eventsrc;
    OAFilterState state;
    HRESULT hr;
    BOOL ret;

    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    IFilterGraph2_QueryInterface(graph, &IID_IMediaEvent, (void **)&eventsrc);

    hr = IMediaControl_Pause(control);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    ok(!ret, "Got unexpected EC_COMPLETE.\n");

    hr = IPin_EndOfStream(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    todo_wine ok(!ret, "Got unexpected EC_COMPLETE.\n");

    hr = send_frame(input);
    todo_wine ok(hr == VFW_E_SAMPLE_REJECTED_EOS, "Got hr %#x.\n", hr);

    hr = IMediaControl_Run(control);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    todo_wine ok(ret == 1, "Expected EC_COMPLETE.\n");

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    ok(!ret, "Got unexpected EC_COMPLETE.\n");

    /* We do not receive an EC_COMPLETE notification until the last sample is
     * done rendering. */

    hr = IMediaControl_Pause(control);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    hr = send_frame(input);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaControl_Run(control);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    ok(!ret, "Got unexpected EC_COMPLETE.\n");

    hr = IPin_EndOfStream(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    todo_wine ok(!ret, "Got unexpected EC_COMPLETE.\n");
    Sleep(1000);
    ret = check_ec_complete(eventsrc);
    todo_wine ok(ret == 1, "Expected EC_COMPLETE.\n");

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    ok(!ret, "Got unexpected EC_COMPLETE.\n");

    /* Test sending EOS while flushing. */

    hr = IMediaControl_Run(control);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    hr = send_frame(input);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_BeginFlush(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IPin_EndOfStream(pin);
    todo_wine ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    hr = IPin_EndFlush(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    todo_wine ok(!ret, "Got unexpected EC_COMPLETE.\n");

    /* Test sending EOS and then flushing or stopping. */

    hr = IMediaControl_Pause(control);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    hr = send_frame(input);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaControl_Run(control);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    ok(!ret, "Got unexpected EC_COMPLETE.\n");

    hr = IPin_EndOfStream(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    todo_wine ok(!ret, "Got unexpected EC_COMPLETE.\n");

    hr = IPin_BeginFlush(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IPin_EndFlush(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = send_frame(input);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IPin_EndOfStream(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    ok(!ret, "Got unexpected EC_COMPLETE.\n");

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ret = check_ec_complete(eventsrc);
    ok(!ret, "Got unexpected EC_COMPLETE.\n");

    IMediaEvent_Release(eventsrc);
    IMediaControl_Release(control);
}

static DWORD WINAPI song_thread(void *arg)
{
    REFERENCE_TIME start_time = GetTickCount();
    IMemInputPin *input;
    IPin *pin = arg;

    IPin_QueryInterface(pin, &IID_IMemInputPin, (void **)&input);

    /* Samples that are not marked as discontinuous are rendered one after the
     * other, without regard to presentation time. However, samples stamped
     * earlier than the start of the stream are silently dropped. Samples that
     * straddle the start of the stream show inconsistent behaviour—sometimes
     * they are played continuously, sometimes only the part between 0 and the
     * sample end is played, without any clear pattern. */

    trace("Starting song 1, time = %u ms.\n", GetTickCount());
    send_frame_params(input, 0, 4000000, FALSE, 246.94);
    send_frame_params(input, 10 * 10000000, 2000000, FALSE, 277.18);
    send_frame_params(input, 10 * 10000000, 4000000, FALSE, 293.66);
    send_frame_params(input, (LONGLONG)100000 * 10000000, 2000000, FALSE, 329.63);
    send_frame_params(input, 0, 6000000, FALSE, 277.18);
    send_frame_params(input, -1 * 10000000, 4000000, FALSE, 440.0); /* not played */
    send_frame_params(input, 0, 4000000, FALSE, 220.0);
    send_frame_params(input, -1 * 10000000, 4000000, FALSE, 440.0); /* not played */
    send_frame_params(input, LLONG_MAX, 10000000, FALSE, 246.94);
    send_frame_params(input, LLONG_MIN, 10000000, FALSE, 440.0); /* not played */
    trace("Song 1 done, time = %u ms.\n", GetTickCount());

    /* Similarly, if nothing is in the buffer, a continuous sample is played
     * immediately, instead of whenever it's timestamped to. */
    Sleep(1500);
    trace("A begins, time = %u ms.\n", GetTickCount());
    send_frame_params(input, 10 * 10000000, 5000000, FALSE, 440.0);
    Sleep(500);
    trace("A ends, time = %u ms.\n", GetTickCount());

    Sleep(500);
    trace("G# begins, time = %u ms.\n", GetTickCount());
    send_frame_params(input, 0, 5000000, FALSE, 415.30);
    Sleep(500);
    trace("G# ends, time = %u ms.\n", GetTickCount());

    Sleep(500);
    send_frame_params(input, -1 * 10000000, 5000000, FALSE, 392.0); /* not played */

    /* Samples that are marked as discontinuous are rendered at their
     * presentation time. If that presentation time is in the past, it is
     * played immediately. If it overlaps the presentation time of the last
     * sample, it is played continuously following that sample.
     *
     * However, the DirectSound renderer tries to preserve relative timing
     * between samples, if not per se absolute timing. So if a sample is played
     * too late for one of these reasons, the next sample will be delayed by
     * that much. In other words, the time a discontinuous sample is rendered
     * is the time the last (discontinuous?) sample was rendered, plus the
     * difference in presentation time between those two samples.
     *
     * At no point does the DirectSound renderer decide that we have underrun
     * and skip ahead. Some filters rely on this and use gaps between samples
     * to effect silence, e.g. native Windows Media Player 9. There is one
     * exception to this: the first discontinuous sample encountered in a
     * stream is always played immediately. */

    trace("Starting song 2, time = %u ms.\n", GetTickCount());
    send_frame_params(input, ((GetTickCount() - start_time) * 10000) + 10000000, 5000000, TRUE, 233.08);
    send_frame_params(input, 0, 5000000, TRUE, 196.0);
    send_frame_params(input, 0, 10000000, TRUE, 261.63);
    send_frame_params(input, -15000000, 10000000, TRUE, 116.54); /* not played */
    send_frame_params(input, 5000000, 10000000, TRUE, 220.0);

    send_frame_params(input, 25000000, 10000000, TRUE, 293.66);
    send_frame_params(input, 0, 10000000, FALSE, 233.08);
    send_frame_params(input, 0, 5000000, FALSE, 220.0);
    send_frame_params(input, 15000000, 5000000, TRUE, 261.63);

    send_frame_params(input, 30000000, 5000000, TRUE, 233.08);
    send_frame_params(input, 35000000, 5000000, TRUE, 293.66);
    send_frame_params(input, 0, 10000000, FALSE, 392.0);
    send_frame_params(input, 0, 10000000, FALSE, 220.0);

    send_frame_params(input, 70000000, 10000000, TRUE, 349.23);
    send_frame_params(input, 85000000, 10000000, FALSE, 196.0);
    send_frame_params(input, 0, 5000000, TRUE, 220.0);
    send_frame_params(input, 0, 5000000, TRUE, 233.08);
    trace("Song 2 done, time = %u ms.\n", GetTickCount());

    Sleep(3000);
    trace("A begins, time = %u ms.\n", GetTickCount());
    send_frame_params(input, 0, 5000000, TRUE, 440.0);
    Sleep(500);
    trace("A ends, time = %u ms.\n", GetTickCount());

    Sleep(500);
    send_frame_params(input, 15000000, 5000000, TRUE, 415.30);
    Sleep(500);
    trace("G# begins, time = %u ms.\n", GetTickCount());
    Sleep(500);
    trace("G# ends, time = %u ms.\n", GetTickCount());

    IPin_EndOfStream(pin);
    IMemInputPin_Release(input);
    return 0;
}

static void test_sample_time(IPin *pin, IFilterGraph2 *graph)
{
    IMediaControl *control;
    IMediaEvent *eventsrc;
    OAFilterState state;
    HANDLE thread;
    HRESULT hr;
    LONG code;

    /* These tests don't require user input, but they do take some time to
     * execute, and need manual validation. */
    if (!winetest_interactive)
    {
        skip("Interactive tests for sample presentation time.\n");
        return;
    }

    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    IFilterGraph2_QueryInterface(graph, &IID_IMediaEvent, (void **)&eventsrc);

    thread = CreateThread(NULL, 0, song_thread, pin, 0, NULL);

    hr = IMediaControl_Pause(control);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IMediaControl_GetState(control, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_Run(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaEvent_WaitForCompletion(eventsrc, INFINITE, &code);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    ok(!WaitForSingleObject(thread, 1000), "Wait failed.\n");
    CloseHandle(thread);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IMediaEvent_Release(eventsrc);
    IMediaControl_Release(control);
}

static void test_connect_pin(void)
{
    ALLOCATOR_PROPERTIES req_props = {1, 4 * 44100, 1, 0}, ret_props;
    WAVEFORMATEX wfx =
    {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = 2,
        .nSamplesPerSec = 44100,
        .nAvgBytesPerSec = 44100 * 4,
        .nBlockAlign = 4,
        .wBitsPerSample = 16,
    };
    AM_MEDIA_TYPE req_mt =
    {
        .majortype = MEDIATYPE_Audio,
        .subtype = MEDIASUBTYPE_PCM,
        .formattype = FORMAT_WaveFormatEx,
        .cbFormat = sizeof(wfx),
        .pbFormat = (BYTE *)&wfx,
    };
    IBaseFilter *filter = create_dsound_render();
    IFilterGraph2 *graph = create_graph();
    struct testfilter source;
    IMemAllocator *allocator;
    IMemInputPin *input;
    AM_MEDIA_TYPE mt;
    IPin *pin, *peer;
    HRESULT hr;
    ULONG ref;

    testfilter_init(&source);

    IFilterGraph2_AddFilter(graph, &source.filter.IBaseFilter_iface, NULL);
    IFilterGraph2_AddFilter(graph, filter, NULL);

    IBaseFilter_FindPin(filter, sink_id, &pin);

    peer = (IPin *)0xdeadbeef;
    hr = IPin_ConnectedTo(pin, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    hr = IFilterGraph2_ConnectDirect(graph, &source.source.pin.IPin_iface, pin, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_ConnectedTo(pin, &peer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(peer == &source.source.pin.IPin_iface, "Got peer %p.\n", peer);
    IPin_Release(peer);

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(compare_media_types(&mt, &req_mt), "Media types didn't match.\n");

    IPin_QueryInterface(pin, &IID_IMemInputPin, (void **)&input);

    test_allocator(input);

    hr = CoCreateInstance(&CLSID_MemoryAllocator, NULL, CLSCTX_INPROC_SERVER,
            &IID_IMemAllocator, (void **)&allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMemAllocator_SetProperties(allocator, &req_props, &ret_props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(&ret_props, &req_props, sizeof(req_props)), "Properties did not match.\n");
    hr = IMemInputPin_NotifyAllocator(input, allocator, TRUE);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMemAllocator_Commit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemInputPin_ReceiveCanBlock(input);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    test_filter_state(input, graph);
    test_flushing(pin, input, graph);
    test_eos(pin, input, graph);
    test_sample_time(pin, graph);

    hr = IFilterGraph2_Disconnect(graph, pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IFilterGraph2_Disconnect(graph, pin);
    todo_wine ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(source.source.pin.peer == pin, "Got peer %p.\n", source.source.pin.peer);
    IFilterGraph2_Disconnect(graph, &source.source.pin.IPin_iface);

    peer = (IPin *)0xdeadbeef;
    hr = IPin_ConnectedTo(pin, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    ref = IMemAllocator_Release(allocator);
    todo_wine ok(!ref, "Got outstanding refcount %d.\n", ref);
    IMemInputPin_Release(input);
    IPin_Release(pin);
    ref = IFilterGraph2_Release(graph);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ref = IBaseFilter_Release(&source.filter.IBaseFilter_iface);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

static void test_unconnected_filter_state(void)
{
    IBaseFilter *filter = create_dsound_render();
    FILTER_STATE state;
    HRESULT hr;
    ULONG ref;

    hr = IBaseFilter_GetState(filter, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Stopped, "Got state %u.\n", state);

    hr = IBaseFilter_Pause(filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_GetState(filter, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Paused, "Got state %u.\n", state);

    hr = IBaseFilter_Run(filter, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_GetState(filter, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Running, "Got state %u.\n", state);

    hr = IBaseFilter_Pause(filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_GetState(filter, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Paused, "Got state %u.\n", state);

    hr = IBaseFilter_Stop(filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_GetState(filter, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Stopped, "Got state %u.\n", state);

    hr = IBaseFilter_Run(filter, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_GetState(filter, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Running, "Got state %u.\n", state);

    hr = IBaseFilter_Stop(filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_GetState(filter, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Stopped, "Got state %u.\n", state);

    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
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
    test_aggregation();
    test_enum_pins();
    test_find_pin();
    test_pin_info();
    test_basic_audio();
    test_enum_media_types();
    test_unconnected_filter_state();
    test_connect_pin();

    CoUninitialize();
}
