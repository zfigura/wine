/*
 * DMO wrapper filter
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
#include "strmif.h"
#include "dmodshow.h"
#include "wine/strmbase.h"
#include "wine/debug.h"
#include "wine/heap.h"

#include "qasf_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(qasf);

struct dmo_wrapper {
    BaseFilter filter;
    IDMOWrapperFilter IDMOWrapperFilter_iface;
};

static inline struct dmo_wrapper *impl_from_BaseFilter(BaseFilter *filter)
{
    return CONTAINING_RECORD(filter, struct dmo_wrapper, filter);
}

static inline struct dmo_wrapper *impl_from_IBaseFilter(IBaseFilter *iface)
{
    BaseFilter *filter = CONTAINING_RECORD(iface, BaseFilter, IBaseFilter_iface);
    return impl_from_BaseFilter(filter);
}

static HRESULT WINAPI BaseFilter_QueryInterface(IBaseFilter *iface, REFIID iid, void **obj)
{
    struct dmo_wrapper *This = impl_from_IBaseFilter(iface);

    TRACE("(%p/%p)->(%s %p)\n", This, iface, debugstr_guid(iid), obj);

    if (IsEqualIID(iid, &IID_IUnknown) ||
        IsEqualIID(iid, &IID_IPersist) ||
        IsEqualIID(iid, &IID_IMediaFilter) ||
        IsEqualIID(iid, &IID_IBaseFilter))
    {
        *obj = &This->filter.IBaseFilter_iface;
    }
    else if (IsEqualIID(iid, &IID_IDMOWrapperFilter))
    {
        *obj = &This->IDMOWrapperFilter_iface;
    }
    else
    {
        WARN("no interface for %s\n", debugstr_guid(iid));
        *obj = NULL;
        return E_NOINTERFACE;
    }

    IBaseFilter_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI BaseFilter_Release(IBaseFilter *iface)
{
    struct dmo_wrapper *This = impl_from_IBaseFilter(iface);
    ULONG refcount = BaseFilterImpl_Release(iface);

    TRACE("(%p/%p) Release from %d\n", This, iface, refcount + 1);

    if (!refcount)
    {
        heap_free(This);
    }
    return refcount;
}

static HRESULT WINAPI BaseFilter_Stop(IBaseFilter *iface)
{
    FIXME("(%p)->() stub!\n", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI BaseFilter_Pause(IBaseFilter *iface)
{
    FIXME("(%p)->() stub!\n", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI BaseFilter_Run(IBaseFilter *iface, REFERENCE_TIME start)
{
    FIXME("(%p)->(%s) stub!\n", iface, wine_dbgstr_longlong(start));

    return E_NOTIMPL;
}

static HRESULT WINAPI BaseFilter_FindPin(IBaseFilter *iface, const WCHAR *id, IPin **pin)
{
    FIXME("(%p)->(%s, %p) stub!\n", iface, debugstr_w(id), pin);

    return E_NOTIMPL;
}

static const IBaseFilterVtbl BaseFilter_vtbl = {
    BaseFilter_QueryInterface,
    BaseFilterImpl_AddRef,
    BaseFilter_Release,
    BaseFilterImpl_GetClassID,
    BaseFilter_Stop,
    BaseFilter_Pause,
    BaseFilter_Run,
    BaseFilterImpl_GetState,
    BaseFilterImpl_SetSyncSource,
    BaseFilterImpl_GetSyncSource,
    BaseFilterImpl_EnumPins,
    BaseFilter_FindPin,
    BaseFilterImpl_QueryFilterInfo,
    BaseFilterImpl_JoinFilterGraph,
    BaseFilterImpl_QueryVendorInfo,
};

static const BaseFilterFuncTable filter_func_table = {
};

static inline struct dmo_wrapper *impl_from_IDMOWrapperFilter(IDMOWrapperFilter *iface)
{
    return CONTAINING_RECORD(iface, struct dmo_wrapper, IDMOWrapperFilter_iface);
}

static HRESULT WINAPI DMOWrapperFilter_QueryInterface(IDMOWrapperFilter *iface, REFIID iid, void **obj)
{
    struct dmo_wrapper *This = impl_from_IDMOWrapperFilter(iface);
    return IBaseFilter_QueryInterface(&This->filter.IBaseFilter_iface, iid, obj);
}

static ULONG WINAPI DMOWrapperFilter_AddRef(IDMOWrapperFilter *iface)
{
    struct dmo_wrapper *This = impl_from_IDMOWrapperFilter(iface);
    return IBaseFilter_AddRef(&This->filter.IBaseFilter_iface);
}

static ULONG WINAPI DMOWrapperFilter_Release(IDMOWrapperFilter *iface)
{
    struct dmo_wrapper *This = impl_from_IDMOWrapperFilter(iface);
    return IBaseFilter_Release(&This->filter.IBaseFilter_iface);
}

static HRESULT WINAPI DMOWrapperFilter_Init(IDMOWrapperFilter *iface, REFCLSID clsid, REFCLSID cat)
{
    TRACE("(%p)->(%s, %s) stub!\n", iface, debugstr_guid(clsid), debugstr_guid(cat));

    return E_NOTIMPL;
}

static const IDMOWrapperFilterVtbl DMOWrapperFilter_vtbl = {
    DMOWrapperFilter_QueryInterface,
    DMOWrapperFilter_AddRef,
    DMOWrapperFilter_Release,
    DMOWrapperFilter_Init,
};

IUnknown * WINAPI create_DMOWrapperFilter(IUnknown *outer, HRESULT *hr)
{
    struct dmo_wrapper *This;

    TRACE("(%p)\n", outer);

    This = heap_alloc_zero(sizeof(*This));
    if (!This)
    {
        *hr = E_OUTOFMEMORY;
        return NULL;
    }

    BaseFilter_Init(&This->filter, &BaseFilter_vtbl, &CLSID_DMOWrapperFilter,
        (DWORD_PTR) (__FILE__ ": DMOWrapperFilter.csFilter"), &filter_func_table);

    This->IDMOWrapperFilter_iface.lpVtbl = &DMOWrapperFilter_vtbl;

    *hr = S_OK;
    return (IUnknown *)&This->filter.IBaseFilter_iface;
}
