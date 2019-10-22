/*
 * Sample grabber filter unit tests
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

#define COBJMACROS
#include "dshow.h"
#include "qedit.h"
#include "wine/strmbase.h"
#include "wine/test.h"

static const WCHAR sink_id[] = {'I','n',0};
static const WCHAR source_id[] = {'O','u','t',0};
static const WCHAR sink_name[] = {'I','n','p','u','t',0};
static const WCHAR source_name[] = {'O','u','t','p','u','t',0};

static IBaseFilter *create_sample_grabber(void)
{
    IBaseFilter *filter = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
            &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    return filter;
}

static ULONG get_refcount(void *iface)
{
    IUnknown *unknown = iface;
    IUnknown_AddRef(unknown);
    return IUnknown_Release(unknown);
}

static IFilterGraph2 *create_graph(void)
{
    IFilterGraph2 *ret;
    HRESULT hr;
    hr = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IFilterGraph2, (void **)&ret);
    ok(hr == S_OK, "Failed to create FilterGraph: %#x\n", hr);
    return ret;
}

#define check_interface(a, b, c) check_interface_(__LINE__, a, b, c)
static void check_interface_(unsigned int line, void *iface_ptr, REFIID iid, BOOL supported)
{
    IUnknown *iface = iface_ptr;
    HRESULT hr, expected_hr;
    ULONG ref, expect_ref;
    IUnknown *unk;

    expected_hr = supported ? S_OK : E_NOINTERFACE;

    expect_ref = get_refcount(iface);

    hr = IUnknown_QueryInterface(iface, iid, (void **)&unk);
    ok_(__FILE__, line)(hr == expected_hr, "Got hr %#x, expected %#x.\n", hr, expected_hr);
    if (SUCCEEDED(hr))
    {
        ref = get_refcount(iface);
        ok_(__FILE__, line)(ref == expect_ref + 1, "Expected %u references, got %u.\n", expect_ref + 1, ref);
        ref = get_refcount(unk);
        ok_(__FILE__, line)(ref == expect_ref + 1, "Expected %u references, got %u.\n", expect_ref + 1, ref);
        IUnknown_Release(unk);
    }
}

static void test_interfaces(void)
{
    IBaseFilter *filter = create_sample_grabber();
    IUnknown *unk;
    HRESULT hr;
    ULONG ref;
    IPin *pin;

    check_interface(filter, &IID_IBaseFilter, TRUE);
    check_interface(filter, &IID_IMediaFilter, TRUE);
    check_interface(filter, &IID_IPersist, TRUE);
    check_interface(filter, &IID_ISampleGrabber, TRUE);
    check_interface(filter, &IID_IUnknown, TRUE);

    check_interface(filter, &IID_IAMFilterMiscFlags, FALSE);
    check_interface(filter, &IID_IBasicAudio, FALSE);
    check_interface(filter, &IID_IBasicVideo, FALSE);
    check_interface(filter, &IID_IKsPropertySet, FALSE);
    check_interface(filter, &IID_IMediaPosition, FALSE);
    check_interface(filter, &IID_IMediaSeeking, FALSE);
    check_interface(filter, &IID_IMemInputPin, FALSE);
    check_interface(filter, &IID_IPersistPropertyBag, FALSE);
    check_interface(filter, &IID_IPin, FALSE);
    check_interface(filter, &IID_IQualityControl, FALSE);
    check_interface(filter, &IID_IQualProp, FALSE);
    check_interface(filter, &IID_IReferenceClock, FALSE);
    check_interface(filter, &IID_ISeekingPassThru, FALSE);
    check_interface(filter, &IID_IVideoWindow, FALSE);

    IBaseFilter_FindPin(filter, sink_id, &pin);

    check_interface(pin, &IID_IMemInputPin, TRUE);
    check_interface(pin, &IID_IPin, TRUE);
    todo_wine check_interface(pin, &IID_IQualityControl, TRUE);
    check_interface(pin, &IID_IUnknown, TRUE);

    check_interface(pin, &IID_IKsPropertySet, FALSE);
    check_interface(pin, &IID_IMediaPosition, FALSE);
    check_interface(pin, &IID_IMediaSeeking, FALSE);

    IPin_Release(pin);

    IBaseFilter_FindPin(filter, source_id, &pin);

    /* Queries for IMediaPosition or IMediaSeeking do not seem to increase the
     * reference count. */
    hr = IPin_QueryInterface(pin, &IID_IMediaPosition, (void **)&unk);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IUnknown_Release(unk);
    hr = IPin_QueryInterface(pin, &IID_IMediaSeeking, (void **)&unk);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IUnknown_Release(unk);
    check_interface(pin, &IID_IPin, TRUE);
    todo_wine check_interface(pin, &IID_IQualityControl, TRUE);
    check_interface(pin, &IID_IUnknown, TRUE);

    check_interface(pin, &IID_IAsyncReader, FALSE);
    check_interface(pin, &IID_IKsPropertySet, FALSE);
    check_interface(pin, &IID_IMemInputPin, FALSE);

    IPin_Release(pin);

    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got unexpected refcount %d.\n", ref);
}

static void test_enum_pins(void)
{
    IBaseFilter *filter = create_sample_grabber();
    IEnumPins *enum1, *enum2;
    ULONG count, ref;
    IPin *pins[3];
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
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 2, "Got count %u.\n", count);
    IPin_Release(pins[0]);
    IPin_Release(pins[1]);

    hr = IEnumPins_Next(enum1, 2, pins, &count);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(!count, "Got count %u.\n", count);

    hr = IEnumPins_Reset(enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum1, 3, pins, &count);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(count == 2, "Got count %u.\n", count);
    IPin_Release(pins[0]);
    IPin_Release(pins[1]);

    hr = IEnumPins_Reset(enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Clone(enum1, &enum2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Skip(enum1, 3);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumPins_Skip(enum1, 2);
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
    IBaseFilter *filter = create_sample_grabber();
    IEnumPins *enum_pins;
    IPin *pin, *pin2;
    HRESULT hr;
    ULONG ref;

    hr = IBaseFilter_FindPin(filter, sink_name, &pin);
    ok(hr == VFW_E_NOT_FOUND, "Got hr %#x.\n", hr);
    hr = IBaseFilter_FindPin(filter, source_name, &pin);
    ok(hr == VFW_E_NOT_FOUND, "Got hr %#x.\n", hr);

    hr = IBaseFilter_EnumPins(filter, &enum_pins);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_FindPin(filter, sink_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IEnumPins_Next(enum_pins, 1, &pin2, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(pin2 == pin, "Expected pin %p, got %p.\n", pin, pin2);
    IPin_Release(pin2);
    IPin_Release(pin);

    hr = IBaseFilter_FindPin(filter, source_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IEnumPins_Next(enum_pins, 1, &pin2, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(pin2 == pin, "Expected pin %p, got %p.\n", pin, pin2);
    IPin_Release(pin2);
    IPin_Release(pin);

    IEnumPins_Release(enum_pins);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

static void test_pin_info(void)
{
    IBaseFilter *filter = create_sample_grabber();
    PIN_DIRECTION dir;
    PIN_INFO info;
    ULONG count;
    HRESULT hr;
    WCHAR *id;
    ULONG ref;
    IPin *pin;

    hr = IBaseFilter_FindPin(filter, sink_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_QueryPinInfo(pin, &info);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(info.pFilter == filter, "Expected filter %p, got %p.\n", filter, info.pFilter);
    ok(info.dir == PINDIR_INPUT, "Got direction %d.\n", info.dir);
    todo_wine ok(!lstrcmpW(info.achName, sink_name), "Got name %s.\n", wine_dbgstr_w(info.achName));
    IBaseFilter_Release(info.pFilter);

    hr = IPin_QueryDirection(pin, &dir);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(dir == PINDIR_INPUT, "Got direction %d.\n", dir);

    hr = IPin_QueryId(pin, &id);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!lstrcmpW(id, sink_id), "Got id %s.\n", wine_dbgstr_w(id));
    CoTaskMemFree(id);

    hr = IPin_QueryInternalConnections(pin, NULL, &count);
    todo_wine ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);

    IPin_Release(pin);

    hr = IBaseFilter_FindPin(filter, source_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_QueryPinInfo(pin, &info);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(info.pFilter == filter, "Expected filter %p, got %p.\n", filter, info.pFilter);
    ok(info.dir == PINDIR_OUTPUT, "Got direction %d.\n", info.dir);
    todo_wine ok(!lstrcmpW(info.achName, source_name), "Got name %s.\n", wine_dbgstr_w(info.achName));
    IBaseFilter_Release(info.pFilter);

    hr = IPin_QueryDirection(pin, &dir);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(dir == PINDIR_OUTPUT, "Got direction %d.\n", dir);

    hr = IPin_QueryId(pin, &id);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!lstrcmpW(id, source_id), "Got id %s.\n", wine_dbgstr_w(id));
    CoTaskMemFree(id);

    hr = IPin_QueryInternalConnections(pin, NULL, &count);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);

    IPin_Release(pin);

    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
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
    hr = CoCreateInstance(&CLSID_SampleGrabber, &test_outer, CLSCTX_INPROC_SERVER,
            &IID_IBaseFilter, (void **)&filter);
    ok(hr == E_NOINTERFACE, "Got hr %#x.\n", hr);
    ok(!filter, "Got interface %p.\n", filter);

    hr = CoCreateInstance(&CLSID_SampleGrabber, &test_outer, CLSCTX_INPROC_SERVER,
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

static void test_media_types(void)
{
    IBaseFilter *filter = create_sample_grabber();
    IEnumMediaTypes *enummt;
    AM_MEDIA_TYPE mt = {};
    HRESULT hr;
    ULONG ref;
    IPin *pin;

    IBaseFilter_FindPin(filter, sink_id, &pin);

    hr = IPin_EnumMediaTypes(pin, &enummt);
    todo_wine ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IPin_Release(pin);

    IBaseFilter_FindPin(filter, source_id, &pin);

    hr = IPin_EnumMediaTypes(pin, &enummt);
    todo_wine ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IPin_Release(pin);

    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

struct testsource
{
    struct strmbase_filter filter;
    struct strmbase_source pin;
};

static inline struct testsource *impl_source_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct testsource, filter);
}

static IPin *testsource_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct testsource *filter = impl_source_from_strmbase_filter(iface);
    if (!index)
        return &filter->pin.pin.IPin_iface;
    return NULL;
}

static void testsource_destroy(struct strmbase_filter *iface)
{
    struct testsource *filter = impl_source_from_strmbase_filter(iface);
    strmbase_source_cleanup(&filter->pin);
    strmbase_filter_cleanup(&filter->filter);
}

static const struct strmbase_filter_ops testsource_ops =
{
    .filter_get_pin = testsource_get_pin,
    .filter_destroy = testsource_destroy,
};

static HRESULT testpin_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    return S_OK;
}

static HRESULT WINAPI testsource_pin_AttemptConnection(struct strmbase_source *iface,
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

static const struct strmbase_source_ops testsource_pin_ops =
{
    .base.pin_query_accept = testpin_query_accept,
    .base.pin_get_media_type = strmbase_pin_get_media_type,
    .pfnAttemptConnection = testsource_pin_AttemptConnection,
};

static void testsource_init(struct testsource *filter)
{
    static const GUID clsid = {0xabacab};
    strmbase_filter_init(&filter->filter, NULL, &clsid, &testsource_ops);
    strmbase_source_init(&filter->pin, &filter->filter, L"", &testsource_pin_ops);
}

struct testsink
{
    struct strmbase_filter filter;
    struct strmbase_sink pin;
    BOOL expose_mt;
};

static inline struct testsink *impl_sink_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct testsink, filter);
}

static IPin *testsink_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct testsink *filter = impl_sink_from_strmbase_filter(iface);
    if (!index)
        return &filter->pin.pin.IPin_iface;
    return NULL;
}

static void testsink_destroy(struct strmbase_filter *iface)
{
    struct testsink *filter = impl_sink_from_strmbase_filter(iface);
    strmbase_sink_cleanup(&filter->pin);
    strmbase_filter_cleanup(&filter->filter);
}

static const struct strmbase_filter_ops testsink_ops =
{
    .filter_get_pin = testsink_get_pin,
    .filter_destroy = testsink_destroy,
};

static const IPinVtbl testsink_pin_vtbl =
{
    BasePinImpl_QueryInterface,
    BasePinImpl_AddRef,
    BasePinImpl_Release,
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
    BasePinImpl_NewSegment,
};

static HRESULT testsink_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct testsink *filter = impl_sink_from_strmbase_filter(iface->filter);

    if (IsEqualGUID(iid, &IID_IMemInputPin))
        *out = &filter->pin.IMemInputPin_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT testsink_get_media_type(struct strmbase_pin *iface, unsigned int index, AM_MEDIA_TYPE *mt)
{
    struct testsink *filter = impl_sink_from_strmbase_filter(iface->filter);
    if (!index && filter->expose_mt)
    {
        memset(mt, 0, sizeof(AM_MEDIA_TYPE));
        mt->majortype = MEDIATYPE_Stream;
        mt->subtype = MEDIASUBTYPE_Avi;
        return S_OK;
    }
    return VFW_S_NO_MORE_ITEMS;
}

static HRESULT WINAPI testsink_Receive(struct strmbase_sink *iface, IMediaSample *sample)
{
    return S_OK;
}

static const struct strmbase_sink_ops testsink_pin_ops =
{
    .base.pin_query_interface = testsink_query_interface,
    .base.pin_query_accept = testpin_query_accept,
    .base.pin_get_media_type = testsink_get_media_type,
    .pfnReceive = testsink_Receive,
};

static void testsink_init(struct testsink *filter)
{
IMemAllocator *allocator;
    static const GUID clsid = {0xabacab};
    strmbase_filter_init(&filter->filter, NULL, &clsid, &testsink_ops);
CoCreateInstance(&CLSID_MemoryAllocator, NULL, CLSCTX_INPROC_SERVER, &IID_IMemAllocator, (void **)&allocator);
    strmbase_sink_init(&filter->pin, &testsink_pin_vtbl, &filter->filter, L"", &testsink_pin_ops, NULL);
    filter->expose_mt = FALSE;
}

static void test_allocator(IMemInputPin *input)
{
    ALLOCATOR_PROPERTIES req_props = {1, 5000, 1, 0}, ret_props;
    IMemAllocator *req_allocator, *ret_allocator;
    HRESULT hr;

    hr = IMemInputPin_GetAllocatorRequirements(input, &ret_props);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);

    hr = IMemInputPin_GetAllocator(input, &ret_allocator);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    if (hr == S_OK)
    {
        hr = IMemAllocator_GetProperties(ret_allocator, &ret_props);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(!ret_props.cBuffers, "Got %d buffers.\n", ret_props.cBuffers);
        ok(!ret_props.cbBuffer, "Got size %d.\n", ret_props.cbBuffer);
        ok(!ret_props.cbAlign, "Got alignment %d.\n", ret_props.cbAlign);
        ok(!ret_props.cbPrefix, "Got prefix %d.\n", ret_props.cbPrefix);

        hr = IMemInputPin_NotifyAllocator(input, ret_allocator, TRUE);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        IMemAllocator_Release(ret_allocator);
    }

    CoCreateInstance(&CLSID_MemoryAllocator, NULL, CLSCTX_INPROC_SERVER,
            &IID_IMemAllocator, (void **)&req_allocator);

    hr = IMemInputPin_NotifyAllocator(input, req_allocator, TRUE);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemInputPin_GetAllocator(input, &ret_allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(ret_allocator == req_allocator, "Allocators didn't match.\n");
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemAllocator_SetProperties(req_allocator, &req_props, &ret_props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemAllocator_Commit(req_allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IMemAllocator_Release(req_allocator);
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
    free(params);
    return hr;
}

static HRESULT send_frame(IMemInputPin *sink, const char *str)
{
    struct frame_thread_params *params = malloc(sizeof(params));
    IMemAllocator *allocator;
    IMediaSample *sample;
    HANDLE thread;
    HRESULT hr;
    BYTE *data;
    DWORD ret;

    hr = IMemInputPin_GetAllocator(sink, &allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemAllocator_Commit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetPointer(sample, &data);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    strcpy((char *)data, str);

    hr = IMediaSample_SetActualDataLength(sample, strlen(str) + 1);
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

static void test_connect_pin(void)
{
    AM_MEDIA_TYPE req_mt =
    {
        .majortype = MEDIATYPE_Stream,
        .subtype = MEDIASUBTYPE_Avi,
    };
    IBaseFilter *filter = create_sample_grabber();
    IFilterGraph2 *graph = create_graph();
    struct testsource testsource;
    IPin *sink, *source, *peer;
    struct testsink testsink;
    IEnumMediaTypes *enummt;
    AM_MEDIA_TYPE mt, *pmt;
    IMemInputPin *meminput;
    HRESULT hr;
    ULONG ref;

    testsource_init(&testsource);
    testsink_init(&testsink);
    IFilterGraph2_AddFilter(graph, &testsource.filter.IBaseFilter_iface, NULL);
    IFilterGraph2_AddFilter(graph, &testsink.filter.IBaseFilter_iface, NULL);
    IFilterGraph2_AddFilter(graph, filter, NULL);
    IBaseFilter_FindPin(filter, L"In", &sink);
    IBaseFilter_FindPin(filter, L"Out", &source);
    IPin_QueryInterface(sink, &IID_IMemInputPin, (void **)&meminput);

    /* Test sink connection. */
    peer = (IPin *)0xdeadbeef;
    hr = IPin_ConnectedTo(sink, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(sink, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    hr = IFilterGraph2_ConnectDirect(graph, &testsource.pin.pin.IPin_iface, sink, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_ConnectedTo(sink, &peer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(peer == &testsource.pin.pin.IPin_iface, "Got peer %p.\n", peer);
    IPin_Release(peer);

    hr = IPin_ConnectionMediaType(sink, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(&mt, &req_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");

    test_allocator(meminput);

    hr = IPin_EnumMediaTypes(sink, &enummt);
    todo_wine ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    hr = IPin_EnumMediaTypes(source, &enummt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IEnumMediaTypes_Reset(enummt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IEnumMediaTypes_Next(enummt, 1, &pmt, NULL);
    todo_wine ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(!memcmp(&mt, &req_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");
    IEnumMediaTypes_Release(enummt);

    mt.majortype = MEDIASUBTYPE_RGB8;
    hr = IPin_QueryAccept(sink, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IPin_QueryAccept(source, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    /* Test source connection. */
    peer = (IPin *)0xdeadbeef;
    hr = IPin_ConnectedTo(source, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(source, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    hr = IFilterGraph2_ConnectDirect(graph, source, &testsink.pin.pin.IPin_iface, &req_mt);
    todo_wine ok(hr == VFW_E_NO_ACCEPTABLE_TYPES, "Got hr %#x.\n", hr);
    testsink.expose_mt = TRUE;
    hr = IFilterGraph2_ConnectDirect(graph, source, &testsink.pin.pin.IPin_iface, &req_mt);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_ConnectedTo(source, &peer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(peer == &testsink.pin.pin.IPin_iface, "Got peer %p.\n", peer);
    IPin_Release(peer);

    hr = IPin_ConnectionMediaType(source, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(&mt, &req_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");

    hr = IPin_EnumMediaTypes(sink, &enummt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IEnumMediaTypes_Reset(enummt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IEnumMediaTypes_Next(enummt, 1, &pmt, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(pmt, &req_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");
    hr = IEnumMediaTypes_Next(enummt, 1, &pmt, NULL);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    IEnumMediaTypes_Release(enummt);

    hr = IPin_EnumMediaTypes(source, &enummt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IEnumMediaTypes_Reset(enummt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IEnumMediaTypes_Next(enummt, 1, &pmt, NULL);
    todo_wine ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(!memcmp(&mt, &req_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");
    IEnumMediaTypes_Release(enummt);

    mt.majortype = MEDIASUBTYPE_RGB8;
    hr = IPin_QueryAccept(sink, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IPin_QueryAccept(source, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

{
    ISampleGrabber *grabber;
    IMediaControl *control;

    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    IBaseFilter_QueryInterface(filter, &IID_ISampleGrabber, (void **)&grabber);

    trace("...\n");

    hr = IMediaControl_Run(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = send_frame(meminput, "one");
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = send_frame(meminput, "two");
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = ISampleGrabber_SetOneShot(grabber, TRUE);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = send_frame(meminput, "one");
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = send_frame(meminput, "two");
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IFilterGraph2_RemoveFilter(graph, filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IFilterGraph2_AddFilter(graph, filter, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IFilterGraph2_ConnectDirect(graph, &testsource.pin.pin.IPin_iface, sink, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    test_allocator(meminput);
    hr = IFilterGraph2_ConnectDirect(graph, source, &testsink.pin.pin.IPin_iface, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaControl_Run(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = send_frame(meminput, "one");
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    trace("... <--\n");

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    ISampleGrabber_Release(grabber);
    IMediaControl_Release(control);
}

    hr = IFilterGraph2_Disconnect(graph, source);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IFilterGraph2_Disconnect(graph, source);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(testsink.pin.pin.peer == source, "Got peer %p.\n", testsink.pin.pin.peer);
    IFilterGraph2_Disconnect(graph, &testsink.pin.pin.IPin_iface);

    peer = (IPin *)0xdeadbeef;
    hr = IPin_ConnectedTo(source, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(source, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    hr = IFilterGraph2_Disconnect(graph, sink);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IFilterGraph2_Disconnect(graph, sink);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(testsource.pin.pin.peer == sink, "Got peer %p.\n", testsource.pin.pin.peer);
    IFilterGraph2_Disconnect(graph, &testsource.pin.pin.IPin_iface);

    peer = (IPin *)0xdeadbeef;
    hr = IPin_ConnectedTo(sink, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(sink, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    IPin_Release(sink);
    IPin_Release(source);
    IMemInputPin_Release(meminput);
    ref = IFilterGraph2_Release(graph);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ref = IBaseFilter_Release(&testsource.filter.IBaseFilter_iface);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ref = IBaseFilter_Release(&testsink.filter.IBaseFilter_iface);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

START_TEST(samplegrabber)
{
    IBaseFilter *filter;
    HRESULT hr;

    CoInitialize(NULL);

    if (FAILED(hr = CoCreateInstance(&CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
            &IID_IBaseFilter, (void **)&filter)))
    {
        /* qedit.dll does not exist on 2003. */
        win_skip("Failed to create sample grabber filter, hr %#x.\n", hr);
        return;
    }
    IBaseFilter_Release(filter);

    test_interfaces();
    test_enum_pins();
    test_find_pin();
    test_pin_info();
    test_aggregation();
    test_media_types();
    test_connect_pin();

    CoUninitialize();
}
