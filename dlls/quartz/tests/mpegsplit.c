/*
 * MPEG-1 splitter filter unit tests
 *
 * Copyright 2015 Anton Baskanov
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
#include "wine/test.h"

#include "util.h"

static const WCHAR inputW[] = {'I','n','p','u','t',0};
static const WCHAR audioW[] = {'A','u','d','i','o',0};

static IBaseFilter *create_mpeg_splitter(void)
{
    IBaseFilter *filter = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_MPEG1Splitter, NULL, CLSCTX_INPROC_SERVER,
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
    const WCHAR *filename = load_resource(mp3file);
    IBaseFilter *filter = create_mpeg_splitter();
    IFilterGraph2 *graph = connect_input(filter, filename);
    IPin *pin;

    check_interface(filter, &IID_IAMStreamSelect, TRUE);
    check_interface(filter, &IID_IBaseFilter, TRUE);

    check_interface(filter, &IID_IAMFilterMiscFlags, FALSE);
    check_interface(filter, &IID_IBasicAudio, FALSE);
    check_interface(filter, &IID_IBasicVideo, FALSE);
    check_interface(filter, &IID_IKsPropertySet, FALSE);
    check_interface(filter, &IID_IMediaPosition, FALSE);
    check_interface(filter, &IID_IMediaSeeking, FALSE);
    check_interface(filter, &IID_IPersistPropertyBag, FALSE);
    check_interface(filter, &IID_IPin, FALSE);
    check_interface(filter, &IID_IQualityControl, FALSE);
    check_interface(filter, &IID_IQualProp, FALSE);
    check_interface(filter, &IID_IReferenceClock, FALSE);
    check_interface(filter, &IID_IVideoWindow, FALSE);

    IBaseFilter_FindPin(filter, inputW, &pin);

    check_interface(pin, &IID_IPin, TRUE);

todo_wine
    check_interface(pin, &IID_IMediaSeeking, FALSE);

    IPin_Release(pin);

    IBaseFilter_FindPin(filter, audioW, &pin);

    check_interface(pin, &IID_IMediaSeeking, TRUE);
    check_interface(pin, &IID_IPin, TRUE);

    check_interface(pin, &IID_IAsyncReader, FALSE);

    IPin_Release(pin);

    IBaseFilter_Release(filter);
    IFilterGraph2_Release(graph);
    DeleteFileW(filename);
}

static void test_enum_pins(void)
{
    const WCHAR *filename = load_resource(mp3file);
    IBaseFilter *filter = create_mpeg_splitter();
    IEnumPins *enum1, *enum2;
    IFilterGraph2 *graph;
    ULONG count, ref;
    IPin *pins[3];
    HRESULT hr;
    BOOL ret;

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

    graph = connect_input(filter, filename);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
todo_wine
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumPins_Reset(enum1);
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

    hr = IEnumPins_Next(enum1, 2, pins, &count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 2, "Got count %u.\n", count);
    IPin_Release(pins[0]);
    IPin_Release(pins[1]);

    hr = IEnumPins_Reset(enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum1, 3, pins, &count);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(count == 2, "Got count %u.\n", count);
    IPin_Release(pins[0]);
    IPin_Release(pins[1]);

    IEnumPins_Release(enum1);
    IFilterGraph2_Release(graph);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
}

static void test_find_pin(void)
{
    static const WCHAR input_pinW[] = {'i','n','p','u','t',' ','p','i','n',0};
    const WCHAR *filename = load_resource(mp3file);
    IBaseFilter *filter = create_mpeg_splitter();
    IFilterGraph2 *graph;
    IEnumPins *enum_pins;
    IPin *pin, *pin2;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    hr = IBaseFilter_FindPin(filter, input_pinW, &pin);
    ok(hr == VFW_E_NOT_FOUND, "Got hr %#x.\n", hr);

    hr = IBaseFilter_FindPin(filter, inputW, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IPin_Release(pin);

    hr = IBaseFilter_FindPin(filter, audioW, &pin);
    ok(hr == VFW_E_NOT_FOUND, "Got hr %#x.\n", hr);

    graph = connect_input(filter, filename);

    hr = IBaseFilter_EnumPins(filter, &enum_pins);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum_pins, 1, &pin2, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_FindPin(filter, inputW, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(pin == pin2, "Expected pin %p, got %p.\n", pin2, pin);
    IPin_Release(pin);
    IPin_Release(pin2);

    hr = IEnumPins_Next(enum_pins, 1, &pin2, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_FindPin(filter, audioW, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(pin == pin2, "Expected pin %p, got %p.\n", pin2, pin);
    IPin_Release(pin);
    IPin_Release(pin2);

    IEnumPins_Release(enum_pins);
    IFilterGraph2_Release(graph);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
}

static void test_pin_info(void)
{
    const WCHAR *filename = load_resource(mp3file);
    IBaseFilter *filter = create_mpeg_splitter();
    IFilterGraph2 *graph;
    PIN_DIRECTION dir;
    PIN_INFO info;
    HRESULT hr;
    WCHAR *id;
    ULONG ref;
    IPin *pin;
    BOOL ret;

    graph = connect_input(filter, filename);

    hr = IBaseFilter_FindPin(filter, inputW, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_QueryPinInfo(pin, &info);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(info.pFilter == filter, "Expected filter %p, got %p.\n", filter, info.pFilter);
    ok(info.dir == PINDIR_INPUT, "Got direction %d.\n", info.dir);
    ok(!lstrcmpW(info.achName, inputW), "Got name %s.\n", wine_dbgstr_w(info.achName));
    IBaseFilter_Release(info.pFilter);

    hr = IPin_QueryDirection(pin, &dir);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(dir == PINDIR_INPUT, "Got direction %d.\n", dir);

    hr = IPin_QueryId(pin, &id);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!lstrcmpW(id, inputW), "Got id %s.\n", wine_dbgstr_w(id));
    CoTaskMemFree(id);

    IPin_Release(pin);

    hr = IBaseFilter_FindPin(filter, audioW, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    check_interface(pin, &IID_IPin, TRUE);
    check_interface(pin, &IID_IMediaSeeking, TRUE);

    hr = IPin_QueryPinInfo(pin, &info);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(info.pFilter == filter, "Expected filter %p, got %p.\n", filter, info.pFilter);
    ok(info.dir == PINDIR_OUTPUT, "Got direction %d.\n", info.dir);
    ok(!lstrcmpW(info.achName, audioW), "Got name %s.\n", wine_dbgstr_w(info.achName));
    IBaseFilter_Release(info.pFilter);

    hr = IPin_QueryDirection(pin, &dir);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(dir == PINDIR_OUTPUT, "Got direction %d.\n", dir);

    hr = IPin_QueryId(pin, &id);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!lstrcmpW(id, audioW), "Got id %s.\n", wine_dbgstr_w(id));
    CoTaskMemFree(id);

    IPin_Release(pin);

    IFilterGraph2_Release(graph);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
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

static void testsource_init(struct testpin *pin)
{
    testpin_init(pin, &testsource_vtbl, PINDIR_OUTPUT);
    pin->IAsyncReader_iface.lpVtbl = &testreader_vtbl;
}

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

static void testsink_init(struct testpin *pin, AM_MEDIA_TYPE *types, ULONG type_count)
{
    testpin_init(pin, &testsink_vtbl, PINDIR_INPUT);
    pin->IMemInputPin_iface.lpVtbl = &testmeminput_vtbl;
    pin->types = types;
    pin->type_count = type_count;
}

static void test_connect_pin(void)
{
    static const WCHAR outputW[] = {'O','u','t','p','u','t',0};
    AM_MEDIA_TYPE sink_mt = {0};
    struct testpin source_pin, sink_pin;
    struct testfilter source, sink;

    IPin *splitter_source, *splitter_sink, *peer, *reader_source;
    IBaseFilter *reader, *splitter = create_mpeg_splitter();
    AM_MEDIA_TYPE *expect_mt, mt, req_mt = {0};
    WCHAR *filename = load_resource(mp3file);
    IFileSourceFilter *filesource;
    IEnumMediaTypes *enummt;
    IFilterGraph2 *graph;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    testsource_init(&source_pin);
    testfilter_init(&source, &source_pin, 1);
    testsink_init(&sink_pin, &sink_mt, 1);
    testfilter_init(&sink, &sink_pin, 1);

    IBaseFilter_FindPin(splitter, inputW, &splitter_sink);

    hr = IPin_ConnectedTo(splitter_sink, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(splitter_sink, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    CoCreateInstance(&CLSID_AsyncReader, NULL, CLSCTX_INPROC_SERVER,
            &IID_IBaseFilter, (void **)&reader);
    IBaseFilter_QueryInterface(reader, &IID_IFileSourceFilter, (void **)&filesource);
    IFileSourceFilter_Load(filesource, filename, NULL);

    CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IFilterGraph2, (void **)&graph);
    IFilterGraph2_AddFilter(graph, reader, NULL);
    IFilterGraph2_AddFilter(graph, splitter, NULL);
    IFilterGraph2_AddFilter(graph, &source.IBaseFilter_iface, NULL);
    IFilterGraph2_AddFilter(graph, &sink.IBaseFilter_iface, NULL);

    IBaseFilter_FindPin(reader, outputW, &reader_source);
    IPin_QueryInterface(reader_source, &IID_IAsyncReader, (void **)&source_pin.reader);

    req_mt.majortype = MEDIATYPE_Stream;
    req_mt.subtype = MEDIASUBTYPE_MPEG1Audio;
    hr = IFilterGraph2_ConnectDirect(graph, &source_pin.IPin_iface, splitter_sink, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_ConnectedTo(splitter_sink, &peer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(peer == &source_pin.IPin_iface, "Got peer %p.\n", peer);
    IPin_Release(peer);

    hr = IPin_ConnectionMediaType(splitter_sink, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(&mt, &req_mt, sizeof(mt)), "Media types didn't match.\n");

    hr = IFilterGraph2_Disconnect(graph, splitter_sink);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IFilterGraph2_Disconnect(graph, splitter_sink);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    IFilterGraph2_Disconnect(graph, &source_pin.IPin_iface);

    hr = IPin_ConnectedTo(splitter_sink, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(splitter_sink, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    hr = IFilterGraph2_ConnectDirect(graph, &source_pin.IPin_iface, splitter_sink, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IBaseFilter_FindPin(splitter, audioW, &splitter_source);

    hr = IPin_ConnectedTo(splitter_source, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(splitter_source, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    IPin_EnumMediaTypes(splitter_source, &enummt);
    IEnumMediaTypes_Next(enummt, 1, &expect_mt, NULL);
    IEnumMediaTypes_Release(enummt);

    hr = IFilterGraph2_ConnectDirect(graph, splitter_source, &sink_pin.IPin_iface, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == splitter_source, "Got peer %p.\n", sink_pin.peer);
    ok(compare_media_types(sink_pin.mt, expect_mt), "Media types didn't match.\n");

    hr = IPin_ConnectedTo(splitter_source, &peer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(peer == &sink_pin.IPin_iface, "Got peer %p.\n", peer);
    IPin_Release(peer);

    hr = IPin_ConnectionMediaType(splitter_source, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(compare_media_types(&mt, expect_mt), "Media types didn't match.\n");

    hr = IFilterGraph2_Disconnect(graph, splitter_source);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == splitter_source, "Got peer %p.\n", sink_pin.peer);
    IFilterGraph2_Disconnect(graph, &sink_pin.IPin_iface);

    req_mt.subtype = MEDIASUBTYPE_PCM;
    hr = IFilterGraph2_ConnectDirect(graph, splitter_source, &sink_pin.IPin_iface, &req_mt);
todo_wine {
    ok(hr == VFW_E_NO_ACCEPTABLE_TYPES, "Got hr %#x.\n", hr);
    ok(!sink_pin.peer, "Got peer %p.\n", sink_pin.peer);
    IFilterGraph2_Disconnect(graph, splitter_source);
    IFilterGraph2_Disconnect(graph, &sink_pin.IPin_iface);
}

    req_mt.majortype = GUID_NULL;
    req_mt.subtype = GUID_NULL;
    hr = IFilterGraph2_ConnectDirect(graph, splitter_source, &sink_pin.IPin_iface, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == splitter_source, "Got peer %p.\n", sink_pin.peer);
    ok(compare_media_types(sink_pin.mt, expect_mt), "Media types didn't match.\n");
    IFilterGraph2_Disconnect(graph, splitter_source);
    IFilterGraph2_Disconnect(graph, &sink_pin.IPin_iface);

    sink_pin.accept_mt = &sink_mt;
    hr = IFilterGraph2_ConnectDirect(graph, splitter_source, &sink_pin.IPin_iface, NULL);
todo_wine {
    ok(hr == VFW_E_NO_ACCEPTABLE_TYPES, "Got hr %#x.\n", hr);
    ok(!sink_pin.peer, "Got peer %p.\n", sink_pin.peer);
    IFilterGraph2_Disconnect(graph, splitter_source);
    IFilterGraph2_Disconnect(graph, &sink_pin.IPin_iface);
}

    copy_media_type(&sink_mt, expect_mt);
    sink_mt.lSampleSize = 123;
    hr = IFilterGraph2_ConnectDirect(graph, splitter_source, &sink_pin.IPin_iface, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == splitter_source, "Got peer %p.\n", sink_pin.peer);
    ok(compare_media_types(sink_pin.mt, &sink_mt), "Media types didn't match.\n");
    IFilterGraph2_Disconnect(graph, splitter_source);
    IFilterGraph2_Disconnect(graph, &sink_pin.IPin_iface);

    IPin_Release(splitter_source);
    IPin_Release(splitter_sink);
    IAsyncReader_Release(source_pin.reader);
    IPin_Release(reader_source);
    IFileSourceFilter_Release(filesource);
    IBaseFilter_Release(reader);
    IFilterGraph2_Release(graph);
    ref = IBaseFilter_Release(splitter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
    ok(sink.ref == 1, "Got outstanding refcount %d.\n", sink.ref);
    ok(sink_pin.ref == 1, "Got outstanding refcount %d.\n", sink_pin.ref);
}

static void test_filter_state(void)
{
    struct testpin sink_pin;
    struct testfilter sink;

    const WCHAR *filename = load_resource(mp3file);
    IBaseFilter *splitter = create_mpeg_splitter();
    IMediaControl *control;
    IFilterGraph2 *graph;
    FILTER_STATE state;
    IPin *source;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    testsink_init(&sink_pin, NULL, 0);
    testfilter_init(&sink, &sink_pin, 1);

    graph = connect_input(splitter, filename);
    IFilterGraph2_AddFilter(graph, &sink.IBaseFilter_iface, NULL);
    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);

    IBaseFilter_FindPin(splitter, audioW, &source);
    IFilterGraph2_ConnectDirect(graph, source, &sink_pin.IPin_iface, NULL);
    IPin_Release(source);

    hr = IBaseFilter_GetState(splitter, 0, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Stopped, "Got state %d.\n", state);

    hr = IMediaControl_Run(control);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(splitter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Running, "Got state %d.\n", state);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(splitter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Stopped, "Got state %d.\n", state);

    hr = IMediaControl_Pause(control);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(splitter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Paused, "Got state %d.\n", state);

    hr = IMediaControl_Run(control);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(splitter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Running, "Got state %d.\n", state);

    hr = IMediaControl_Pause(control);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(splitter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Paused, "Got state %d.\n", state);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IBaseFilter_GetState(splitter, 1000, &state);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(state == State_Stopped, "Got state %d.\n", state);

    IMediaControl_Release(control);
    IFilterGraph2_Release(graph);
    ref = IBaseFilter_Release(splitter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
    ok(sink.ref == 1, "got %d outstanding refs\n", sink.ref);
    ok(sink_pin.ref == 1, "got %d outstanding refs\n", sink_pin.ref);
}

START_TEST(mpegsplit)
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
