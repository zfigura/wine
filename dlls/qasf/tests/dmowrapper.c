/*
 * DMO wrapper filter unit tests
 *
 * Copyright (C) 2018 Zebediah Figura
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
#include "dmo.h"
#include "dmodshow.h"
#include "wine/test.h"

#include "initguid.h"

IUnknown *wrapper_unk;

static HRESULT WINAPI MediaObject_QueryInterface(IMediaObject *iface, REFIID iid, void **obj)
{
    if (IsEqualGUID(iid, &IID_IMediaObject) || IsEqualGUID(iid, &IID_IUnknown))
    {
        *obj = iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_IDMOQualityControl) ||
             IsEqualGUID(iid, &IID_IDMOVideoOutputOptimizations))
        return E_NOINTERFACE;

    ok(0, "unexpected iid %s\n", wine_dbgstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI MediaObject_AddRef(IMediaObject *iface)
{
    return 2;
}

static ULONG WINAPI MediaObject_Release(IMediaObject *iface)
{
    return 1;
}

static HRESULT WINAPI MediaObject_GetStreamCount(IMediaObject *iface, DWORD *input, DWORD *output)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetOutputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetOutputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_SetInputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_SetOutputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetOutputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size, DWORD *lookahead, DWORD *align)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetOutputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size, DWORD *alignment)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME *latency)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_SetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME latency)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_Flush(IMediaObject *iface)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_Discontinuity(IMediaObject *iface, DWORD index)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_AllocateStreamingResources(IMediaObject *iface)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_FreeStreamingResources(IMediaObject *iface)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputStatus(IMediaObject *iface, DWORD index, DWORD *flags)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_ProcessInput(IMediaObject *iface, DWORD index,
    IMediaBuffer *buffer, DWORD flags, REFERENCE_TIME timestamp, REFERENCE_TIME timelength)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_ProcessOutput(IMediaObject *iface, DWORD flags, DWORD count, DMO_OUTPUT_DATA_BUFFER *buffers, DWORD *status)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_Lock(IMediaObject *iface, LONG lock)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static const IMediaObjectVtbl MediaObject_vtbl = {
    MediaObject_QueryInterface,
    MediaObject_AddRef,
    MediaObject_Release,
    MediaObject_GetStreamCount,
    MediaObject_GetInputStreamInfo,
    MediaObject_GetOutputStreamInfo,
    MediaObject_GetInputType,
    MediaObject_GetOutputType,
    MediaObject_SetInputType,
    MediaObject_SetOutputType,
    MediaObject_GetInputCurrentType,
    MediaObject_GetOutputCurrentType,
    MediaObject_GetInputSizeInfo,
    MediaObject_GetOutputSizeInfo,
    MediaObject_GetInputMaxLatency,
    MediaObject_SetInputMaxLatency,
    MediaObject_Flush,
    MediaObject_Discontinuity,
    MediaObject_AllocateStreamingResources,
    MediaObject_FreeStreamingResources,
    MediaObject_GetInputStatus,
    MediaObject_ProcessInput,
    MediaObject_ProcessOutput,
    MediaObject_Lock,
};

static IMediaObject MediaObject = { &MediaObject_vtbl };

static HRESULT WINAPI ClassFactory_QueryInterface(IClassFactory *iface, REFIID iid, void **obj)
{
    *obj = NULL;

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory)) {
        *obj = iface;
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID_IMarshal))
        return E_NOINTERFACE;

    ok(0, "unexpected iid %s\n", wine_dbgstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI ClassFactory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI ClassFactory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI ClassFactory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **obj)
{
    ok(outer == wrapper_unk, "expected %p, got %p\n", wrapper_unk, outer);
    ok(IsEqualGUID(iid, &IID_IUnknown), "got iid %s\n", wine_dbgstr_guid(iid));

    *obj = &MediaObject;
    return S_OK;
}

static HRESULT WINAPI ClassFactory_LockServer(IClassFactory *iface, BOOL lock)
{
    ok(0, "unexpected call\n");
    return S_OK;
}

static const IClassFactoryVtbl ClassFactoryVtbl = {
    ClassFactory_QueryInterface,
    ClassFactory_AddRef,
    ClassFactory_Release,
    ClassFactory_CreateInstance,
    ClassFactory_LockServer
};

static IClassFactory MediaObject_cf = { &ClassFactoryVtbl };

DEFINE_GUID(CLSID_test_dmo,0x12344321,0,0,0,0,0,0,0,0,0,0);

static void test_dmo_wrapper(void)
{
    static const WCHAR nameW[] = {'T','e','s','t',' ','D','M','O',0};
    IDMOWrapperFilter *wrapper;
    DWORD cookie;
    HRESULT hr;

    hr = DMORegister(nameW, &CLSID_test_dmo, &DMOCATEGORY_AUDIO_DECODER, 0, 0, NULL, 0, NULL);
    if (hr == E_FAIL)
    {
        skip("Not enough permissions to register DMO.\n");
        return;
    }
    ok(hr == S_OK, "DMORegister failed: %#x\n", hr);

    hr = CoRegisterClassObject(&CLSID_test_dmo, (IUnknown *)&MediaObject_cf,
        CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, &cookie);
    ok(hr == S_OK, "CoRegisterClassObject failed: %#x\n", hr);

    hr = CoCreateInstance(&CLSID_DMOWrapperFilter, NULL, CLSCTX_INPROC_SERVER,
        &IID_IDMOWrapperFilter, (void **)&wrapper);
    ok(hr == S_OK, "got %#x\n", hr);

    hr = IDMOWrapperFilter_QueryInterface(wrapper, &IID_IUnknown, (void **)&wrapper_unk);
    ok(hr == S_OK, "got %#x\n", hr);

    hr = IDMOWrapperFilter_Init(wrapper, &CLSID_test_dmo, &DMOCATEGORY_AUDIO_DECODER);
    ok(hr == S_OK, "got %#x\n", hr);

    IUnknown_Release(wrapper_unk);
    IDMOWrapperFilter_Release(wrapper);

    CoRevokeClassObject(cookie);
    DMOUnregister(&CLSID_test_dmo, &DMOCATEGORY_AUDIO_DECODER);
}

START_TEST(dmowrapper)
{
    CoInitialize(NULL);

    test_dmo_wrapper();

    CoUninitialize();
}
