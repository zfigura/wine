/*
 * Direct Sound Audio Renderer
 *
 * Copyright 2004 Christian Costa
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

#include "quartz_private.h"
#include "pin.h"

#include "uuids.h"
#include "vfwmsgs.h"
#include "windef.h"
#include "winbase.h"
#include "dshow.h"
#include "evcode.h"
#include "strmif.h"
#include "dsound.h"
#include "amaudio.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

/* NOTE: buffer can still be filled completely,
 * but we start waiting until only this amount is buffered
 */
static const REFERENCE_TIME DSoundRenderer_Max_Fill = 150 * 10000;

static const IBaseFilterVtbl DSoundRender_Vtbl;
static const IBasicAudioVtbl IBasicAudio_Vtbl;
static const IReferenceClockVtbl IReferenceClock_Vtbl;
static const IAMDirectSoundVtbl IAMDirectSound_Vtbl;

typedef struct DSoundRenderImpl
{
    BaseRenderer renderer;

    IBasicAudio IBasicAudio_iface;
    IReferenceClock IReferenceClock_iface;
    IAMDirectSound IAMDirectSound_iface;

    struct reference_clock clock;

    IDirectSound8 *dsound;
    LPDIRECTSOUNDBUFFER dsbuffer;
    DWORD buf_size;
    DWORD in_loop;
    DWORD last_playpos, writepos;

    /* We last started playing at about this many ticks. */
    ULONGLONG start_ticks;

    REFERENCE_TIME play_time;

    LONG volume;
    LONG pan;
} DSoundRenderImpl;

static inline DSoundRenderImpl *impl_from_BaseRenderer(BaseRenderer *iface)
{
    return CONTAINING_RECORD(iface, DSoundRenderImpl, renderer);
}

static inline DSoundRenderImpl *impl_from_IBaseFilter(IBaseFilter *iface)
{
    return CONTAINING_RECORD(iface, DSoundRenderImpl, renderer.filter.IBaseFilter_iface);
}

static inline DSoundRenderImpl *impl_from_IBasicAudio(IBasicAudio *iface)
{
    return CONTAINING_RECORD(iface, DSoundRenderImpl, IBasicAudio_iface);
}

static inline DSoundRenderImpl *impl_from_IReferenceClock(IReferenceClock *iface)
{
    return CONTAINING_RECORD(iface, DSoundRenderImpl, IReferenceClock_iface);
}

static inline DSoundRenderImpl *impl_from_IAMDirectSound(IAMDirectSound *iface)
{
    return CONTAINING_RECORD(iface, DSoundRenderImpl, IAMDirectSound_iface);
}

static REFERENCE_TIME time_from_pos(DSoundRenderImpl *This, DWORD pos) {
    WAVEFORMATEX *wfx = (WAVEFORMATEX *)This->renderer.sink.pin.mtCurrent.pbFormat;
    REFERENCE_TIME ret = 10000000;
    ret = ret * pos / wfx->nAvgBytesPerSec;
    return ret;
}

static DWORD pos_from_time(DSoundRenderImpl *This, REFERENCE_TIME time) {
    WAVEFORMATEX *wfx = (WAVEFORMATEX *)This->renderer.sink.pin.mtCurrent.pbFormat;
    REFERENCE_TIME ret = time;
    ret *= wfx->nAvgBytesPerSec;
    ret /= 10000000;
    ret -= ret % wfx->nBlockAlign;
    return ret;
}

static void DSoundRender_UpdatePositions(DSoundRenderImpl *This, DWORD *seqwritepos, DWORD *minwritepos) {
    WAVEFORMATEX *wfx = (WAVEFORMATEX *)This->renderer.sink.pin.mtCurrent.pbFormat;
    BYTE *buf1, *buf2;
    DWORD size1, size2, playpos, writepos, old_writepos, old_playpos, adv;
    BOOL writepos_set = This->writepos < This->buf_size;

    /* Update position and zero */
    old_writepos = This->writepos;
    old_playpos = This->last_playpos;
    if (old_writepos <= old_playpos)
        old_writepos += This->buf_size;

    IDirectSoundBuffer_GetCurrentPosition(This->dsbuffer, &playpos, &writepos);
    if (old_playpos > playpos) {
        adv = This->buf_size + playpos - old_playpos;
        This->play_time += time_from_pos(This, This->buf_size);
    } else
        adv = playpos - old_playpos;
    This->last_playpos = playpos;
    if (adv) {
        TRACE("Moving from %u to %u: clearing %u bytes\n", old_playpos, playpos, adv);
        IDirectSoundBuffer_Lock(This->dsbuffer, old_playpos, adv, (void**)&buf1, &size1, (void**)&buf2, &size2, 0);
        memset(buf1, wfx->wBitsPerSample == 8 ? 128  : 0, size1);
        memset(buf2, wfx->wBitsPerSample == 8 ? 128  : 0, size2);
        IDirectSoundBuffer_Unlock(This->dsbuffer, buf1, size1, buf2, size2);
    }
    *minwritepos = writepos;
    if (!writepos_set || old_writepos < writepos) {
        if (writepos_set) {
            This->writepos = This->buf_size;
            FIXME("Underrun of data occurred!\n");
        }
        *seqwritepos = writepos;
    } else
        *seqwritepos = This->writepos;
}

static HRESULT DSoundRender_GetWritePos(DSoundRenderImpl *This, DWORD *ret_writepos, REFERENCE_TIME write_at, DWORD *pfree, DWORD *skip)
{
    WAVEFORMATEX *wfx = (WAVEFORMATEX *)This->renderer.sink.pin.mtCurrent.pbFormat;
    DWORD writepos, min_writepos, playpos;
    REFERENCE_TIME max_lag = 50 * 10000;
    REFERENCE_TIME min_lag = 25 * 10000;
    REFERENCE_TIME cur, writepos_t, delta_t;

    DSoundRender_UpdatePositions(This, &writepos, &min_writepos);
    playpos = This->last_playpos;
    if (This->renderer.filter.pClock == &This->IReferenceClock_iface) {
        max_lag = min_lag;
        cur = This->play_time + time_from_pos(This, playpos);
        cur -= This->renderer.filter.rtStreamStart;
    } else if (This->renderer.filter.pClock) {
        IReferenceClock_GetTime(This->renderer.filter.pClock, &cur);
        cur -= This->renderer.filter.rtStreamStart;
    } else
        write_at = -1;

    if (writepos == min_writepos)
        max_lag = 0;

    *skip = 0;
    if (write_at < 0) {
        *ret_writepos = writepos;
        goto end;
    }

    if (writepos >= playpos)
        writepos_t = cur + time_from_pos(This, writepos - playpos);
    else
        writepos_t = cur + time_from_pos(This, This->buf_size + writepos - playpos);

    /* write_at: Starting time of sample */
    /* cur: current time of play position */
    /* writepos_t: current time of our pointer play position */
    delta_t = write_at - writepos_t;
    if (delta_t >= -max_lag && delta_t <= max_lag) {
        TRACE("Continuing from old position\n");
        *ret_writepos = writepos;
    } else if (delta_t < 0) {
        REFERENCE_TIME past, min_writepos_t;
        WARN("Delta too big %i/%i, overwriting old data or even skipping\n", (int)delta_t / 10000, (int)max_lag / 10000);
        if (min_writepos >= playpos)
            min_writepos_t = cur + time_from_pos(This, min_writepos - playpos);
        else
            min_writepos_t = cur + time_from_pos(This, This->buf_size - playpos + min_writepos);
        past = min_writepos_t - write_at;
        if (past >= 0) {
            DWORD skipbytes = pos_from_time(This, past);
            WARN("Skipping %u bytes\n", skipbytes);
            *skip = skipbytes;
            *ret_writepos = min_writepos;
        } else {
            DWORD aheadbytes = pos_from_time(This, -past);
            WARN("Advancing %u bytes\n", aheadbytes);
            *ret_writepos = (min_writepos + aheadbytes) % This->buf_size;
        }
    } else /* delta_t > 0 */ {
        DWORD aheadbytes;
        WARN("Delta too big %i/%i, too far ahead\n", (int)delta_t / 10000, (int)max_lag / 10000);
        aheadbytes = pos_from_time(This, delta_t);
        WARN("Advancing %u bytes\n", aheadbytes);
        if (delta_t >= DSoundRenderer_Max_Fill)
            return S_FALSE;
        *ret_writepos = (min_writepos + aheadbytes) % This->buf_size;
    }
end:
    if (playpos > *ret_writepos)
        *pfree = playpos - *ret_writepos;
    else if (playpos == *ret_writepos)
        *pfree = This->buf_size - wfx->nBlockAlign;
    else
        *pfree = This->buf_size + playpos - *ret_writepos;
    if (time_from_pos(This, This->buf_size - *pfree) >= DSoundRenderer_Max_Fill) {
        TRACE("Blocked: too full %i / %i\n", (int)(time_from_pos(This, This->buf_size - *pfree)/10000), (int)(DSoundRenderer_Max_Fill / 10000));
        return S_FALSE;
    }
    return S_OK;
}

static HRESULT DSoundRender_HandleEndOfStream(DSoundRenderImpl *This)
{
    while (This->renderer.filter.state == State_Running)
    {
        DWORD pos1, pos2;
        DSoundRender_UpdatePositions(This, &pos1, &pos2);
        if (pos1 == pos2)
            break;

        This->in_loop = 1;
        LeaveCriticalSection(&This->renderer.filter.csFilter);
        LeaveCriticalSection(&This->renderer.csRenderLock);
        WaitForSingleObject(This->renderer.flush_event, 10);
        EnterCriticalSection(&This->renderer.csRenderLock);
        EnterCriticalSection(&This->renderer.filter.csFilter);
        This->in_loop = 0;
    }

    return S_OK;
}

static HRESULT DSoundRender_SendSampleData(DSoundRenderImpl* This, REFERENCE_TIME tStart, REFERENCE_TIME tStop, const BYTE *data, DWORD size)
{
    HRESULT hr;

    while (size && This->renderer.filter.state != State_Stopped) {
        DWORD writepos, skip = 0, free, size1, size2, ret;
        BYTE *buf1, *buf2;

        if (This->renderer.filter.state == State_Running)
            hr = DSoundRender_GetWritePos(This, &writepos, tStart, &free, &skip);
        else
            hr = S_FALSE;

        if (hr != S_OK) {
            This->in_loop = 1;
            LeaveCriticalSection(&This->renderer.csRenderLock);
            ret = WaitForSingleObject(This->renderer.flush_event, 10);
            EnterCriticalSection(&This->renderer.csRenderLock);
            This->in_loop = 0;
            if (This->renderer.sink.flushing || This->renderer.filter.state == State_Stopped)
                return This->renderer.filter.state == State_Paused ? S_OK : VFW_E_WRONG_STATE;
            if (ret != WAIT_TIMEOUT)
                ERR("%x\n", ret);
            continue;
        }
        tStart = -1;

        if (skip)
            FIXME("Sample dropped %u of %u bytes\n", skip, size);
        if (skip >= size)
            return S_OK;
        data += skip;
        size -= skip;

        hr = IDirectSoundBuffer_Lock(This->dsbuffer, writepos, min(free, size), (void**)&buf1, &size1, (void**)&buf2, &size2, 0);
        if (hr != DS_OK) {
            ERR("Unable to lock sound buffer! (%x)\n", hr);
            break;
        }
        memcpy(buf1, data, size1);
        if (size2)
            memcpy(buf2, data+size1, size2);
        IDirectSoundBuffer_Unlock(This->dsbuffer, buf1, size1, buf2, size2);
        This->writepos = (writepos + size1 + size2) % This->buf_size;
        TRACE("Wrote %u bytes at %u, next at %u - (%u/%u)\n", size1+size2, writepos, This->writepos, free, size);
        data += size1 + size2;
        size -= size1 + size2;
    }
    return S_OK;
}

static HRESULT WINAPI DSoundRender_ShouldDrawSampleNow(BaseRenderer *This, IMediaSample *pMediaSample, REFERENCE_TIME *pStartTime, REFERENCE_TIME *pEndTime)
{
    /* We time ourselves do not use the base renderers timing */
    return S_OK;
}


static HRESULT WINAPI DSoundRender_PrepareReceive(BaseRenderer *iface, IMediaSample *pSample)
{
    DSoundRenderImpl *This = impl_from_BaseRenderer(iface);
    HRESULT hr;
    AM_MEDIA_TYPE *amt;

    if (IMediaSample_GetMediaType(pSample, &amt) == S_OK)
    {
        AM_MEDIA_TYPE *orig = &This->renderer.sink.pin.mtCurrent;
        WAVEFORMATEX *origfmt = (WAVEFORMATEX *)orig->pbFormat;
        WAVEFORMATEX *newfmt = (WAVEFORMATEX *)amt->pbFormat;

        if (origfmt->wFormatTag == newfmt->wFormatTag &&
            origfmt->nChannels == newfmt->nChannels &&
            origfmt->nBlockAlign == newfmt->nBlockAlign &&
            origfmt->wBitsPerSample == newfmt->wBitsPerSample &&
            origfmt->cbSize ==  newfmt->cbSize)
        {
            if (origfmt->nSamplesPerSec != newfmt->nSamplesPerSec)
            {
                hr = IDirectSoundBuffer_SetFrequency(This->dsbuffer,
                                                     newfmt->nSamplesPerSec);
                if (FAILED(hr))
                    return VFW_E_TYPE_NOT_ACCEPTED;
                FreeMediaType(orig);
                CopyMediaType(orig, amt);
                IMediaSample_SetMediaType(pSample, NULL);
            }
        }
        else
            return VFW_E_TYPE_NOT_ACCEPTED;
    }
    return S_OK;
}

static HRESULT WINAPI DSoundRender_DoRenderSample(BaseRenderer *iface, IMediaSample * pSample)
{
    DSoundRenderImpl *This = impl_from_BaseRenderer(iface);
    LPBYTE pbSrcStream = NULL;
    LONG cbSrcStream = 0;
    REFERENCE_TIME tStart, tStop;
    HRESULT hr;

    TRACE("%p %p\n", iface, pSample);

    /* Slightly incorrect, Pause completes when a frame is received so we should signal
     * pause completion here, but for sound playing a single frame doesn't make sense
     */

    hr = IMediaSample_GetPointer(pSample, &pbSrcStream);
    if (FAILED(hr))
    {
        ERR("Cannot get pointer to sample data (%x)\n", hr);
        return hr;
    }

    hr = IMediaSample_GetTime(pSample, &tStart, &tStop);
    if (FAILED(hr)) {
        ERR("Cannot get sample time (%x)\n", hr);
        tStart = tStop = -1;
    }

    IMediaSample_IsDiscontinuity(pSample);

    if (IMediaSample_IsPreroll(pSample) == S_OK)
    {
        TRACE("Preroll!\n");
        return S_OK;
    }

    cbSrcStream = IMediaSample_GetActualDataLength(pSample);
    TRACE("Sample data ptr = %p, size = %d\n", pbSrcStream, cbSrcStream);

    return DSoundRender_SendSampleData(This, tStart, tStop, pbSrcStream, cbSrcStream);
}

static HRESULT WINAPI DSoundRender_CheckMediaType(BaseRenderer *iface, const AM_MEDIA_TYPE * pmt)
{
    WAVEFORMATEX* format;

    if (!IsEqualIID(&pmt->majortype, &MEDIATYPE_Audio))
        return S_FALSE;

    format =  (WAVEFORMATEX*)pmt->pbFormat;
    TRACE("Format = %p\n", format);
    TRACE("wFormatTag = %x %x\n", format->wFormatTag, WAVE_FORMAT_PCM);
    TRACE("nChannels = %d\n", format->nChannels);
    TRACE("nSamplesPerSec = %d\n", format->nSamplesPerSec);
    TRACE("nAvgBytesPerSec = %d\n", format->nAvgBytesPerSec);
    TRACE("nBlockAlign = %d\n", format->nBlockAlign);
    TRACE("wBitsPerSample = %d\n", format->wBitsPerSample);

    if (!IsEqualIID(&pmt->subtype, &MEDIASUBTYPE_PCM))
        return S_FALSE;

    return S_OK;
}

static void dsound_render_stop_stream(BaseRenderer *iface)
{
    DSoundRenderImpl *This = impl_from_BaseRenderer(iface);

    TRACE("(%p/%p)->()\n", This, iface);

    IDirectSoundBuffer_Stop(This->dsbuffer);
    This->writepos = This->buf_size;
}

static void dsound_render_start_stream(BaseRenderer *iface)
{
    DSoundRenderImpl *This = impl_from_BaseRenderer(iface);

    TRACE("(%p)\n", This);

    if (This->renderer.sink.pin.peer)
    {
        DWORD bytes;

        IDirectSoundBuffer_GetCurrentPosition(This->dsbuffer, &bytes, NULL);
        IDirectSoundBuffer_Play(This->dsbuffer, 0, 0, DSBPLAY_LOOPING);
        This->start_ticks = GetTickCount64() + time_from_pos(This, bytes) / 10000;
    }
}

static HRESULT WINAPI DSoundRender_CompleteConnect(BaseRenderer * iface, IPin * pReceivePin)
{
    DSoundRenderImpl *This = impl_from_BaseRenderer(iface);
    const AM_MEDIA_TYPE *pmt = &This->renderer.sink.pin.mtCurrent;
    HRESULT hr = S_OK;
    WAVEFORMATEX *format;
    DSBUFFERDESC buf_desc;

    TRACE("(%p)->(%p)\n", This, pReceivePin);
    dump_AM_MEDIA_TYPE(pmt);

    TRACE("MajorType %s\n", debugstr_guid(&pmt->majortype));
    TRACE("SubType %s\n", debugstr_guid(&pmt->subtype));
    TRACE("Format %s\n", debugstr_guid(&pmt->formattype));
    TRACE("Size %d\n", pmt->cbFormat);

    format = (WAVEFORMATEX*)pmt->pbFormat;

    This->buf_size = format->nAvgBytesPerSec;

    memset(&buf_desc,0,sizeof(DSBUFFERDESC));
    buf_desc.dwSize = sizeof(DSBUFFERDESC);
    buf_desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN |
                       DSBCAPS_CTRLFREQUENCY | DSBCAPS_GLOBALFOCUS |
                       DSBCAPS_GETCURRENTPOSITION2;
    buf_desc.dwBufferBytes = This->buf_size;
    buf_desc.lpwfxFormat = format;
    hr = IDirectSound8_CreateSoundBuffer(This->dsound, &buf_desc, &This->dsbuffer, NULL);
    This->writepos = This->buf_size;
    if (FAILED(hr))
        ERR("Can't create sound buffer (%x)\n", hr);

    if (SUCCEEDED(hr))
    {
        hr = IDirectSoundBuffer_SetVolume(This->dsbuffer, This->volume);
        if (FAILED(hr))
            ERR("Can't set volume to %d (%x)\n", This->volume, hr);

        hr = IDirectSoundBuffer_SetPan(This->dsbuffer, This->pan);
        if (FAILED(hr))
            ERR("Can't set pan to %d (%x)\n", This->pan, hr);
        hr = S_OK;
    }

    if (FAILED(hr) && hr != VFW_E_ALREADY_CONNECTED)
    {
        if (This->dsbuffer)
            IDirectSoundBuffer_Release(This->dsbuffer);
        This->dsbuffer = NULL;
    }

    return hr;
}

static HRESULT WINAPI DSoundRender_BreakConnect(BaseRenderer* iface)
{
    DSoundRenderImpl *This = impl_from_BaseRenderer(iface);

    TRACE("(%p)->()\n", iface);

    if (This->dsbuffer)
        IDirectSoundBuffer_Release(This->dsbuffer);
    This->dsbuffer = NULL;

    return S_OK;
}

static HRESULT WINAPI DSoundRender_EndOfStream(BaseRenderer* iface)
{
    DSoundRenderImpl *This = impl_from_BaseRenderer(iface);
    HRESULT hr;

    TRACE("(%p)->()\n",iface);

    hr = BaseRendererImpl_EndOfStream(iface);
    if (hr != S_OK)
    {
        ERR("%08x\n", hr);
        return hr;
    }

    hr = DSoundRender_HandleEndOfStream(This);

    return hr;
}

static HRESULT WINAPI DSoundRender_EndFlush(BaseRenderer* iface)
{
    DSoundRenderImpl *This = impl_from_BaseRenderer(iface);

    TRACE("\n");

    BaseRendererImpl_EndFlush(iface);

    if (This->dsbuffer)
    {
        LPBYTE buffer;
        DWORD size;

        /* Force a reset */
        IDirectSoundBuffer_Lock(This->dsbuffer, 0, 0, (LPVOID *)&buffer, &size, NULL, NULL, DSBLOCK_ENTIREBUFFER);
        memset(buffer, 0, size);
        IDirectSoundBuffer_Unlock(This->dsbuffer, buffer, size, NULL, 0);
        This->writepos = This->buf_size;
    }

    return S_OK;
}

static void dsound_render_destroy(BaseRenderer *iface)
{
    DSoundRenderImpl *filter = impl_from_BaseRenderer(iface);

    reference_clock_cleanup(&filter->clock);
    if (filter->dsbuffer)
        IDirectSoundBuffer_Release(filter->dsbuffer);
    filter->dsbuffer = NULL;
    if (filter->dsound)
        IDirectSound8_Release(filter->dsound);
    filter->dsound = NULL;

    strmbase_renderer_cleanup(&filter->renderer);
    CoTaskMemFree(filter);
}

static HRESULT dsound_render_query_interface(BaseRenderer *iface, REFIID iid, void **out)
{
    DSoundRenderImpl *filter = impl_from_BaseRenderer(iface);

    if (IsEqualGUID(iid, &IID_IBasicAudio))
        *out = &filter->IBasicAudio_iface;
    else if (IsEqualGUID(iid, &IID_IReferenceClock))
        *out = &filter->IReferenceClock_iface;
    else if (IsEqualGUID(iid, &IID_IAMDirectSound))
        *out = &filter->IAMDirectSound_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static const BaseRendererFuncTable BaseFuncTable =
{
    .pfnCheckMediaType = DSoundRender_CheckMediaType,
    .pfnDoRenderSample = DSoundRender_DoRenderSample,
    .renderer_start_stream = dsound_render_start_stream,
    .renderer_stop_stream = dsound_render_stop_stream,
    .pfnShouldDrawSampleNow = DSoundRender_ShouldDrawSampleNow,
    .pfnPrepareReceive = DSoundRender_PrepareReceive,
    .pfnCompleteConnect = DSoundRender_CompleteConnect,
    .pfnBreakConnect = DSoundRender_BreakConnect,
    .pfnEndOfStream = DSoundRender_EndOfStream,
    .pfnEndFlush = DSoundRender_EndFlush,
    .renderer_destroy = dsound_render_destroy,
    .renderer_query_interface = dsound_render_query_interface,
};

static const IBaseFilterVtbl DSoundRender_Vtbl =
{
    BaseFilterImpl_QueryInterface,
    BaseFilterImpl_AddRef,
    BaseFilterImpl_Release,
    BaseFilterImpl_GetClassID,
    BaseRendererImpl_Stop,
    BaseRendererImpl_Pause,
    BaseRendererImpl_Run,
    BaseRendererImpl_GetState,
    BaseRendererImpl_SetSyncSource,
    BaseFilterImpl_GetSyncSource,
    BaseFilterImpl_EnumPins,
    BaseFilterImpl_FindPin,
    BaseFilterImpl_QueryFilterInfo,
    BaseFilterImpl_JoinFilterGraph,
    BaseFilterImpl_QueryVendorInfo
};

/*** IUnknown methods ***/
static HRESULT WINAPI Basicaudio_QueryInterface(IBasicAudio *iface,
						REFIID riid,
						LPVOID*ppvObj) {
    DSoundRenderImpl *This = impl_from_IBasicAudio(iface);

    TRACE("(%p/%p)->(%s, %p)\n", This, iface, debugstr_guid(riid), ppvObj);

    return BaseFilterImpl_QueryInterface(&This->renderer.filter.IBaseFilter_iface, riid, ppvObj);
}

static ULONG WINAPI Basicaudio_AddRef(IBasicAudio *iface) {
    DSoundRenderImpl *This = impl_from_IBasicAudio(iface);

    TRACE("(%p/%p)->()\n", This, iface);

    return BaseFilterImpl_AddRef(&This->renderer.filter.IBaseFilter_iface);
}

static ULONG WINAPI Basicaudio_Release(IBasicAudio *iface) {
    DSoundRenderImpl *This = impl_from_IBasicAudio(iface);

    TRACE("(%p/%p)->()\n", This, iface);

    return BaseFilterImpl_Release(&This->renderer.filter.IBaseFilter_iface);
}

HRESULT WINAPI basic_audio_GetTypeInfoCount(IBasicAudio *iface, UINT *count)
{
    TRACE("iface %p, count %p.\n", iface, count);
    *count = 1;
    return S_OK;
}

HRESULT WINAPI basic_audio_GetTypeInfo(IBasicAudio *iface, UINT index,
        LCID lcid, ITypeInfo **typeinfo)
{
    TRACE("iface %p, index %u, lcid %#x, typeinfo %p.\n", iface, index, lcid, typeinfo);
    return strmbase_get_typeinfo(IBasicAudio_tid, typeinfo);
}

HRESULT WINAPI basic_audio_GetIDsOfNames(IBasicAudio *iface, REFIID iid,
        LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    ITypeInfo *typeinfo;
    HRESULT hr;

    TRACE("iface %p, iid %s, names %p, count %u, lcid %#x, ids %p.\n",
            iface, debugstr_guid(iid), names, count, lcid, ids);

    if (SUCCEEDED(hr = strmbase_get_typeinfo(IBasicAudio_tid, &typeinfo)))
    {
        hr = ITypeInfo_GetIDsOfNames(typeinfo, names, count, ids);
        ITypeInfo_Release(typeinfo);
    }
    return hr;
}

static HRESULT WINAPI basic_audio_Invoke(IBasicAudio *iface, DISPID id, REFIID iid, LCID lcid,
        WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *excepinfo, UINT *error_arg)
{
    ITypeInfo *typeinfo;
    HRESULT hr;

    TRACE("iface %p, id %d, iid %s, lcid %#x, flags %#x, params %p, result %p, excepinfo %p, error_arg %p.\n",
            iface, id, debugstr_guid(iid), lcid, flags, params, result, excepinfo, error_arg);

    if (SUCCEEDED(hr = strmbase_get_typeinfo(IBasicAudio_tid, &typeinfo)))
    {
        hr = ITypeInfo_Invoke(typeinfo, iface, id, flags, params, result, excepinfo, error_arg);
        ITypeInfo_Release(typeinfo);
    }
    return hr;
}

static HRESULT WINAPI Basicaudio_put_Volume(IBasicAudio *iface,
                                            LONG lVolume) {
    DSoundRenderImpl *This = impl_from_IBasicAudio(iface);

    TRACE("(%p/%p)->(%d)\n", This, iface, lVolume);

    if (lVolume > DSBVOLUME_MAX || lVolume < DSBVOLUME_MIN)
        return E_INVALIDARG;

    if (This->dsbuffer) {
        if (FAILED(IDirectSoundBuffer_SetVolume(This->dsbuffer, lVolume)))
            return E_FAIL;
    }

    This->volume = lVolume;
    return S_OK;
}

static HRESULT WINAPI Basicaudio_get_Volume(IBasicAudio *iface,
                                            LONG *plVolume) {
    DSoundRenderImpl *This = impl_from_IBasicAudio(iface);

    TRACE("(%p/%p)->(%p)\n", This, iface, plVolume);

    if (!plVolume)
        return E_POINTER;

    *plVolume = This->volume;
    return S_OK;
}

static HRESULT WINAPI Basicaudio_put_Balance(IBasicAudio *iface,
                                             LONG lBalance) {
    DSoundRenderImpl *This = impl_from_IBasicAudio(iface);

    TRACE("(%p/%p)->(%d)\n", This, iface, lBalance);

    if (lBalance < DSBPAN_LEFT || lBalance > DSBPAN_RIGHT)
        return E_INVALIDARG;

    if (This->dsbuffer) {
        if (FAILED(IDirectSoundBuffer_SetPan(This->dsbuffer, lBalance)))
            return E_FAIL;
    }

    This->pan = lBalance;
    return S_OK;
}

static HRESULT WINAPI Basicaudio_get_Balance(IBasicAudio *iface,
                                             LONG *plBalance) {
    DSoundRenderImpl *This = impl_from_IBasicAudio(iface);

    TRACE("(%p/%p)->(%p)\n", This, iface, plBalance);

    if (!plBalance)
        return E_POINTER;

    *plBalance = This->pan;
    return S_OK;
}

static const IBasicAudioVtbl IBasicAudio_Vtbl =
{
    Basicaudio_QueryInterface,
    Basicaudio_AddRef,
    Basicaudio_Release,
    basic_audio_GetTypeInfoCount,
    basic_audio_GetTypeInfo,
    basic_audio_GetIDsOfNames,
    basic_audio_Invoke,
    Basicaudio_put_Volume,
    Basicaudio_get_Volume,
    Basicaudio_put_Balance,
    Basicaudio_get_Balance
};

/*** IUnknown methods ***/
static HRESULT WINAPI ReferenceClock_QueryInterface(IReferenceClock *iface,
						REFIID riid,
						LPVOID*ppvObj)
{
    DSoundRenderImpl *This = impl_from_IReferenceClock(iface);

    TRACE("(%p/%p)->(%s, %p)\n", This, iface, debugstr_guid(riid), ppvObj);

    return BaseFilterImpl_QueryInterface(&This->renderer.filter.IBaseFilter_iface, riid, ppvObj);
}

static ULONG WINAPI ReferenceClock_AddRef(IReferenceClock *iface)
{
    DSoundRenderImpl *This = impl_from_IReferenceClock(iface);

    TRACE("(%p/%p)->()\n", This, iface);

    return BaseFilterImpl_AddRef(&This->renderer.filter.IBaseFilter_iface);
}

static ULONG WINAPI ReferenceClock_Release(IReferenceClock *iface)
{
    DSoundRenderImpl *This = impl_from_IReferenceClock(iface);

    TRACE("(%p/%p)->()\n", This, iface);

    return BaseFilterImpl_Release(&This->renderer.filter.IBaseFilter_iface);
}

static HRESULT WINAPI ReferenceClock_GetTime(IReferenceClock *iface, REFERENCE_TIME *time)
{
    DSoundRenderImpl *filter = impl_from_IReferenceClock(iface);

    TRACE("filter %p, time %p.\n", filter, time);

    return reference_clock_get_time(&filter->clock, time);
}

static HRESULT WINAPI ReferenceClock_AdviseTime(IReferenceClock *iface,
        REFERENCE_TIME base, REFERENCE_TIME offset, HEVENT event, DWORD_PTR *cookie)
{
    DSoundRenderImpl *filter = impl_from_IReferenceClock(iface);

    TRACE("filter %p, base %s, offset %s, event %#lx, cookie %p.\n",
            filter, wine_dbgstr_longlong(base), wine_dbgstr_longlong(offset), event, cookie);

    return reference_clock_advise(&filter->clock, base + offset, event, cookie);
}

static HRESULT WINAPI ReferenceClock_AdvisePeriodic(IReferenceClock *iface,
        REFERENCE_TIME start, REFERENCE_TIME period, HSEMAPHORE semaphore, DWORD_PTR *cookie)
{
    DSoundRenderImpl *filter = impl_from_IReferenceClock(iface);

    TRACE("filter %p, start %s, period %s, semaphore %#lx, cookie %p.\n",
            filter, wine_dbgstr_longlong(start), wine_dbgstr_longlong(period), semaphore, cookie);

    return reference_clock_advise_periodic(&filter->clock, start, period, semaphore, cookie);
}

static HRESULT WINAPI ReferenceClock_Unadvise(IReferenceClock *iface, DWORD_PTR cookie)
{
    DSoundRenderImpl *filter = impl_from_IReferenceClock(iface);

    TRACE("filter %p, cookie %#lx.\n", filter, cookie);

    return reference_clock_unadvise(&filter->clock, cookie);
}

static const IReferenceClockVtbl IReferenceClock_Vtbl =
{
    ReferenceClock_QueryInterface,
    ReferenceClock_AddRef,
    ReferenceClock_Release,
    ReferenceClock_GetTime,
    ReferenceClock_AdviseTime,
    ReferenceClock_AdvisePeriodic,
    ReferenceClock_Unadvise
};

static inline DSoundRenderImpl *impl_from_reference_clock(struct reference_clock *clock)
{
    return CONTAINING_RECORD(clock, DSoundRenderImpl, clock);
}

static REFERENCE_TIME dsound_render_clock_get_time(struct reference_clock *clock)
{
    DSoundRenderImpl *filter = impl_from_reference_clock(clock);
    REFERENCE_TIME ret, ret_ticks;
    ULONGLONG ticks, seconds;
    DWORD bytes;

    EnterCriticalSection(&filter->renderer.filter.csFilter);

    ticks = GetTickCount64();
    ret = ret_ticks = ticks * 10000;

    if (filter->renderer.filter.state == State_Running)
    {
        IDirectSoundBuffer_GetCurrentPosition(filter->dsbuffer, &bytes, NULL);
        seconds = ticks - ((ticks - filter->start_ticks) % 1000);
        ret = seconds * 10000 + time_from_pos(filter, bytes);

        /* The system clock and the DirectSound clock might disagree by several
         * milliseconds in either direction. If we are near the beginning or end
         * of the buffer, we will be 1 second off in either direction as a
         * result, so correct for this. */
        if (ret - ret_ticks > 5000000)
            ret -= 10000000;
        else if (ret - ret_ticks < -5000000)
            ret += 10000000;
    }

    LeaveCriticalSection(&filter->renderer.filter.csFilter);

    return ret;
}

static BOOL dsound_render_clock_wait_time(struct reference_clock *clock, REFERENCE_TIME time)
{
    HANDLE handles[2] = {clock->stop_event, clock->notify_event};
    DWORD timeout;

    /* FIXME: Use IDirectSoundNotify instead. */

    if (time == LLONG_MAX)
        timeout = INFINITE;
    else
        timeout = max(time / 10000 - GetTickCount64(), 0);

    return WaitForMultipleObjects(2, handles, FALSE, timeout);
}

static const struct reference_clock_ops dsound_render_clock_ops =
{
    dsound_render_clock_get_time,
    dsound_render_clock_wait_time,
};

/*** IUnknown methods ***/
static HRESULT WINAPI AMDirectSound_QueryInterface(IAMDirectSound *iface,
						REFIID riid,
						LPVOID*ppvObj)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    TRACE("(%p/%p)->(%s, %p)\n", This, iface, debugstr_guid(riid), ppvObj);

    return BaseFilterImpl_QueryInterface(&This->renderer.filter.IBaseFilter_iface, riid, ppvObj);
}

static ULONG WINAPI AMDirectSound_AddRef(IAMDirectSound *iface)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    TRACE("(%p/%p)->()\n", This, iface);

    return BaseFilterImpl_AddRef(&This->renderer.filter.IBaseFilter_iface);
}

static ULONG WINAPI AMDirectSound_Release(IAMDirectSound *iface)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    TRACE("(%p/%p)->()\n", This, iface);

    return BaseFilterImpl_Release(&This->renderer.filter.IBaseFilter_iface);
}

/*** IAMDirectSound methods ***/
static HRESULT WINAPI AMDirectSound_GetDirectSoundInterface(IAMDirectSound *iface,  IDirectSound **ds)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    FIXME("(%p/%p)->(%p): stub\n", This, iface, ds);

    return E_NOTIMPL;
}

static HRESULT WINAPI AMDirectSound_GetPrimaryBufferInterface(IAMDirectSound *iface, IDirectSoundBuffer **buf)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    FIXME("(%p/%p)->(%p): stub\n", This, iface, buf);

    return E_NOTIMPL;
}

static HRESULT WINAPI AMDirectSound_GetSecondaryBufferInterface(IAMDirectSound *iface, IDirectSoundBuffer **buf)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    FIXME("(%p/%p)->(%p): stub\n", This, iface, buf);

    return E_NOTIMPL;
}

static HRESULT WINAPI AMDirectSound_ReleaseDirectSoundInterface(IAMDirectSound *iface, IDirectSound *ds)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    FIXME("(%p/%p)->(%p): stub\n", This, iface, ds);

    return E_NOTIMPL;
}

static HRESULT WINAPI AMDirectSound_ReleasePrimaryBufferInterface(IAMDirectSound *iface, IDirectSoundBuffer *buf)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    FIXME("(%p/%p)->(%p): stub\n", This, iface, buf);

    return E_NOTIMPL;
}

static HRESULT WINAPI AMDirectSound_ReleaseSecondaryBufferInterface(IAMDirectSound *iface, IDirectSoundBuffer *buf)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    FIXME("(%p/%p)->(%p): stub\n", This, iface, buf);

    return E_NOTIMPL;
}

static HRESULT WINAPI AMDirectSound_SetFocusWindow(IAMDirectSound *iface, HWND hwnd, BOOL bgaudible)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    FIXME("(%p/%p)->(%p,%d): stub\n", This, iface, hwnd, bgaudible);

    return E_NOTIMPL;
}

static HRESULT WINAPI AMDirectSound_GetFocusWindow(IAMDirectSound *iface, HWND *hwnd, BOOL *bgaudible)
{
    DSoundRenderImpl *This = impl_from_IAMDirectSound(iface);

    FIXME("(%p/%p)->(%p,%p): stub\n", This, iface, hwnd, bgaudible);

    return E_NOTIMPL;
}

static const IAMDirectSoundVtbl IAMDirectSound_Vtbl =
{
    AMDirectSound_QueryInterface,
    AMDirectSound_AddRef,
    AMDirectSound_Release,
    AMDirectSound_GetDirectSoundInterface,
    AMDirectSound_GetPrimaryBufferInterface,
    AMDirectSound_GetSecondaryBufferInterface,
    AMDirectSound_ReleaseDirectSoundInterface,
    AMDirectSound_ReleasePrimaryBufferInterface,
    AMDirectSound_ReleaseSecondaryBufferInterface,
    AMDirectSound_SetFocusWindow,
    AMDirectSound_GetFocusWindow
};

HRESULT dsound_render_create(IUnknown *outer, void **out)
{
    static const WCHAR sink_name[] = {'A','u','d','i','o',' ','I','n','p','u','t',' ','p','i','n',' ','(','r','e','n','d','e','r','e','d',')',0};
    DSoundRenderImpl *object;
    HRESULT hr;

    *out = NULL;

    if (!(object = CoTaskMemAlloc(sizeof(DSoundRenderImpl))))
        return E_OUTOFMEMORY;
    memset(object, 0, sizeof(*object));

    hr = strmbase_renderer_init(&object->renderer, &DSoundRender_Vtbl,
            outer, &CLSID_DSoundRender, sink_name, &BaseFuncTable);

    object->IBasicAudio_iface.lpVtbl = &IBasicAudio_Vtbl;
    object->IReferenceClock_iface.lpVtbl = &IReferenceClock_Vtbl;
    object->IAMDirectSound_iface.lpVtbl = &IAMDirectSound_Vtbl;

    if (SUCCEEDED(hr))
    {
        hr = DirectSoundCreate8(NULL, &object->dsound, NULL);
        if (FAILED(hr))
            ERR("Cannot create Direct Sound object (%x)\n", hr);
        else
            hr = IDirectSound8_SetCooperativeLevel(object->dsound, GetDesktopWindow(), DSSCL_PRIORITY);
        if (SUCCEEDED(hr))
        {
            IDirectSoundBuffer *buffer;
            static const DSBUFFERDESC desc =
            {
                .dwSize = sizeof(DSBUFFERDESC),
                .dwFlags = DSBCAPS_PRIMARYBUFFER,
            };
            if (SUCCEEDED(IDirectSound8_CreateSoundBuffer(object->dsound, &desc, &buffer, NULL)))
            {
                IDirectSoundBuffer_Play(buffer, 0, 0, DSBPLAY_LOOPING);
                IDirectSoundBuffer_Release(buffer);
            }
            hr = S_OK;
        }
    }

    if (SUCCEEDED(hr))
    {
        reference_clock_init(&object->clock, &dsound_render_clock_ops);

        TRACE("Created DirectSound renderer %p.\n", object);
        *out = &object->renderer.filter.IUnknown_inner;
    }
    else
    {
        strmbase_renderer_cleanup(&object->renderer);
        CoTaskMemFree(object);
    }

    return hr;
}
