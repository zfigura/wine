/*
 * Audio effects DMOs
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
#include "wingdi.h"
#include "mmsystem.h"
#include "mmreg.h"
#include "uuids.h"
#define COBJMACROS
#include "dmo.h"
#include "wine/debug.h"
#include "wine/heap.h"

#include "dsdmo_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dsdmo);

struct base_effect {
    struct base_dmo dmo;
};

static const WAVEFORMATEX supported_formats[] =
{
    {WAVE_FORMAT_IEEE_FLOAT, 1, 44100, 176400, 4, 32},
};

static int check_supported_format(WAVEFORMATEX *wfx)
{
    int i;

    for (i = 0; i < sizeof(supported_formats)/sizeof(supported_formats[0]); i++)
    {
        if (wfx->wFormatTag == supported_formats[i].wFormatTag &&
            wfx->nChannels  == supported_formats[i].nChannels)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static HRESULT stream_set_type(struct dmo_stream *stream, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    if (flags & DMO_SET_TYPEF_CLEAR)
    {
        MoDeleteMediaType(stream->current);
        stream->current = NULL;
        return S_OK;
    }

    if (!type)
        return E_POINTER;

    if (IsEqualGUID(&type->majortype, &MEDIATYPE_Audio) &&
        IsEqualGUID(&type->formattype, &FORMAT_WaveFormatEx))
    {
        WAVEFORMATEX *wfx = (WAVEFORMATEX *)type->pbFormat;

        if (check_supported_format(wfx))
        {
            if (!(flags & DMO_SET_TYPEF_TEST_ONLY))
            {
                MoDeleteMediaType(stream->current);
                MoDuplicateMediaType(&stream->current, type);
            }

            return S_OK;
        }

        FIXME("unsupported wave tag %x, channels %d, samples/sec %d, bytes/sec %d\n",
              wfx->wFormatTag, wfx->nChannels, wfx->nSamplesPerSec, wfx->nAvgBytesPerSec);
        FIXME("%d %d %d\n", wfx->nBlockAlign, wfx->wBitsPerSample, wfx->cbSize);
    }

    FIXME("unsupported stream type %s, subtype %s, format %s\n", debugstr_guid(&type->majortype),
          debugstr_guid(&type->subtype), debugstr_guid(&type->formattype));

    return DMO_E_TYPE_NOT_ACCEPTED;
}

static inline struct base_effect *base_effect_impl_from_IMediaObject(IMediaObject *iface)
{
    return CONTAINING_RECORD(iface, struct base_effect, dmo.IMediaObject_iface);
}

static HRESULT WINAPI base_effect_SetInputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    struct base_effect *This = base_effect_impl_from_IMediaObject(iface);

    TRACE("(%p)->(%d %p %x)\n", This, index, type, flags);

    if (index >= This->dmo.inputs_count)
        return DMO_E_INVALIDSTREAMINDEX;

    return stream_set_type(&This->dmo.inputs[index], type, flags);
}

static HRESULT WINAPI base_effect_SetOutputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    struct base_effect *This = base_effect_impl_from_IMediaObject(iface);

    TRACE("(%p)->(%d %p %x)\n", This, index, type, flags);

    if (index >= This->dmo.outputs_count)
        return DMO_E_INVALIDSTREAMINDEX;

    return stream_set_type(&This->dmo.outputs[index], type, flags);
}

struct reverb_impl {
    struct base_dmo dmo;
};

static inline struct reverb_impl *impl_from_IMediaObject(IMediaObject *iface)
{
    return CONTAINING_RECORD(iface, struct reverb_impl, dmo.IMediaObject_iface);
}

static HRESULT WINAPI I3DL2Reverb_QueryInterface(IMediaObject *iface, REFIID riid, void **ppv)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), ppv);

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IMediaObject))
    {
        *ppv = &This->dmo.IMediaObject_iface;
    }
    else
    {
        WARN("no interface for %s\n", debugstr_guid(riid));
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IMediaObject_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI I3DL2Reverb_Release(IMediaObject *iface)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);
    ULONG refcount = InterlockedDecrement(&This->dmo.refcount);

    TRACE("(%p) Release from %d\n", This, refcount + 1);

    if (!refcount)
    {
        destroy_base_dmo(&This->dmo);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return refcount;
}

static HRESULT WINAPI I3DL2Reverb_GetInputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %p) stub!\n", This, index, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_GetOutputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %p) stub!\n", This, index, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_GetInputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %d %p) stub!\n", This, index, type_index, type);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_GetOutputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %d %p) stub!\n", This, index, type_index, type);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_SetInputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %p %x) stub!\n", This, index, type, flags);

    WAVEFORMATEX *wex = type->pbFormat;
    FIXME("tag %x, %d channels, %d Hz, %d bytes/sec, %d align, %d bits/sample\n", wex->wFormatTag, wex->nChannels, wex->nSamplesPerSec, wex->nAvgBytesPerSec, wex->nBlockAlign, wex->wBitsPerSample);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_SetOutputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %p %x) stub!\n", This, index, type, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_GetInputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size, DWORD *max_lookahead, DWORD *alignment)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %p %p %p) stub!\n", This, index, size, max_lookahead, alignment);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_GetOutputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size, DWORD *alignment)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %p %p) stub!\n", This, index, size, alignment);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_GetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME *latency)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %p) stub!\n", This, index, latency);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_SetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME latency)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %s) stub!\n", This, index, wine_dbgstr_longlong(latency));

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_Flush(IMediaObject *iface)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->() stub!\n", This);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_Discontinuity(IMediaObject *iface, DWORD index)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d) stub!\n", This, index);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_AllocateStreamingResources(IMediaObject *iface)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->() stub!\n", This);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_FreeStreamingResources(IMediaObject *iface)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->() stub!\n", This);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_GetInputStatus(IMediaObject *iface, DWORD index, DWORD *flags)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %p) stub!\n", This, index, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_ProcessInput(IMediaObject *iface, DWORD index,
    IMediaBuffer *buffer, DWORD flags, REFERENCE_TIME timestamp, REFERENCE_TIME timelength)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%d %p %x %s %s) stub!\n", This, index, buffer, flags,
          wine_dbgstr_longlong(timestamp), wine_dbgstr_longlong(timelength));

    return E_NOTIMPL;
}

static HRESULT WINAPI I3DL2Reverb_ProcessOutput(IMediaObject *iface, DWORD flags, DWORD count, DMO_OUTPUT_DATA_BUFFER *buffers, DWORD *status)
{
    struct reverb_impl *This = impl_from_IMediaObject(iface);

    FIXME("(%p)->(%x %d %p %p) stub!\n", This, flags, count, buffers, status);

    return E_NOTIMPL;
}

static const IMediaObjectVtbl IMediaObject_vtbl = {
    I3DL2Reverb_QueryInterface,
    base_dmo_AddRef,
    I3DL2Reverb_Release,
    base_dmo_GetStreamCount,
    I3DL2Reverb_GetInputStreamInfo,
    I3DL2Reverb_GetOutputStreamInfo,
    I3DL2Reverb_GetInputType,
    I3DL2Reverb_GetOutputType,
    base_effect_SetInputType,
    base_effect_SetOutputType,
    base_dmo_GetInputCurrentType,
    base_dmo_GetOutputCurrentType,
    I3DL2Reverb_GetInputSizeInfo,
    I3DL2Reverb_GetOutputSizeInfo,
    I3DL2Reverb_GetInputMaxLatency,
    I3DL2Reverb_SetInputMaxLatency,
    I3DL2Reverb_Flush,
    I3DL2Reverb_Discontinuity,
    I3DL2Reverb_AllocateStreamingResources,
    I3DL2Reverb_FreeStreamingResources,
    I3DL2Reverb_GetInputStatus,
    I3DL2Reverb_ProcessInput,
    I3DL2Reverb_ProcessOutput,
    base_dmo_Lock,
};

HRESULT create_I3DL2Reverb(REFIID riid, void **ppv)
{
    struct reverb_impl *This;

    if (!(This = heap_alloc(sizeof(*This))))
    {
        *ppv = NULL;
        return E_OUTOFMEMORY;
    }

    This->dmo.IMediaObject_iface.lpVtbl = &IMediaObject_vtbl;

    This->dmo.inputs_count = 1;
    This->dmo.outputs_count = 1;

    init_base_dmo(&This->dmo);

    return IMediaObject_QueryInterface(&This->dmo.IMediaObject_iface, riid, ppv);
}
