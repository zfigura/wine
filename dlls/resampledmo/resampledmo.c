/*
 * Audio resampler DMO
 *
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
#include "wine/debug.h"
#include "wine/heap.h"

WINE_DEFAULT_DEBUG_CHANNEL(resampledmo);

static HINSTANCE resampledmo_instance;

struct resampler
{
    IUnknown IUnknown_inner;
    IWMResamplerProps IWMResamplerProps_iface;
    IMFTransform IMFTransform_iface;
    IUnknown *outer_unk;
    LONG refcount;
};

static inline struct resampler *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct resampler, IUnknown_inner);
}

static HRESULT WINAPI inner_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    struct resampler *dmo = impl_from_IUnknown(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown))
        *out = iface;
    else if (IsEqualGUID(iid, &IID_IWMResamplerProps))
        *out = &dmo->IWMResamplerProps_iface;
    else if (IsEqualGUID(iid, &IID_IMFTransform))
        *out = &dmo->IMFTransform_iface;
    else
    {
        WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
        *out = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI inner_AddRef(IUnknown *iface)
{
    struct resampler *dmo = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedIncrement(&dmo->refcount);

    TRACE("%p increasing refcount to %u.\n", dmo, refcount);

    return refcount;
}

static ULONG WINAPI inner_Release(IUnknown *iface)
{
    struct resampler *dmo = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&dmo->refcount);

    TRACE("%p decreasing refcount to %u.\n", dmo, refcount);

    if (!refcount)
    {
        heap_free(dmo);
    }

    return refcount;
}

static const IUnknownVtbl inner_vtbl =
{
    inner_QueryInterface,
    inner_AddRef,
    inner_Release,
};

static inline struct resampler *impl_from_IWMResamplerProps(IWMResamplerProps *iface)
{
    return CONTAINING_RECORD(iface, struct resampler, IWMResamplerProps_iface);
}

static HRESULT WINAPI resampler_props_QueryInterface(IWMResamplerProps *iface, REFIID iid, void **out)
{
    struct resampler *dmo = impl_from_IWMResamplerProps(iface);
    return IUnknown_QueryInterface(dmo->outer_unk, iid, out);
}

static ULONG WINAPI resampler_props_AddRef(IWMResamplerProps *iface)
{
    struct resampler *dmo = impl_from_IWMResamplerProps(iface);
    return IUnknown_AddRef(dmo->outer_unk);
}

static ULONG WINAPI resampler_props_Release(IWMResamplerProps *iface)
{
    struct resampler *dmo = impl_from_IWMResamplerProps(iface);
    return IUnknown_Release(dmo->outer_unk);
}

static HRESULT WINAPI resampler_props_SetHalfFilterLength(IWMResamplerProps *iface, LONG len)
{
    FIXME("iface %p, len %d, stub!\n", iface, len);
    return E_NOTIMPL;
}

static HRESULT WINAPI resampler_props_SetUserChannelMtx(IWMResamplerProps *iface, float *matrix)
{
    FIXME("iface %p, matrix %p, stub!\n", iface, matrix);
    return E_NOTIMPL;
}

static const IWMResamplerPropsVtbl resampler_props_vtbl =
{
    resampler_props_QueryInterface,
    resampler_props_AddRef,
    resampler_props_Release,
    resampler_props_SetHalfFilterLength,
    resampler_props_SetUserChannelMtx,
};

static inline struct resampler *impl_from_IMFTransform(IMFTransform *iface)
{
    return CONTAINING_RECORD(iface, struct resampler, IMFTransform_iface);
}

static HRESULT WINAPI transform_QueryInterface(IMFTransform *iface, REFIID iid, void **out)
{
    struct resampler *dmo = impl_from_IMFTransform(iface);
    return IUnknown_QueryInterface(dmo->outer_unk, iid, out);
}

static ULONG WINAPI transform_AddRef(IMFTransform *iface)
{
    struct resampler *dmo = impl_from_IMFTransform(iface);
    return IUnknown_AddRef(dmo->outer_unk);
}

static ULONG WINAPI transform_Release(IMFTransform *iface)
{
    struct resampler *dmo = impl_from_IMFTransform(iface);
    return IUnknown_Release(dmo->outer_unk);
}

static HRESULT WINAPI transform_GetStreamLimits(IMFTransform *iface,
        DWORD *input_min, DWORD *input_max, DWORD *output_min, DWORD *output_max)
{
    FIXME("iface %p, input_min %p, input_max %p, output_min %p, output_max %p, stub!\n",
            iface, input_min, input_max, output_min, output_max);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetStreamCount(IMFTransform *iface, DWORD *inputs, DWORD *outputs)
{
    FIXME("iface %p, inputs %p, outputs %p, stub!\n", iface, inputs, outputs);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetStreamIDs(IMFTransform *iface, DWORD input_count,
        DWORD *inputs, DWORD output_count, DWORD *outputs)
{
    FIXME("iface %p, input_count %u, inputs %p, output_count %u, outputs %p, stub!\n",
            iface, input_count, inputs, output_count, outputs);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetInputStreamInfo(IMFTransform *iface, DWORD id,
        MFT_INPUT_STREAM_INFO *info)
{
    FIXME("iface %p, id %u, info %p, stub!\n", iface, id, info);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputStreamInfo(IMFTransform *iface, DWORD id,
        MFT_OUTPUT_STREAM_INFO *info)
{
    FIXME("iface %p, id %u, info %p, stub!\n", iface, id, info);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetAttributes(IMFTransform *iface, IMFAttributes **attr)
{
    FIXME("iface %p, attr %p, stub!\n", iface, attr);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetInputStreamAttributes(IMFTransform *iface,
        DWORD id, IMFAttributes **attr)
{
    FIXME("iface %p, id %u, attr %p, stub!\n", iface, id, attr);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputStreamAttributes(IMFTransform *iface,
        DWORD id, IMFAttributes **attr)
{
    FIXME("iface %p, id %u, attr %p, stub!\n", iface, id, attr);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_DeleteInputStream(IMFTransform *iface, DWORD id)
{
    FIXME("iface %p, id %u, stub!\n", iface, id);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_AddInputStreams(IMFTransform *iface, DWORD count, DWORD *ids)
{
    FIXME("iface %p, count %u, ids %p, stub!\n", iface, count, ids);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetInputAvailableType(IMFTransform *iface,
        DWORD id, DWORD index, IMFMediaType **type)
{
    FIXME("iface %p, id %u, index %u, type %p, stub!\n", iface, id, index, type);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputAvailableType(IMFTransform *iface,
        DWORD id, DWORD index, IMFMediaType **type)
{
    FIXME("iface %p, id %u, index %u, type %p, stub!\n", iface, id, index, type);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_SetInputType(IMFTransform *iface,
        DWORD id, IMFMediaType *type, DWORD flags)
{
    FIXME("iface %p, id %u, type %p, flags %#x, stub!\n", iface, id, type, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_SetOutputType(IMFTransform *iface,
        DWORD id, IMFMediaType *type, DWORD flags)
{
    FIXME("iface %p, id %u, type %p, flags %#x, stub!\n", iface, id, type, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetInputCurrentType(IMFTransform *iface,
        DWORD id, IMFMediaType **type)
{
    FIXME("iface %p, id %u, type %p, stub!\n", iface, id, type);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputCurrentType(IMFTransform *iface,
        DWORD id, IMFMediaType **type)
{
    FIXME("iface %p, id %u, type %p, stub!\n", iface, id, type);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetInputStatus(IMFTransform *iface, DWORD id, DWORD *flags)
{
    FIXME("iface %p, id %u, flags %p, stub!\n", iface, id, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputStatus(IMFTransform *iface, DWORD *flags)
{
    FIXME("iface %p, flags %p, stub!\n", iface, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_SetOutputBounds(IMFTransform *iface, LONGLONG lower, LONGLONG upper)
{
    FIXME("iface %p, lower %s, upper %s, stub!\n",
            iface, wine_dbgstr_longlong(lower), wine_dbgstr_longlong(upper));
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessEvent(IMFTransform *iface, DWORD id, IMFMediaEvent *event)
{
    FIXME("iface %p, id %u, event %p, stub!\n", iface, id, event);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessMessage(IMFTransform *iface,
        MFT_MESSAGE_TYPE message, ULONG_PTR param)
{
    FIXME("iface %p, message %#x, param %#lx, stub!\n", iface, message, param);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessInput(IMFTransform *iface, DWORD id, IMFSample *sample, DWORD flags)
{
    FIXME("iface %p, id %u, sample %p, flags %#x, stub!\n", iface, id, sample, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessOutput(IMFTransform *iface, DWORD flags,
        DWORD count, MFT_OUTPUT_DATA_BUFFER *samples, DWORD *status)
{
    FIXME("iface %p, flags %#x, count %u, samples %p, status %p, stub!\n",
            iface, flags, count, samples, status);
    return E_NOTIMPL;
}

static const IMFTransformVtbl transform_vtbl =
{
    transform_QueryInterface,
    transform_AddRef,
    transform_Release,
    transform_GetStreamLimits,
    transform_GetStreamCount,
    transform_GetStreamIDs,
    transform_GetInputStreamInfo,
    transform_GetOutputStreamInfo,
    transform_GetAttributes,
    transform_GetInputStreamAttributes,
    transform_GetOutputStreamAttributes,
    transform_DeleteInputStream,
    transform_AddInputStreams,
    transform_GetInputAvailableType,
    transform_GetOutputAvailableType,
    transform_SetInputType,
    transform_SetOutputType,
    transform_GetInputCurrentType,
    transform_GetOutputCurrentType,
    transform_GetInputStatus,
    transform_GetOutputStatus,
    transform_SetOutputBounds,
    transform_ProcessEvent,
    transform_ProcessMessage,
    transform_ProcessInput,
    transform_ProcessOutput,
};

static HRESULT resampler_create(IUnknown *outer, REFIID iid, void **out)
{
    struct resampler *object;
    HRESULT hr;

    if (!(object = heap_alloc_zero(sizeof(*object))))
        return E_OUTOFMEMORY;

    object->IUnknown_inner.lpVtbl = &inner_vtbl;
    object->IWMResamplerProps_iface.lpVtbl = &resampler_props_vtbl;
    object->IMFTransform_iface.lpVtbl = &transform_vtbl;
    object->refcount = 1;
    object->outer_unk = outer ? outer : &object->IUnknown_inner;

    hr = IUnknown_QueryInterface(&object->IUnknown_inner, iid, out);
    IUnknown_Release(&object->IUnknown_inner);
    return hr;
}

static HRESULT WINAPI class_factory_QueryInterface(IClassFactory *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory))
    {
        IClassFactory_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    WARN("%s not implemented, returning E_NOINTERFACE\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI class_factory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI class_factory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI class_factory_CreateInstance(IClassFactory *iface,
        IUnknown *outer, REFIID iid, void **out)
{
    TRACE("iface %p, outer %p, iid %s, out %p.\n", iface, outer, debugstr_guid(iid), out);

    *out = NULL;

    if (outer && !IsEqualGUID(iid, &IID_IUnknown))
        return E_NOINTERFACE;

    return resampler_create(outer, iid, out);
}

static HRESULT WINAPI class_factory_LockServer(IClassFactory *iface, BOOL lock)
{
    FIXME("(%d) stub\n", lock);
    return S_OK;
}

static const IClassFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_CreateInstance,
    class_factory_LockServer
};

static IClassFactory resampler_cf = { &class_factory_vtbl };

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    TRACE("instance %p, reason %d, reserved %p.\n", instance, reason, reserved);
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        resampledmo_instance = instance;
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    TRACE("clsid %s, iid %s, out %p.\n", debugstr_guid(clsid), debugstr_guid(iid), out);

    if (IsEqualGUID(clsid, &CLSID_CResamplerMediaObject))
        return IClassFactory_QueryInterface(&resampler_cf, iid, out);

    FIXME("class %s not available\n", debugstr_guid(clsid));
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return __wine_register_resources( resampledmo_instance );
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return __wine_unregister_resources( resampledmo_instance );
}
