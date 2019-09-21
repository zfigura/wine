/*
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
#define COBJMACROS
#include "objbase.h"
#include "rpcproxy.h"
#include "wmcodecdsp.h"
#include "mftransform.h"
#include "mferror.h"
#include "mfapi.h"
#include "wine/test.h"

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
    IUnknown *dmo;
    HRESULT hr;
    ULONG ref;

    hr = CoCreateInstance(&CLSID_CResamplerMediaObject, NULL,
            CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&dmo);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    check_interface(dmo, &IID_IMFTransform, TRUE);
    check_interface(dmo, &IID_IUnknown, TRUE);
    check_interface(dmo, &IID_IWMResamplerProps, TRUE);

    ref = IUnknown_Release(dmo);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

static ULONG get_refcount(void *iface)
{
    IUnknown *unknown = iface;
    IUnknown_AddRef(unknown);
    return IUnknown_Release(unknown);
}

static const GUID test_iid = {0x33333333};
static LONG outer_ref = 1;

static HRESULT WINAPI outer_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IWMResamplerProps)
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
    IWMResamplerProps *props, *props2;
    IUnknown *unk, *unk2;
    HRESULT hr;
    ULONG ref;

    props = (IWMResamplerProps *)0xdeadbeef;
    hr = CoCreateInstance(&CLSID_CResamplerMediaObject, &test_outer,
            CLSCTX_INPROC_SERVER, &IID_IWMResamplerProps, (void **)&props);
    ok(hr == E_NOINTERFACE, "Got hr %#x.\n", hr);
    ok(!props, "Got interface %p.\n", props);

    hr = CoCreateInstance(&CLSID_CResamplerMediaObject, &test_outer,
            CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
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

    hr = IUnknown_QueryInterface(unk, &IID_IWMResamplerProps, (void **)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IWMResamplerProps_QueryInterface(props, &IID_IUnknown, (void **)&unk2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(unk2 == (IUnknown *)0xdeadbeef, "Got unexpected IUnknown %p.\n", unk2);

    hr = IWMResamplerProps_QueryInterface(props, &IID_IWMResamplerProps, (void **)&props2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props2 == (IWMResamplerProps *)0xdeadbeef, "Got unexpected IWMResamplerProps %p.\n", props2);

    hr = IUnknown_QueryInterface(unk, &test_iid, (void **)&unk2);
    ok(hr == E_NOINTERFACE, "Got hr %#x.\n", hr);
    ok(!unk2, "Got unexpected IUnknown %p.\n", unk2);

    hr = IWMResamplerProps_QueryInterface(props, &test_iid, (void **)&unk2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(unk2 == (IUnknown *)0xdeadbeef, "Got unexpected IUnknown %p.\n", unk2);

    IWMResamplerProps_Release(props);
    ref = IUnknown_Release(unk);
    ok(!ref, "Got unexpected refcount %d.\n", ref);
    ok(outer_ref == 1, "Got unexpected refcount %d.\n", outer_ref);
}

static void test_transform(void)
{
    DWORD input_count, output_count, input_id, output_id, flags, count;
    DWORD input_min, input_max, output_min, output_max;
    MFT_OUTPUT_STREAM_INFO output_info;
    MFT_INPUT_STREAM_INFO input_info;
    IMFTransform *transform;
    IMFAttributes *attr;
    UINT32 independent;
    IMFMediaType *mt;
    HRESULT hr;
    ULONG ref;
    GUID guid;

    hr = CoCreateInstance(&CLSID_CResamplerMediaObject, NULL,
            CLSCTX_INPROC_SERVER, &IID_IMFTransform, (void **)&transform);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMFTransform_GetStreamLimits(transform, &input_min, &input_max, &output_min, &output_max);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(input_min == 1, "Got input min %u.\n", input_min);
    ok(input_max == 1, "Got input max %u.\n", input_min);
    ok(output_min == 1, "Got output min %u.\n", output_min);
    ok(output_max == 1, "Got output max %u.\n", output_min);

    hr = IMFTransform_GetStreamCount(transform, &input_count, &output_count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(input_count == 1, "Got %u inputs.\n", input_count);
    ok(output_count == 1, "Got %u outputs.\n", output_count);

    hr = IMFTransform_GetStreamIDs(transform, 1, &input_id, 1, &output_id);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);

    hr = IMFTransform_GetInputStreamInfo(transform, 0, &input_info);
    ok(hr == MF_E_TRANSFORM_TYPE_NOT_SET, "Got hr %#x.\n", hr);
    hr = IMFTransform_GetOutputStreamInfo(transform, 0, &output_info);
    ok(hr == MF_E_TRANSFORM_TYPE_NOT_SET, "Got hr %#x.\n", hr);

    hr = IMFTransform_GetAttributes(transform, &attr);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);
    hr = IMFTransform_GetInputStreamAttributes(transform, 0, &attr);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);
    hr = IMFTransform_GetOutputStreamAttributes(transform, 0, &attr);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);
    hr = IMFTransform_DeleteInputStream(transform, 0);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);
    input_id = 100;
    hr = IMFTransform_AddInputStreams(transform, 1, &input_id);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);

    hr = IMFTransform_GetInputAvailableType(transform, 0, 0, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMFMediaType_GetMajorType(mt, &guid);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(IsEqualGUID(&guid, &MFMediaType_Audio), "Got major type %s.\n", wine_dbgstr_guid(&guid));
    hr = IMFMediaType_GetCount(mt, &count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 3, "Got count %u.\n", count);
    hr = IMFMediaType_GetGUID(mt, &MF_MT_SUBTYPE, &guid);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(IsEqualGUID(&guid, &MFAudioFormat_Float), "Got subtype %s.\n", wine_dbgstr_guid(&guid));
    hr = IMFMediaType_GetUINT32(mt, &MF_MT_ALL_SAMPLES_INDEPENDENT, &independent);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(independent == TRUE, "Got value %u.\n", independent);
    ref = IMFMediaType_Release(mt);
    ok(!ref, "Got unexpected refcount %d.\n", ref);

    hr = IMFTransform_GetInputAvailableType(transform, 0, 1, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMFMediaType_GetMajorType(mt, &guid);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(IsEqualGUID(&guid, &MFMediaType_Audio), "Got major type %s.\n", wine_dbgstr_guid(&guid));
    hr = IMFMediaType_GetCount(mt, &count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 3, "Got count %u.\n", count);
    hr = IMFMediaType_GetGUID(mt, &MF_MT_SUBTYPE, &guid);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(IsEqualGUID(&guid, &MFAudioFormat_PCM), "Got subtype %s.\n", wine_dbgstr_guid(&guid));
    hr = IMFMediaType_GetUINT32(mt, &MF_MT_ALL_SAMPLES_INDEPENDENT, &independent);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(independent == TRUE, "Got value %u.\n", independent);
    ref = IMFMediaType_Release(mt);
    ok(!ref, "Got unexpected refcount %d.\n", ref);

    hr = IMFTransform_GetInputAvailableType(transform, 0, 2, &mt);
    ok(hr == MF_E_NO_MORE_TYPES, "Got hr %#x.\n", hr);

    hr = IMFTransform_GetInputCurrentType(transform, 0, &mt);
    ok(hr == MF_E_TRANSFORM_TYPE_NOT_SET, "Got hr %#x.\n", hr);
    hr = IMFTransform_GetOutputCurrentType(transform, 0, &mt);
    ok(hr == MF_E_TRANSFORM_TYPE_NOT_SET, "Got hr %#x.\n", hr);

    hr = IMFTransform_GetInputStatus(transform, 0, &flags);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!flags, "Got flags %#x.\n", flags);
    hr = IMFTransform_GetOutputStatus(transform, &flags);
    ok(hr == E_NOTIMPL, "Got hr %#x.\n", hr);

    ref = IMFTransform_Release(transform);
    ok(!ref, "Got unexpected refcount %d.\n", ref);
}

START_TEST(resampledmo)
{
    IUnknown *dmo;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    hr = CoCreateInstance(&CLSID_CResamplerMediaObject, NULL,
            CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&dmo);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("Failed to create audio resampler DMO.\n");
        return;
    }
    IUnknown_Release(dmo);

    test_interfaces();
    test_aggregation();
    test_transform();

    CoUninitialize();
}
