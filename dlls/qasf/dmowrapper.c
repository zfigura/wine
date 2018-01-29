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
#include "qasf_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(qasf);

struct dmo_wrapper {
    BaseFilter filter;
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

static HRESULT WINAPI BaseFilter_QueryInterface(IBaseFilter *iface, REFIID riid, void **ppv)
{
    struct dmo_wrapper *This = impl_from_IBaseFilter(iface);

    TRACE("(%p/%p)->(%s %p)\n", This, iface, debugstr_guid(riid), ppv);

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IPersist) ||
        IsEqualIID(riid, &IID_IMediaFilter) ||
        IsEqualIID(riid, &IID_IBaseFilter))
    {
        *ppv = &This->filter.IBaseFilter_iface;
    }
    else
    {
        WARN("no interface for %s\n", debugstr_guid(riid));
        *ppv = NULL;
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
        HeapFree(GetProcessHeap(), 0, This);
    }
    return refcount;
}

static const IBaseFilterVtbl BaseFilter_vtbl = {
    BaseFilter_QueryInterface,
    BaseFilterImpl_AddRef,
    BaseFilter_Release,
};

static const BaseFilterFuncTable filter_func_table = {
};

IUnknown * WINAPI create_DMOWrapperFilter(IUnknown *outer, HRESULT *hr)
{
    struct dmo_wrapper *This;

    TRACE("(%p)\n", outer);

    /* FIXME: check aggregation */

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if (!This)
    {
        *hr = E_OUTOFMEMORY;
        return NULL;
    }

    BaseFilter_Init(&This->filter, &BaseFilter_vtbl, &CLSID_DMOWrapperFilter,
        (DWORD_PTR) (__FILE__ ": DMOWrapperFilter.csFilter"), &filter_func_table);

    *hr = S_OK;
    return (IUnknown *)&This->filter.IBaseFilter_iface;
}
