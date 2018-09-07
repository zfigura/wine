/*
 * File source filter unit tests
 *
 * Copyright 2016 Sebastian Lackner
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
#include "wine/heap.h"
#include "wine/test.h"

#include "util.h"

static const WCHAR source_id[] = {'O','u','t','p','u','t',0};

static IBaseFilter *create_file_source(void)
{
    IBaseFilter *filter = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_AsyncReader, NULL, CLSCTX_INPROC_SERVER,
        &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    return filter;
}

static void load_file(IBaseFilter *filter, const WCHAR *filename)
{
    IFileSourceFilter *filesource;
    HRESULT hr;

    hr = IBaseFilter_QueryInterface(filter, &IID_IFileSourceFilter, (void **)&filesource);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IFileSourceFilter_Load(filesource, filename, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IFileSourceFilter_Release(filesource);
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
    const WCHAR *filename = load_resource(avifile);
    IBaseFilter *filter = create_file_source();
    IPin *pin;

    check_interface(filter, &IID_IBaseFilter, TRUE);
    check_interface(filter, &IID_IFileSourceFilter, TRUE);

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

    load_file(filter, filename);
    IBaseFilter_FindPin(filter, source_name, &pin);

    check_interface(pin, &IID_IAsyncReader, TRUE);
    check_interface(pin, &IID_IPin, TRUE);

    check_interface(pin, &IID_IMediaSeeking, FALSE);

    IPin_Release(pin);
    IBaseFilter_Release(filter);
}

static void test_file_source_filter(void)
{
    static const WCHAR prefix[] = {'w','i','n',0};
    static const struct
    {
        const char *label;
        const char *data;
        DWORD size;
        const GUID *subtype;
    }
    tests[] =
    {
        {
            "AVI",
            "\x52\x49\x46\x46xxxx\x41\x56\x49\x20",
            12,
            &MEDIASUBTYPE_Avi,
        },
        {
            "MPEG1 System",
            "\x00\x00\x01\xBA\x21\x00\x01\x00\x01\x80\x00\x01\x00\x00\x01\xBB",
            16,
            &MEDIASUBTYPE_MPEG1System,
        },
        {
            "MPEG1 Video",
            "\x00\x00\x01\xB3",
            4,
            &MEDIASUBTYPE_MPEG1Video,
        },
        {
            "MPEG1 Audio",
            "\xFF\xE0",
            2,
            &MEDIASUBTYPE_MPEG1Audio,
        },
        {
            "MPEG2 Program",
            "\x00\x00\x01\xBA\x40",
            5,
            &MEDIASUBTYPE_MPEG2_PROGRAM,
        },
        {
            "WAVE",
            "\x52\x49\x46\x46xxxx\x57\x41\x56\x45",
            12,
            &MEDIASUBTYPE_WAVE,
        },
        {
            "unknown format",
            "Hello World",
            11,
            &MEDIASUBTYPE_NULL,
        },
    };
    WCHAR path[MAX_PATH], temp[MAX_PATH], *filename;
    AM_MEDIA_TYPE mt, file_mt, *pmt;
    IFileSourceFilter *filesource;
    IEnumMediaTypes *enum_mt;
    IBaseFilter *filter;
    OLECHAR *olepath;
    DWORD written;
    HANDLE file;
    HRESULT hr;
    ULONG ref;
    IPin *pin;
    BOOL ret;
    int i;

    GetTempPathW(MAX_PATH, temp);
    GetTempFileNameW(temp, prefix, 0, path);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        trace("Running test for %s.\n", tests[i].label);

        file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        ok(file != INVALID_HANDLE_VALUE, "Failed to create file, error %u.\n", GetLastError());
        ret = WriteFile(file, tests[i].data, tests[i].size, &written, NULL);
        ok(ret, "Failed to write file, error %u.\n", GetLastError());
        CloseHandle(file);

        filter = create_file_source();
        IBaseFilter_QueryInterface(filter, &IID_IFileSourceFilter, (void **)&filesource);

        olepath = (void *)0xdeadbeef;
        hr = IFileSourceFilter_GetCurFile(filesource, &olepath, NULL);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(!olepath, "Got path %s.\n", wine_dbgstr_w(olepath));

        hr = IFileSourceFilter_Load(filesource, NULL, NULL);
        ok(hr == E_POINTER, "Got hr %#x.\n", hr);

        hr = IFileSourceFilter_Load(filesource, path, NULL);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        hr = IFileSourceFilter_GetCurFile(filesource, NULL, &mt);
        ok(hr == E_POINTER, "Got hr %#x.\n", hr);

        olepath = NULL;
        hr = IFileSourceFilter_GetCurFile(filesource, &olepath, NULL);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        CoTaskMemFree(olepath);

        olepath = NULL;
        memset(&file_mt, 0x11, sizeof(file_mt));
        hr = IFileSourceFilter_GetCurFile(filesource, &olepath, &file_mt);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(!lstrcmpW(olepath, path), "Expected path %s, got %s.\n",
                wine_dbgstr_w(path), wine_dbgstr_w(olepath));
        ok(IsEqualGUID(&file_mt.majortype, &MEDIATYPE_Stream), "Got major type %s.\n",
                wine_dbgstr_guid(&file_mt.majortype));
        /* winegstreamer hijacks format type detection. */
        if (!IsEqualGUID(tests[i].subtype, &MEDIASUBTYPE_NULL))
            ok(IsEqualGUID(&file_mt.subtype, tests[i].subtype), "Expected subtype %s, got %s.\n",
                wine_dbgstr_guid(tests[i].subtype), wine_dbgstr_guid(&file_mt.subtype));
        ok(file_mt.bFixedSizeSamples == TRUE, "Got fixed size %d.\n", file_mt.bFixedSizeSamples);
        ok(file_mt.bTemporalCompression == FALSE, "Got temporal compression %d.\n",
                file_mt.bTemporalCompression);
        ok(file_mt.lSampleSize == 1, "Got sample size %u.\n", file_mt.lSampleSize);
        ok(IsEqualGUID(&file_mt.formattype, &GUID_NULL), "Got format type %s.\n",
                wine_dbgstr_guid(&file_mt.formattype));
        ok(!file_mt.pUnk, "Got pUnk %p.\n", file_mt.pUnk);
        ok(!file_mt.cbFormat, "Got format size %#x.\n", file_mt.cbFormat);
        ok(!file_mt.pbFormat, "Got format %p.\n", file_mt.pbFormat);
        CoTaskMemFree(olepath);

        hr = IBaseFilter_FindPin(filter, source_name, &pin);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        hr = IPin_EnumMediaTypes(pin, &enum_mt);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        hr = IEnumMediaTypes_Next(enum_mt, 1, &pmt, NULL);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(!memcmp(pmt, &file_mt, sizeof(*pmt)), "Media types did not match.\n");
        CoTaskMemFree(pmt);

        hr = IEnumMediaTypes_Next(enum_mt, 1, &pmt, NULL);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        mt = file_mt;
        mt.subtype = GUID_NULL;
        ok(!memcmp(pmt, &mt, sizeof(*pmt)), "Media types did not match.\n");
        CoTaskMemFree(pmt);

        hr = IEnumMediaTypes_Next(enum_mt, 1, &pmt, NULL);
        ok(hr == S_FALSE, "Got hr %#x.\n", hr);

        IEnumMediaTypes_Release(enum_mt);

        mt = file_mt;
        hr = IPin_QueryAccept(pin, &mt);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        mt.bFixedSizeSamples = FALSE;
        mt.bTemporalCompression = TRUE;
        mt.lSampleSize = 123;
        mt.formattype = FORMAT_VideoInfo;
        hr = IPin_QueryAccept(pin, &mt);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        mt.majortype = MEDIATYPE_Video;
        hr = IPin_QueryAccept(pin, &mt);
        ok(hr == S_FALSE, "Got hr %#x.\n", hr);
        mt.majortype = MEDIATYPE_Stream;

        if (!IsEqualGUID(tests[i].subtype, &GUID_NULL))
        {
            mt.subtype = GUID_NULL;
            hr = IPin_QueryAccept(pin, &mt);
            ok(hr == S_FALSE, "Got hr %#x.\n", hr);
        }

        IPin_Release(pin);
        IFileSourceFilter_Release(filesource);
        ref = IBaseFilter_Release(filter);
        ok(!ref, "Got outstanding refcount %d.\n", ref);

        ret = DeleteFileW(path);
        ok(ret, "Failed to delete file, error %u\n", GetLastError());
    }

    /* test prescribed format */
    filter = create_file_source();
    hr = IBaseFilter_QueryInterface(filter, &IID_IFileSourceFilter, (void **)&filesource);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    mt.majortype = MEDIATYPE_Video;
    mt.subtype = MEDIASUBTYPE_RGB8;
    mt.bFixedSizeSamples = FALSE;
    mt.bTemporalCompression = TRUE;
    mt.lSampleSize = 123;
    mt.formattype = FORMAT_None;
    mt.pUnk = NULL;
    mt.cbFormat = 0;
    mt.pbFormat = NULL;
    filename = load_resource(avifile);
    hr = IFileSourceFilter_Load(filesource, filename, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IFileSourceFilter_GetCurFile(filesource, &olepath, &file_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(&file_mt, &mt, sizeof(mt)), "Media types did not match.\n");
    CoTaskMemFree(olepath);

    hr = IBaseFilter_FindPin(filter, source_name, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_EnumMediaTypes(pin, &enum_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Next(enum_mt, 1, &pmt, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(pmt, &file_mt, sizeof(*pmt)), "Media types did not match.\n");
    CoTaskMemFree(pmt);

    hr = IEnumMediaTypes_Next(enum_mt, 1, &pmt, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(IsEqualGUID(&pmt->majortype, &MEDIATYPE_Stream), "Got major type %s.\n",
            wine_dbgstr_guid(&pmt->majortype));
    ok(IsEqualGUID(&pmt->subtype, &GUID_NULL), "Got subtype %s.\n",
            wine_dbgstr_guid(&pmt->subtype));
    ok(pmt->bFixedSizeSamples == TRUE, "Got fixed size %d.\n", pmt->bFixedSizeSamples);
    ok(!pmt->bTemporalCompression, "Got temporal compression %d.\n", pmt->bTemporalCompression);
    ok(pmt->lSampleSize == 1, "Got sample size %u.\n", pmt->lSampleSize);
    ok(IsEqualGUID(&pmt->formattype, &GUID_NULL), "Got format type %s.\n",
            wine_dbgstr_guid(&pmt->formattype));
    ok(!pmt->pUnk, "Got pUnk %p.\n", pmt->pUnk);
    ok(!pmt->cbFormat, "Got format size %#x.\n", pmt->cbFormat);
    ok(!pmt->pbFormat, "Got format %p.\n", pmt->pbFormat);

    hr = IPin_QueryAccept(pin, pmt);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumMediaTypes_Next(enum_mt, 1, &pmt, NULL);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    IEnumMediaTypes_Release(enum_mt);

    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    mt.bFixedSizeSamples = TRUE;
    mt.bTemporalCompression = FALSE;
    mt.lSampleSize = 456;
    mt.formattype = FORMAT_VideoInfo;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    mt.majortype = MEDIATYPE_Stream;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    mt.majortype = MEDIATYPE_Video;

    mt.subtype = MEDIASUBTYPE_NULL;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    IPin_Release(pin);
    IFileSourceFilter_Release(filesource);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
}

static void test_enum_pins(void)
{
    const WCHAR *filename = load_resource(avifile);
    IBaseFilter *filter = create_file_source();
    IEnumPins *enum1, *enum2;
    IPin *pins[2];
    ULONG count;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    hr = IBaseFilter_EnumPins(filter, &enum1);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumPins_Skip(enum1, 1);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    load_file(filter, filename);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
todo_wine
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    hr = IEnumPins_Reset(enum1);
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
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
}

static void test_find_pin(void)
{
    const WCHAR *filename = load_resource(avifile);
    IBaseFilter *filter = create_file_source();
    IEnumPins *enumpins;
    IPin *pin, *pin2;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    hr = IBaseFilter_FindPin(filter, source_id, &pin);
    ok(hr == VFW_E_NOT_FOUND, "Got hr %#x.\n", hr);

    load_file(filter, filename);

    hr = IBaseFilter_FindPin(filter, source_id, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IBaseFilter_EnumPins(filter, &enumpins);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IEnumPins_Next(enumpins, 1, &pin2, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(pin == pin2, "Expected pin %p, got %p.\n", pin, pin2);

    IPin_Release(pin2);
    IPin_Release(pin);
    IEnumPins_Release(enumpins);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
}

static void test_pin_info(void)
{
    const WCHAR *filename = load_resource(avifile);
    IBaseFilter *filter = create_file_source();
    PIN_DIRECTION dir;
    PIN_INFO info;
    HRESULT hr;
    WCHAR *id;
    ULONG ref;
    IPin *pin;
    BOOL ret;

    load_file(filter, filename);

    hr = IBaseFilter_FindPin(filter, source_name, &pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IPin_QueryPinInfo(pin, &info);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(info.pFilter == filter, "Expected filter %p, got %p.\n", filter, info.pFilter);
    ok(info.dir == PINDIR_OUTPUT, "Got direction %d.\n", info.dir);
    ok(!lstrcmpW(info.achName, source_name), "Got name %s.\n", wine_dbgstr_w(info.achName));
    IBaseFilter_Release(info.pFilter);

    hr = IPin_QueryDirection(pin, &dir);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(dir == PINDIR_OUTPUT, "Got direction %d.\n", dir);

    hr = IPin_QueryId(pin, &id);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!lstrcmpW(id, source_name), "Got id %s.\n", wine_dbgstr_w(id));
    CoTaskMemFree(id);

    IPin_Release(pin);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
}

static HRESULT WINAPI testsink_ReceiveConnection(IPin *iface, IPin *peer, const AM_MEDIA_TYPE *mt)
{
    struct testpin *pin = impl_from_IPin(iface);
    HRESULT hr;
    if (winetest_debug > 1) trace("%p->ReceiveConnection()\n", pin);

    if (pin->accept_mt && memcmp(pin->accept_mt, mt, sizeof(*mt)))
        return VFW_E_TYPE_NOT_ACCEPTED;

    hr = IPin_QueryInterface(peer, &IID_IAsyncReader, (void **)&pin->reader);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!!mt, "Expected non-NULL media type.\n");
    heap_free(pin->mt);
    pin->mt = heap_alloc(sizeof(*mt));
    memcpy(pin->mt, mt, sizeof(*mt));
    pin->peer = peer;
    IPin_AddRef(peer);
    return S_OK;
}

static HRESULT WINAPI testsink_Disconnect(IPin *iface)
{
    struct testpin *pin = impl_from_IPin(iface);
    if (winetest_debug > 1) trace("%p->Disconnect()\n", pin);

    if (pin->peer)
    {
        IPin_Release(pin->peer);
        IAsyncReader_Release(pin->reader);
    }
    pin->peer = NULL;
    return S_OK;
}

static const IPinVtbl testpin_vtbl =
{
    testpin_QueryInterface,
    testpin_AddRef,
    testpin_Release,
    no_Connect,
    testsink_ReceiveConnection,
    testsink_Disconnect,
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
    testpin_init(pin, &testpin_vtbl, PINDIR_INPUT);
    pin->types = types;
    pin->type_count = type_count;
}

static void test_connect_pin(void)
{
    AM_MEDIA_TYPE sink_mt = {0};
    struct testpin sink_pin;
    struct testfilter sink;

    const WCHAR *filename = load_resource(avifile);
    IBaseFilter *filter = create_file_source();
    AM_MEDIA_TYPE *expect_mt, req_mt = {0}, mt;
    IFileSourceFilter *filesource;
    IEnumMediaTypes *enum_mt;
    IPin *pin, *peer;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    sink_mt.majortype = MEDIATYPE_Stream;
    sink_mt.subtype = MEDIASUBTYPE_Avi;
    sink_mt.lSampleSize = 123;
    testsink_init(&sink_pin, &sink_mt, 1);
    testfilter_init(&sink, &sink_pin, 1);

    IBaseFilter_QueryInterface(filter, &IID_IFileSourceFilter, (void **)&filesource);
    IFileSourceFilter_Load(filesource, filename, NULL);
    IBaseFilter_FindPin(filter, source_name, &pin);

    hr = IPin_ConnectedTo(pin, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    hr = IPin_EnumMediaTypes(pin, &enum_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IEnumMediaTypes_Next(enum_mt, 1, &expect_mt, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IEnumMediaTypes_Release(enum_mt);

    hr = IPin_Connect(pin, &sink_pin.IPin_iface, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == pin, "Got pin %p.\n", sink_pin.peer);
    ok(!memcmp(sink_pin.mt, expect_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");

    hr = IPin_ConnectedTo(pin, &peer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(peer == &sink_pin.IPin_iface, "Got peer %p.\n", peer);
    IPin_Release(peer);

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(&mt, expect_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");

    hr = IPin_Disconnect(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == pin, "Got peer %p.\n", sink_pin.peer);
    IPin_Disconnect(&sink_pin.IPin_iface);

    hr = IPin_ConnectedTo(pin, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#x.\n", hr);

    req_mt.majortype = MEDIATYPE_Stream;
    req_mt.subtype = MEDIASUBTYPE_RGB8;
    hr = IPin_Connect(pin, &sink_pin.IPin_iface, &req_mt);
todo_wine {
    ok(hr == VFW_E_NO_ACCEPTABLE_TYPES, "Got hr %#x.\n", hr);
    ok(!sink_pin.peer, "Got peer %p.\n", sink_pin.peer);
    IPin_Disconnect(pin);
    IPin_Disconnect(&sink_pin.IPin_iface);
}

    /* Test wildcard values. */
    req_mt.majortype = MEDIATYPE_NULL;
    req_mt.subtype = MEDIASUBTYPE_NULL;
    hr = IPin_Connect(pin, &sink_pin.IPin_iface, &req_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == pin, "Got peer %p.\n", sink_pin.peer);
    ok(!memcmp(sink_pin.mt, expect_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(&mt, expect_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");

    hr = IPin_Disconnect(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IPin_Disconnect(&sink_pin.IPin_iface);

    /* Our media types are tried if the file source filter's don't work. */
    sink_pin.accept_mt = &sink_mt;

    hr = IPin_Connect(pin, &sink_pin.IPin_iface, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sink_pin.peer == pin, "Got peer %p.\n", sink_pin.peer);
    ok(!memcmp(sink_pin.mt, &sink_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(&mt, &sink_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");

    hr = IPin_Disconnect(pin);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IPin_Disconnect(&sink_pin.IPin_iface);
    IPin_Release(pin);
    IFileSourceFilter_Release(filesource);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
    ok(sink_pin.ref == 1, "Got outstanding refcount %d\n", sink_pin.ref);
    ok(sink.ref == 1, "Got outstanding refcount %d\n", sink.ref);
}

static void test_filter_state(void)
{
    IBaseFilter *filter = create_file_source();
    IMediaControl *control;
    IFilterGraph2 *graph;
    FILTER_STATE state;
    HRESULT hr;
    ULONG ref;

    CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IFilterGraph2, (void **)&graph);
    IFilterGraph2_AddFilter(graph, filter, NULL);
    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);

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
}

static void test_sync_read_aligned(IAsyncReader *reader, IMemAllocator *allocator)
{
    REFERENCE_TIME start_time, end_time;
    IMediaSample *sample;
    HRESULT hr;
    BYTE *data;
    LONG len;
    int i;

    IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    IMediaSample_GetPointer(sample, &data);

    start_time = 0;
    end_time = 512 * (LONGLONG)10000000;
    hr = IMediaSample_SetTime(sample, &start_time, &end_time);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IAsyncReader_SyncReadAligned(reader, sample);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    len = IMediaSample_GetActualDataLength(sample);
    ok(len == 512, "Got length %d.\n", len);

    for (i = 0; i < 512; i++)
        ok(data[i] == i % 111, "Got wrong byte %02x at %u.\n", data[i], i);

    start_time = 512 * (LONGLONG)10000000;
    end_time  = 1024 * (LONGLONG)10000000;
    hr = IMediaSample_SetTime(sample, &start_time, &end_time);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IAsyncReader_SyncReadAligned(reader, sample);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);

    len = IMediaSample_GetActualDataLength(sample);
    ok(len == 88, "Got length %d.\n", len);

    for (i = 0; i < 88; i++)
        ok(data[i] == (512 + i) % 111, "Got wrong byte %02x at %u.\n", data[i], i);

    start_time = 1024 * (LONGLONG)10000000;
    end_time   = 1536 * (LONGLONG)10000000;
    hr = IMediaSample_SetTime(sample, &start_time, &end_time);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IAsyncReader_SyncReadAligned(reader, sample);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    len = IMediaSample_GetActualDataLength(sample);
    ok(len == 0, "Got length %d.\n", len);

    IMediaSample_Release(sample);
}

static void test_request(IAsyncReader *reader, IMemAllocator *allocator)
{
    IMediaSample *sample, *sample2, *ret_sample;
    REFERENCE_TIME start_time, end_time;
    BYTE *data, *data2;
    DWORD_PTR cookie;
    HRESULT hr;
    LONG len;
    int i;

    IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    IMediaSample_GetPointer(sample, &data);
    IMemAllocator_GetBuffer(allocator, &sample2, NULL, NULL, 0);
    IMediaSample_GetPointer(sample2, &data2);

    hr = IAsyncReader_WaitForNext(reader, 0, &ret_sample, &cookie);
    ok(hr == VFW_E_TIMEOUT, "Got hr %#x.\n", hr);

    start_time = 0;
    end_time = 512 * (LONGLONG)10000000;
    hr = IMediaSample_SetTime(sample, &start_time, &end_time);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IAsyncReader_Request(reader, sample, 123);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IAsyncReader_WaitForNext(reader, 1000, &ret_sample, &cookie);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(ret_sample == sample, "Expected sample %p, got %p.\n", sample, ret_sample);
    ok(cookie == 123, "Got cookie %lu.\n", cookie);

    len = IMediaSample_GetActualDataLength(sample);
    ok(len == 512, "Got length %d.\n", hr);

    for (i = 0; i < 512; i++)
        ok(data[i] == i % 111, "Got wrong byte %02x at %u.\n", data[i], i);

    start_time = 1024 * (LONGLONG)10000000;
    end_time   = 1536 * (LONGLONG)10000000;
    hr = IMediaSample_SetTime(sample, &start_time, &end_time);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IAsyncReader_Request(reader, sample, 123);
    ok(hr == HRESULT_FROM_WIN32(ERROR_HANDLE_EOF), "Got hr %#x.\n", hr);

    start_time = 0;
    end_time = 512 * (LONGLONG)10000000;
    hr = IMediaSample_SetTime(sample, &start_time, &end_time);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IAsyncReader_Request(reader, sample, 123);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    start_time = 512 * (LONGLONG)10000000;
    end_time  = 1024 * (LONGLONG)10000000;
    hr = IMediaSample_SetTime(sample2, &start_time, &end_time);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IAsyncReader_Request(reader, sample2, 456);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IAsyncReader_WaitForNext(reader, 1000, &ret_sample, &cookie);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    if (cookie == 123)
    {
        ok(ret_sample == sample, "Expected sample %p, got %p.\n", sample, ret_sample);

        hr = IAsyncReader_WaitForNext(reader, 1000, &ret_sample, &cookie);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(ret_sample == sample2, "Expected sample %p, got %p.\n", sample2, ret_sample);
        ok(cookie == 456, "Got cookie %lu.\n", cookie);
    }
    else
    {
        ok(cookie == 456, "Got cookie %lu.\n", cookie);

        hr = IAsyncReader_WaitForNext(reader, 1000, &ret_sample, &cookie);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(ret_sample == sample, "Expected sample %p, got %p.\n", sample2, ret_sample);
        ok(cookie == 123, "Got cookie %lu.\n", cookie);
    }

    for (i = 0; i < 512; i++)
        ok(data[i] == i % 111, "Got wrong byte %02x at %u.\n", data[i], i);

    for (i = 0; i < 88; i++)
        ok(data2[i] == (512 + i) % 111, "Got wrong byte %02x at %u.\n", data2[i], i);

    IMediaSample_Release(sample);
    IMediaSample_Release(sample2);
}

static void test_async_reader(void)
{
    ALLOCATOR_PROPERTIES req_props = {2, 1024, 512, 0}, ret_props;
    IBaseFilter *filter = create_file_source();
    IFileSourceFilter *filesource;
    LONGLONG length, available;
    IMemAllocator *allocator;
    WCHAR filename[MAX_PATH];
    IAsyncReader *reader;
    BYTE buffer[20];
    DWORD written;
    HANDLE file;
    HRESULT hr;
    ULONG ref;
    IPin *pin;
    BOOL ret;
    int i;

    GetTempPathW(ARRAY_SIZE(filename), filename);
    lstrcatW(filename, avifile);
    file = CreateFileW(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "Failed to create file, error %u.\n", GetLastError());
    for (i = 0; i < 600; i++)
    {
        BYTE b = i % 111;
        WriteFile(file, &b, 1, &written, NULL);
    }
    CloseHandle(file);

    IBaseFilter_QueryInterface(filter, &IID_IFileSourceFilter, (void **)&filesource);
    IFileSourceFilter_Load(filesource, filename, NULL);
    IBaseFilter_FindPin(filter, source_name, &pin);

    hr = IPin_QueryInterface(pin, &IID_IAsyncReader, (void **)&reader);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IAsyncReader_Length(reader, &length, &available);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(length == 600, "Got length %s.\n", wine_dbgstr_longlong(length));
    ok(available == 600, "Got available length %s.\n", wine_dbgstr_longlong(available));

    memset(buffer, 0xcc, sizeof(buffer));
    hr = IAsyncReader_SyncRead(reader, 0, 10, buffer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    for (i = 0; i < 10; i++)
        ok(buffer[i] == i % 111, "Got wrong byte %02x at %u.\n", buffer[i], i);

    hr = IAsyncReader_SyncRead(reader, 0, 10, buffer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    for (i = 0; i < 10; i++)
        ok(buffer[i] == i % 111, "Got wrong byte %02x at %u.\n", buffer[i], i);

    hr = IAsyncReader_SyncRead(reader, 10, 10, buffer);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    for (i = 0; i < 10; i++)
        ok(buffer[i] == (10 + i) % 111, "Got wrong byte %02x at %u.\n", buffer[i], i);

    hr = IAsyncReader_SyncRead(reader, 590, 20, buffer);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    for (i = 0; i < 10; i++)
        ok(buffer[i] == (590 + i) % 111, "Got wrong byte %02x at %u.\n", buffer[i], i);
    for (; i < 20; i++)
        ok(buffer[i] == 0xcc, "Got wrong byte %02x at %u.\n", buffer[i], i);

    memset(buffer, 0xcc, sizeof(buffer));
    hr = IAsyncReader_SyncRead(reader, 600, 10, buffer);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(buffer[0] == 0xcc, "Got wrong byte %02x.\n", buffer[0]);

    ret_props = req_props;
    hr = IAsyncReader_RequestAllocator(reader, NULL, &ret_props, &allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(ret_props.cBuffers == 2, "got %d buffers\n", ret_props.cBuffers);
    ok(ret_props.cbBuffer == 1024, "got buffer size %d\n", ret_props.cbBuffer);
    ok(ret_props.cbAlign == 512, "got alignment %d\n", ret_props.cbAlign);
    ok(ret_props.cbPrefix == 0, "got prefix %d\n", ret_props.cbPrefix);

    IMemAllocator_Commit(allocator);

    test_sync_read_aligned(reader, allocator);
    test_request(reader, allocator);

    IMemAllocator_Release(allocator);
    IAsyncReader_Release(reader);
    IPin_Release(pin);
    IFileSourceFilter_Release(filesource);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %u.\n", GetLastError());
}

START_TEST(filesource)
{
    CoInitialize(NULL);

    test_interfaces();
    test_enum_pins();
    test_find_pin();
    test_pin_info();
    test_connect_pin();
    test_filter_state();
    test_file_source_filter();
    test_async_reader();

    CoUninitialize();
}
