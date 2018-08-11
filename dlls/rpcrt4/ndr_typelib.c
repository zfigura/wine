/*
 * Type library proxy/stub implementation
 *
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
#include "oaidl.h"
#define USE_STUBLESS_PROXY
#include "rpcproxy.h"
#include "wine/debug.h"
#include "wine/heap.h"

#include "cpsf.h"

WINE_DEFAULT_DEBUG_CHANNEL(ole);

static HRESULT build_format_strings(ITypeInfo *typeinfo, WORD funcs,
    const unsigned char **type_ret,
    const unsigned char **proc_ret, unsigned short **offset_ret)
{
    return E_NOTIMPL;
}

/* Common helper for Create{Proxy,Stub}FromTypeInfo(). */
static HRESULT get_iface_info(ITypeInfo *typeinfo, WORD *funcs, WORD *parentfuncs)
{
    TYPEATTR *typeattr;
    ITypeLib *typelib;
    TLIBATTR *libattr;
    SYSKIND syskind;
    HRESULT hr;

    hr = ITypeInfo_GetContainingTypeLib(typeinfo, &typelib, NULL);
    if (FAILED(hr))
        return hr;

    hr = ITypeLib_GetLibAttr(typelib, &libattr);
    if (FAILED(hr))
    {
        ITypeLib_Release(typelib);
        return hr;
    }
    syskind = libattr->syskind;
    ITypeLib_ReleaseTLibAttr(typelib, libattr);
    ITypeLib_Release(typelib);

    hr = ITypeInfo_GetTypeAttr(typeinfo, &typeattr);
    if (FAILED(hr))
        return hr;
    *funcs = typeattr->cFuncs;
    *parentfuncs = typeattr->cbSizeVft / (syskind == SYS_WIN64 ? 8 : 4) - *funcs;
    ITypeInfo_ReleaseTypeAttr(typeinfo, typeattr);

    return S_OK;
}

static void init_stub_desc(MIDL_STUB_DESC *desc)
{
    desc->pfnAllocate = NdrOleAllocate;
    desc->pfnFree = NdrOleFree;
    desc->Version = 0x50002;
    /* type format string is initialized with proc format string and offset table */
}

struct typelib_proxy
{
    StdProxyImpl proxy;
    IID iid;
    MIDL_STUB_DESC stub_desc;
    MIDL_STUBLESS_PROXY_INFO proxy_info;
    CInterfaceProxyVtbl *proxy_vtbl;
    unsigned short *offset_table;
};

static ULONG WINAPI typelib_proxy_Release(IRpcProxyBuffer *iface)
{
    struct typelib_proxy *This = CONTAINING_RECORD(iface, struct typelib_proxy, proxy.IRpcProxyBuffer_iface);
    ULONG refcount = InterlockedDecrement(&This->proxy.RefCount);

    TRACE("(%p) decreasing refs to %d\n", This, refcount);

    if (!refcount)
    {
        if (This->proxy.pChannel)
            IRpcProxyBuffer_Disconnect(&This->proxy.IRpcProxyBuffer_iface);
        heap_free((void *)This->stub_desc.pFormatTypes);
        heap_free((void *)This->proxy_info.ProcFormatString);
        heap_free(This->offset_table);
        heap_free(This->proxy_vtbl);
        heap_free(This);
    }
    return refcount;
}

static const IRpcProxyBufferVtbl typelib_proxy_vtbl =
{
    StdProxy_QueryInterface,
    StdProxy_AddRef,
    typelib_proxy_Release,
    StdProxy_Connect,
    StdProxy_Disconnect,
};

static HRESULT typelib_proxy_init(struct typelib_proxy *This, IUnknown *outer,
    ULONG count, IRpcProxyBuffer **proxy, void **obj)
{
    if (!fill_stubless_table((IUnknownVtbl *)This->proxy_vtbl->Vtbl, count))
        return E_OUTOFMEMORY;

    if (!outer) outer = (IUnknown *)&This->proxy;

    This->proxy.IRpcProxyBuffer_iface.lpVtbl = &typelib_proxy_vtbl;
    This->proxy.PVtbl = This->proxy_vtbl->Vtbl;
    This->proxy.RefCount = 1;
    This->proxy.piid = This->proxy_vtbl->header.piid;
    This->proxy.pUnkOuter = outer;

    *proxy = &This->proxy.IRpcProxyBuffer_iface;
    *obj = &This->proxy.PVtbl;
    IUnknown_AddRef((IUnknown *)*obj);

    return S_OK;
}

HRESULT WINAPI CreateProxyFromTypeInfo(ITypeInfo *typeinfo, IUnknown *outer,
    REFIID iid, IRpcProxyBuffer **proxy, void **obj)
{
    struct typelib_proxy *This;
    WORD funcs, parentfuncs, i;
    HRESULT hr;

    TRACE("typeinfo %p, outer %p, iid %s, proxy %p, obj %p.\n",
        typeinfo, outer, debugstr_guid(iid), proxy, obj);

    hr = get_iface_info(typeinfo, &funcs, &parentfuncs);
    if (FAILED(hr))
        return hr;

    if (!(This = heap_alloc_zero(sizeof(*This))))
    {
        ERR("Failed to allocate proxy object.\n");
        return E_OUTOFMEMORY;
    }

    init_stub_desc(&This->stub_desc);
    This->proxy_info.pStubDesc = &This->stub_desc;

    This->proxy_vtbl = heap_alloc_zero(sizeof(This->proxy_vtbl->header) + (funcs + parentfuncs) * sizeof(void *));
    if (!This->proxy_vtbl)
    {
        ERR("Failed to allocate proxy vtbl.\n");
        heap_free(This);
        return E_OUTOFMEMORY;
    }
    This->proxy_vtbl->header.pStublessProxyInfo = &This->proxy_info;
    This->iid = *iid;
    This->proxy_vtbl->header.piid = &This->iid;
    for (i = 0; i < funcs; i++)
        This->proxy_vtbl->Vtbl[3 + i] = (void *)-1;

    hr = build_format_strings(typeinfo, funcs, &This->stub_desc.pFormatTypes,
        &This->proxy_info.ProcFormatString, &This->offset_table);
    if (FAILED(hr))
    {
        heap_free(This->proxy_vtbl);
        heap_free(This);
        return hr;
    }
    This->proxy_info.FormatStringOffset = &This->offset_table[-3];

    hr = typelib_proxy_init(This, outer, funcs + parentfuncs, proxy, obj);
    if (FAILED(hr))
    {
        heap_free((void *)This->stub_desc.pFormatTypes);
        heap_free((void *)This->proxy_info.ProcFormatString);
        heap_free((void *)This->offset_table);
        heap_free(This->proxy_vtbl);
        heap_free(This);
    }

    return hr;
}

struct typelib_stub
{
    CStdStubBuffer stub;
    IID iid;
    MIDL_STUB_DESC stub_desc;
    MIDL_SERVER_INFO server_info;
    CInterfaceStubVtbl stub_vtbl;
    unsigned short *offset_table;
};

static ULONG WINAPI typelib_stub_Release(IRpcStubBuffer *iface)
{
    struct typelib_stub *This = CONTAINING_RECORD(iface, struct typelib_stub, stub);
    ULONG refcount = InterlockedDecrement(&This->stub.RefCount);

    TRACE("(%p) decreasing refs to %d\n", This, refcount);

    if (!refcount)
    {
        /* test_Release shows that native doesn't call Disconnect here.
           We'll leave it in for the time being. */
        IRpcStubBuffer_Disconnect(iface);

        heap_free((void *)This->stub_desc.pFormatTypes);
        heap_free((void *)This->server_info.ProcString);
        heap_free(This->offset_table);
        heap_free(This);
    }

    return refcount;
}

static HRESULT typelib_stub_init(struct typelib_stub *This,
    IUnknown *server, IRpcStubBuffer **stub)
{
    HRESULT hr;

    hr = IUnknown_QueryInterface(server, This->stub_vtbl.header.piid,
        (void **)&This->stub.pvServerObject);
    if (FAILED(hr))
    {
        WARN("Failed to get interface %s, hr %#x.\n",
            debugstr_guid(This->stub_vtbl.header.piid), hr);
        This->stub.pvServerObject = server;
        IUnknown_AddRef(server);
    }

    This->stub.lpVtbl = &This->stub_vtbl.Vtbl;
    This->stub.RefCount = 1;

    *stub = (IRpcStubBuffer *)&This->stub;
    return S_OK;
}

HRESULT WINAPI CreateStubFromTypeInfo(ITypeInfo *typeinfo, REFIID iid,
    IUnknown *server, IRpcStubBuffer **stub)
{
    struct typelib_stub *This;
    WORD funcs, parentfuncs;
    HRESULT hr;

    TRACE("typeinfo %p, iid %s, server %p, stub %p.\n",
        typeinfo, debugstr_guid(iid), server, stub);

    hr = get_iface_info(typeinfo, &funcs, &parentfuncs);
    if (FAILED(hr))
        return hr;

    if (!(This = heap_alloc_zero(sizeof(*This))))
    {
        ERR("Failed to allocate stub object.\n");
        return E_OUTOFMEMORY;
    }

    init_stub_desc(&This->stub_desc);
    This->server_info.pStubDesc = &This->stub_desc;

    hr = build_format_strings(typeinfo, funcs, &This->stub_desc.pFormatTypes,
        &This->server_info.ProcString, &This->offset_table);
    if (FAILED(hr))
    {
        heap_free(This);
        return hr;
    }
    This->server_info.FmtStringOffset = &This->offset_table[-3];

    This->iid = *iid;
    This->stub_vtbl.header.piid = &This->iid;
    This->stub_vtbl.header.pServerInfo = &This->server_info;
    This->stub_vtbl.header.DispatchTableCount = funcs + parentfuncs;
    This->stub_vtbl.Vtbl = CStdStubBuffer_Vtbl;
    This->stub_vtbl.Vtbl.Release = typelib_stub_Release;

    hr = typelib_stub_init(This, server, stub);
    if (FAILED(hr))
    {
        heap_free((void *)This->stub_desc.pFormatTypes);
        heap_free((void *)This->server_info.ProcString);
        heap_free(This->offset_table);
        heap_free(This);
    }

    return hr;
}
