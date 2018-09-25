/*
 * ACM wrapper filter unit tests
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
#include "mmreg.h"
#include "wine/heap.h"
#include "wine/test.h"

#include "util.h"

static const WCHAR sink_id[] = {'I','n',0};
static const WCHAR source_id[] = {'O','u','t',0};

static IBaseFilter *create_acm_wrapper(void)
{
    IBaseFilter *filter = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_ACMWrapper, NULL, CLSCTX_INPROC_SERVER,
        &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    return filter;
}

static const GUID MEDIASUBTYPE_IMAADPCM = {WAVE_FORMAT_IMA_ADPCM,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};

static void init_test_mt(AM_MEDIA_TYPE *mt, IMAADPCMWAVEFORMAT *wfx)
{
    memset(mt, 0, sizeof(*mt));
    memset(wfx, 0, sizeof(*wfx));
    mt->majortype = MEDIATYPE_Audio;
    mt->subtype = MEDIASUBTYPE_IMAADPCM;
    mt->formattype = FORMAT_WaveFormatEx;
    mt->cbFormat = sizeof(*wfx);
    mt->pbFormat = (BYTE *)wfx;
    wfx->wfx.wFormatTag = WAVE_FORMAT_IMA_ADPCM;
    wfx->wfx.nChannels = 1;
    wfx->wfx.nSamplesPerSec = 8000;
    wfx->wfx.nBlockAlign = 256;
    wfx->wfx.wBitsPerSample = 4;
    wfx->wfx.cbSize = 2;
    wfx->wSamplesPerBlock = (256 - (4 * 1)) * (2 / 1) + 1;
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
    IBaseFilter *filter = create_acm_wrapper();
    IPin *pin;

    check_interface(filter, &IID_IBaseFilter, TRUE);

    check_interface(filter, &IID_IAMFilterMiscFlags, FALSE);
    check_interface(filter, &IID_IBasicAudio, FALSE);
    check_interface(filter, &IID_IBasicVideo, FALSE);
    check_interface(filter, &IID_IKsPropertySet, FALSE);
todo_wine
    check_interface(filter, &IID_IMediaPosition, FALSE);
todo_wine
    check_interface(filter, &IID_IMediaSeeking, FALSE);
    check_interface(filter, &IID_IPin, FALSE);
todo_wine
    check_interface(filter, &IID_IQualityControl, FALSE);
    check_interface(filter, &IID_IQualProp, FALSE);
    check_interface(filter, &IID_IReferenceClock, FALSE);
    check_interface(filter, &IID_IVideoWindow, FALSE);

    IBaseFilter_FindPin(filter, sink_id, &pin);

    check_interface(pin, &IID_IMemInputPin, TRUE);
    check_interface(pin, &IID_IPin, TRUE);

    check_interface(pin, &IID_IMediaPosition, FALSE);
todo_wine
    check_interface(pin, &IID_IMediaSeeking, FALSE);

    IPin_Release(pin);

    IBaseFilter_FindPin(filter, source_id, &pin);

    check_interface(pin, &IID_IPin, TRUE);
    check_interface(pin, &IID_IMediaSeeking, TRUE);

    check_interface(pin, &IID_IAsyncReader, FALSE);

    IPin_Release(pin);

    IBaseFilter_Release(filter);
}

static void test_enum_pins(void)
{
    IBaseFilter *filter = create_acm_wrapper();
    IEnumPins *enum1, *enum2;
    ULONG count, ref;
    IPin *pins[3];
    HRESULT hr;

    hr = IBaseFilter_EnumPins(filter, &enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IPin_Release(pins[0]);

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
    IBaseFilter *filter = create_acm_wrapper();
    IEnumPins *enum_pins;
    IPin *pin, *pin2;
    HRESULT hr;
    ULONG ref;

    hr = IBaseFilter_FindPin(filter, sink_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IPin_Release(pin);

    hr = IBaseFilter_FindPin(filter, source_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IPin_Release(pin);

    hr = IBaseFilter_EnumPins(filter, &enum_pins);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum_pins, 1, &pin2, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_FindPin(filter, sink_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(pin == pin2, "Expected pin %p, got %p.\n", pin2, pin);
    IPin_Release(pin);
    IPin_Release(pin2);

    hr = IEnumPins_Next(enum_pins, 1, &pin2, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_FindPin(filter, source_id, &pin);
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
    static const WCHAR sink_name[] = {'I','n','p','u','t',0};
    static const WCHAR source_name[] = {'O','u','t','p','u','t',0};
    IBaseFilter *filter = create_acm_wrapper();
    PIN_DIRECTION dir;
    PIN_INFO info;
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
todo_wine
    ok(!lstrcmpW(info.achName, sink_name), "Got name %s.\n", wine_dbgstr_w(info.achName));
    IBaseFilter_Release(info.pFilter);

    hr = IPin_QueryDirection(pin, &dir);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(dir == PINDIR_INPUT, "Got direction %d.\n", dir);

    hr = IPin_QueryId(pin, &id);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!lstrcmpW(id, sink_id), "Got id %s.\n", wine_dbgstr_w(id));
    CoTaskMemFree(id);

    IPin_Release(pin);

    hr = IBaseFilter_FindPin(filter, source_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    check_interface(pin, &IID_IPin, TRUE);
    check_interface(pin, &IID_IMediaSeeking, TRUE);

    hr = IPin_QueryPinInfo(pin, &info);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(info.pFilter == filter, "Expected filter %p, got %p.\n", filter, info.pFilter);
    ok(info.dir == PINDIR_OUTPUT, "Got direction %d.\n", info.dir);
todo_wine
    ok(!lstrcmpW(info.achName, source_name), "Got name %s.\n", wine_dbgstr_w(info.achName));
    IBaseFilter_Release(info.pFilter);

    hr = IPin_QueryDirection(pin, &dir);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(dir == PINDIR_OUTPUT, "Got direction %d.\n", dir);

    hr = IPin_QueryId(pin, &id);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!lstrcmpW(id, source_id), "Got id %s.\n", wine_dbgstr_w(id));
    CoTaskMemFree(id);

    IPin_Release(pin);

    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

static HRESULT WINAPI testsource_Connect(IPin *iface, IPin *peer, const AM_MEDIA_TYPE *mt)
{
    struct testpin *pin = impl_from_IPin(iface);
    if (winetest_debug > 1) trace("%p->Connect()\n", pin);

    pin->peer = peer;
    IPin_AddRef(peer);
    return IPin_ReceiveConnection(peer, &pin->IPin_iface, mt);
}

static const IPinVtbl testsource_vtbl =
{
    testpin_QueryInterface,
    testpin_AddRef,
    testpin_Release,
    testsource_Connect,
    no_ReceiveConnection,
    testpin_Disconnect,
    testpin_ConnectedTo,
    testpin_ConnectionMediaType,
    testpin_QueryPinInfo,
    testpin_QueryDirection,
    testpin_QueryId,
    testpin_QueryAccept,
    testpin_EnumMediaTypes,
    testpin_QueryInternalConnections,
    testpin_EndOfStream,
    testpin_BeginFlush,
    testpin_EndFlush,
    testpin_NewSegment,
};

static HRESULT WINAPI testsink_ReceiveConnection(IPin *iface, IPin *peer, const AM_MEDIA_TYPE *mt)
{
    struct testpin *pin = impl_from_IPin(iface);
    if (winetest_debug > 1) trace("%p->ReceiveConnection()\n", pin);

    ok(!!mt, "Expected non-NULL media type.\n");

    if (pin->accept_mt && !compare_media_types(pin->accept_mt, mt))
        return VFW_E_TYPE_NOT_ACCEPTED;

    if (pin->mt)
        heap_free(pin->mt->pbFormat);
    heap_free(pin->mt);
    pin->mt = heap_alloc(sizeof(*mt));
    copy_media_type(pin->mt, mt);
    pin->peer = peer;
    IPin_AddRef(peer);
    return S_OK;
}

static const IPinVtbl testsink_vtbl =
{
    testpin_QueryInterface,
    testpin_AddRef,
    testpin_Release,
    no_Connect,
    testsink_ReceiveConnection,
    testpin_Disconnect,
    testpin_ConnectedTo,
    testpin_ConnectionMediaType,
    testpin_QueryPinInfo,
    testpin_QueryDirection,
    testpin_QueryId,
    testpin_QueryAccept,
    testpin_EnumMediaTypes,
    testpin_QueryInternalConnections,
    testpin_EndOfStream,
    testpin_BeginFlush,
    testpin_EndFlush,
    testpin_NewSegment,
};

static void testsink_init(struct testpin *pin, const AM_MEDIA_TYPE *types, ULONG type_count)
{
    testpin_init(pin, &testsink_vtbl, PINDIR_INPUT);
    pin->IMemInputPin_iface.lpVtbl = &testmeminput_vtbl;
    pin->types = types;
    pin->type_count = type_count;
}

static void test_connect_pin(void)
{
    AM_MEDIA_TYPE sink_mt = {0};
    struct testpin source_pin, sink_pin;
    struct testfilter source, sink;

    IPin *parser_source, *parser_sink, *peer;
    IBaseFilter *parser = create_acm_wrapper();
    AM_MEDIA_TYPE *expect_mt, mt, req_mt;
    IEnumMediaTypes *enummt;
    IMAADPCMWAVEFORMAT wfx;
    IFilterGraph2 *graph;
    HRESULT hr;
    ULONG ref;

    testpin_init(&source_pin, &testsource_vtbl, PINDIR_OUTPUT);
    testfilter_init(&source, &source_pin, 1);
    testsink_init(&sink_pin, &sink_mt, 1);
    testfilter_init(&sink, &sink_pin, 1);

    IBaseFilter_FindPin(parser, sink_id, &parser_sink);

    hr = IPin_ConnectedTo(parser_sink, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(parser_sink, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    IBaseFilter_FindPin(parser, source_id, &parser_source);

    hr = IPin_ConnectedTo(parser_source, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(parser_source, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IFilterGraph2, (void **)&graph);
    IFilterGraph2_AddFilter(graph, parser, NULL);
    IFilterGraph2_AddFilter(graph, &source.IBaseFilter_iface, NULL);
    IFilterGraph2_AddFilter(graph, &sink.IBaseFilter_iface, NULL);

    init_test_mt(&req_mt, &wfx);
    hr = IFilterGraph2_ConnectDirect(graph, &source_pin.IPin_iface, parser_sink, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_ConnectedTo(parser_sink, &peer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(peer == &source_pin.IPin_iface, "Got peer %p.\n", peer);
    IPin_Release(peer);

    hr = IPin_ConnectionMediaType(parser_sink, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(compare_media_types(&mt, &req_mt), "Media types didn't match.\n");

    hr = IFilterGraph2_Disconnect(graph, parser_sink);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IFilterGraph2_Disconnect(graph, parser_sink);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    IFilterGraph2_Disconnect(graph, &source_pin.IPin_iface);

    hr = IPin_ConnectedTo(parser_sink, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(parser_sink, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    hr = IFilterGraph2_ConnectDirect(graph, &source_pin.IPin_iface, parser_sink, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IPin_EnumMediaTypes(parser_source, &enummt);
    IEnumMediaTypes_Next(enummt, 1, &expect_mt, NULL);
    IEnumMediaTypes_Release(enummt);

    hr = IFilterGraph2_ConnectDirect(graph, parser_source, &sink_pin.IPin_iface, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == parser_source, "Got peer %p.\n", sink_pin.peer);
    ok(compare_media_types(sink_pin.mt, expect_mt), "Media types didn't match.\n");

    hr = IPin_ConnectedTo(parser_source, &peer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(peer == &sink_pin.IPin_iface, "Got peer %p.\n", peer);
    IPin_Release(peer);

    hr = IPin_ConnectionMediaType(parser_source, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(compare_media_types(&mt, expect_mt), "Media types didn't match.\n");

    hr = IFilterGraph2_Disconnect(graph, parser_source);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == parser_source, "Got peer %p.\n", sink_pin.peer);
    IFilterGraph2_Disconnect(graph, &sink_pin.IPin_iface);

    memset(&req_mt, 0, sizeof(req_mt));
    hr = IFilterGraph2_ConnectDirect(graph, parser_source, &sink_pin.IPin_iface, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == parser_source, "Got peer %p.\n", sink_pin.peer);
    ok(compare_media_types(sink_pin.mt, expect_mt), "Media types didn't match.\n");
    IFilterGraph2_Disconnect(graph, parser_source);
    IFilterGraph2_Disconnect(graph, &sink_pin.IPin_iface);

    sink_pin.accept_mt = &sink_mt;
    hr = IFilterGraph2_ConnectDirect(graph, parser_source, &sink_pin.IPin_iface, NULL);
    ok(hr == VFW_E_NO_ACCEPTABLE_TYPES, "Got hr %#x.\n", hr);
    ok(!sink_pin.peer, "Got peer %p.\n", sink_pin.peer);

    copy_media_type(&sink_mt, expect_mt);
    sink_mt.lSampleSize = 123;
    hr = IFilterGraph2_ConnectDirect(graph, parser_source, &sink_pin.IPin_iface, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == parser_source, "Got peer %p.\n", sink_pin.peer);
    ok(compare_media_types(sink_pin.mt, &sink_mt), "Media types didn't match.\n");
    IFilterGraph2_Disconnect(graph, parser_source);
    IFilterGraph2_Disconnect(graph, &sink_pin.IPin_iface);

    IPin_Release(parser_source);
    IPin_Release(parser_sink);
    IFilterGraph2_Release(graph);
    ref = IBaseFilter_Release(parser);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ok(sink.ref == 1, "Got outstanding refcount %d.\n", sink.ref);
    ok(sink_pin.ref == 1, "Got outstanding refcount %d.\n", sink_pin.ref);
}

static void test_filter_state(void)
{
    struct testpin source_pin, sink_pin;
    struct testfilter source, sink;

    IBaseFilter *filter = create_acm_wrapper();
    IPin *parser_source, *parser_sink;
    IMAADPCMWAVEFORMAT wfx;
    IMediaControl *control;
    IFilterGraph2 *graph;
    AM_MEDIA_TYPE req_mt;
    FILTER_STATE state;
    HRESULT hr;
    ULONG ref;

    testpin_init(&source_pin, &testsource_vtbl, PINDIR_OUTPUT);
    testfilter_init(&source, &source_pin, 1);
    testsink_init(&sink_pin, NULL, 0);
    testfilter_init(&sink, &sink_pin, 1);

    CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IFilterGraph2, (void **)&graph);
    IFilterGraph2_AddFilter(graph, filter, NULL);
    IFilterGraph2_AddFilter(graph, &source.IBaseFilter_iface, NULL);
    IFilterGraph2_AddFilter(graph, &sink.IBaseFilter_iface, NULL);
    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);

    IBaseFilter_FindPin(filter, sink_id, &parser_sink);
    IBaseFilter_FindPin(filter, source_id, &parser_source);
    init_test_mt(&req_mt, &wfx);
    IFilterGraph2_ConnectDirect(graph, &source_pin.IPin_iface, parser_sink, &req_mt);
    IFilterGraph2_ConnectDirect(graph, parser_source, &sink_pin.IPin_iface, NULL);
    IPin_Release(parser_source);
    IPin_Release(parser_sink);

    hr = IBaseFilter_GetState(filter, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Stopped, "Got state %d.\n", state);

    hr = IMediaControl_Run(control);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(filter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Running, "Got state %d.\n", state);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(filter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Stopped, "Got state %d.\n", state);

    hr = IMediaControl_Pause(control);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(filter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Paused, "Got state %d.\n", state);

    hr = IMediaControl_Run(control);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(filter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Running, "Got state %d.\n", state);

    hr = IMediaControl_Pause(control);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(filter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Paused, "Got state %d.\n", state);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(filter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Stopped, "Got state %d.\n", state);

    IMediaControl_Release(control);
    IFilterGraph2_Release(graph);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ok(source.ref == 1, "Got outstanding refcount %d.\n", source.ref);
    ok(source_pin.ref == 1, "Got outstanding refcount %d.\n", source_pin.ref);
    ok(sink.ref == 1, "Got outstanding refcount %d.\n", sink.ref);
    ok(sink_pin.ref == 1, "Got outstanding refcount %d.\n", sink_pin.ref);
}

START_TEST(acmwrapper)
{
    CoInitialize(NULL);

    test_interfaces();
    test_enum_pins();
    test_find_pin();
    test_pin_info();
    test_connect_pin();
    test_filter_state();

    CoUninitialize();
}
