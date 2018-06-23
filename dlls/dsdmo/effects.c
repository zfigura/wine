/*
 * DirectSound standard effects
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

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#define COBJMACROS
#include "dmo.h"
#include "wine/debug.h"
#include "wine/heap.h"

#include "dsdmo_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dsdmo);

struct effect {
    IMediaObject IMediaObject_iface;
    LONG ref;
    CLSID clsid;
};

static inline struct effect *impl_from_IMediaObject(IMediaObject *iface)
{
    return CONTAINING_RECORD(iface, struct effect, IMediaObject_iface);
}

static HRESULT WINAPI MediaObject_QueryInterface(IMediaObject *iface, REFIID iid, void **ppv)
{
    struct effect *This = impl_from_IMediaObject(iface);

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(iid), ppv);

    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IMediaObject))
        *ppv = &This->IMediaObject_iface;
    else
    {
        FIXME("no interface for %s\n", debugstr_guid(iid));
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IMediaObject_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI MediaObject_AddRef(IMediaObject *iface)
{
    struct effect *This = impl_from_IMediaObject(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) AddRef from %d\n", This, ref - 1);

    return ref;
}

static ULONG WINAPI MediaObject_Release(IMediaObject *iface)
{
    struct effect *This = impl_from_IMediaObject(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) Release from %d\n", This, ref + 1);

    if (!ref)
    {
        heap_free(This);
    }
    return ref;
}

static HRESULT WINAPI MediaObject_GetStreamCount(IMediaObject *iface, DWORD *input, DWORD *output)
{
    FIXME("(%p)->(%p, %p) stub!\n", iface, input, output);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    FIXME("(%p)->(%d, %p) stub!\n", iface, index, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetOutputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    FIXME("(%p)->(%d, %p) stub!\n", iface, index, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type)
{
    FIXME("(%p)->(%d, %d, %p) stub!\n", iface, index, type_index, type);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetOutputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type)
{
    FIXME("(%p)->(%d, %d, %p) stub!\n", iface, index, type_index, type);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_SetInputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    FIXME("(%p)->(%d, %p, %#x) stub!\n", iface, index, type, flags);

    return S_OK;
}

static HRESULT WINAPI MediaObject_SetOutputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    FIXME("(%p)->(%d, %p, %#x) stub!\n", iface, index, type, flags);

    return S_OK;
}

static HRESULT WINAPI MediaObject_GetInputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type)
{
    FIXME("(%p)->(%d, %p) stub!\n", iface, index, type);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetOutputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type)
{
    FIXME("(%p)->(%d, %p) stub!\n", iface, index, type);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size, DWORD *max_lookahead, DWORD *alignment)
{
    FIXME("(%p)->(%d, %p, %p, %p) stub!\n", iface, index, size, max_lookahead, alignment);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetOutputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size, DWORD *alignment)
{
    FIXME("(%p)->(%d, %p, %p) stub!\n", iface, index, size, alignment);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME *latency)
{
    FIXME("(%p)->(%d, %p) stub!\n", iface, index, latency);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_SetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME latency)
{
    FIXME("(%p)->(%d, %s) stub!\n", iface, index, wine_dbgstr_longlong(latency));

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_Flush(IMediaObject *iface)
{
    FIXME("(%p)->() stub!\n", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_Discontinuity(IMediaObject *iface, DWORD index)
{
    FIXME("(%p)->(%d) stub!\n", iface, index);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_AllocateStreamingResources(IMediaObject *iface)
{
    FIXME("(%p)->() stub!\n", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_FreeStreamingResources(IMediaObject *iface)
{
    FIXME("(%p)->() stub!\n", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_GetInputStatus(IMediaObject *iface, DWORD index, DWORD *flags)
{
    FIXME("(%p)->(%d, %p) stub!\n", iface, index, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_ProcessInput(IMediaObject *iface, DWORD index,
    IMediaBuffer *buffer, DWORD flags, REFERENCE_TIME timestamp, REFERENCE_TIME timelength)
{
    FIXME("(%p)->(%d, %p, %#x, %s, %s) stub!\n", iface, index, buffer, flags,
          wine_dbgstr_longlong(timestamp), wine_dbgstr_longlong(timelength));

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_ProcessOutput(IMediaObject *iface, DWORD flags,
    DWORD count, DMO_OUTPUT_DATA_BUFFER *buffers, DWORD *status)
{
    FIXME("(%p)->(%#x, %d, %p, %p) stub!\n", iface, flags, count, buffers, status);

    return E_NOTIMPL;
}

static HRESULT WINAPI MediaObject_Lock(IMediaObject *iface, LONG lock)
{
    FIXME("(%p)->(%d) stub!\n", iface, lock);

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

HRESULT create_effect(const CLSID *clsid, IUnknown *outer, REFIID iid, void **obj)
{
    struct effect *This;
    HRESULT hr;

    if (!(This = heap_alloc(sizeof(*This))))
    {
        *obj = NULL;
        return E_OUTOFMEMORY;
    }

    This->IMediaObject_iface.lpVtbl = &MediaObject_vtbl;
    This->ref = 1;
    This->clsid = *clsid;

    hr = IMediaObject_QueryInterface(&This->IMediaObject_iface, iid, obj);
    IMediaObject_Release(&This->IMediaObject_iface);
    return hr;
}
