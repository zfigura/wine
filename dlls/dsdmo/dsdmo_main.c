/*
 * DirectSound DirectX Media Objects
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
#include "mmsystem.h"
#include "dsound.h"
#include "dmo.h"
#include "rpcproxy.h"
#include "wine/debug.h"
#include "wine/heap.h"

#include "dsdmo_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dsdmo);

static inline struct base_dmo *impl_from_IMediaObject(IMediaObject *iface)
{
    return CONTAINING_RECORD(iface, struct base_dmo, IMediaObject_iface);
}

ULONG WINAPI base_dmo_AddRef(IMediaObject *iface)
{
    struct base_dmo *This = impl_from_IMediaObject(iface);
    ULONG refcount = InterlockedIncrement(&This->refcount);

    TRACE("(%p) AddRef from %d\n", This, refcount + 1);

    return refcount;
}

HRESULT WINAPI base_dmo_GetStreamCount(IMediaObject *iface, DWORD *input, DWORD *output)
{
    struct base_dmo *This = impl_from_IMediaObject(iface);

    TRACE("(%p)->(%p %p)\n", This, input, output);

    if (!input || !output)
        return E_POINTER;

    *input = This->inputs_count;
    *output = This->outputs_count;

    return S_OK;
}

HRESULT WINAPI base_dmo_GetInputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type)
{
    struct base_dmo *This = impl_from_IMediaObject(iface);
    struct dmo_stream *stream;

    TRACE("(%p)->(%d %d %p)\n", This, index, type_index, type);

    if (index >= This->inputs_count)
        return DMO_E_INVALIDSTREAMINDEX;

    stream = &This->inputs[index];

    if (type_index >= stream->types_count)
        return DMO_E_NO_MORE_ITEMS;

    if (type)
        return MoCopyMediaType(type, &stream->types[type_index]);

    return S_OK;
}

HRESULT WINAPI base_dmo_GetOutputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type)
{
    struct base_dmo *This = impl_from_IMediaObject(iface);
    struct dmo_stream *stream;

    TRACE("(%p)->(%d %d %p)\n", This, index, type_index, type);

    if (index >= This->outputs_count)
        return DMO_E_INVALIDSTREAMINDEX;

    stream = &This->outputs[index];

    if (type_index >= stream->types_count)
        return DMO_E_NO_MORE_ITEMS;

    if (type)
        return MoCopyMediaType(type, &stream->types[type_index]);

    return S_OK;
}

HRESULT WINAPI base_dmo_GetInputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type)
{
    struct base_dmo *This = impl_from_IMediaObject(iface);
    struct dmo_stream *stream;

    TRACE("(%p)->(%d %p)\n", This, index, type);

    if (index >= This->inputs_count)
        return DMO_E_INVALIDSTREAMINDEX;

    stream = &This->inputs[index];

    if (!stream->current)
        return DMO_E_TYPE_NOT_SET;

    return MoCopyMediaType(type, stream->current);
}

HRESULT WINAPI base_dmo_GetOutputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type)
{
    struct base_dmo *This = impl_from_IMediaObject(iface);
    struct dmo_stream *stream;

    TRACE("(%p)->(%d %p)\n", This, index, type);

    if (index >= This->outputs_count)
        return DMO_E_INVALIDSTREAMINDEX;

    stream = &This->outputs[index];

    if (!stream->current)
        return DMO_E_TYPE_NOT_SET;

    return MoCopyMediaType(type, stream->current);
}

HRESULT WINAPI base_dmo_Lock(IMediaObject *iface, LONG lock)
{
    struct base_dmo *This = impl_from_IMediaObject(iface);

    TRACE("(%p)->(%d)\n", This, lock);

    if (lock)
        EnterCriticalSection(&This->cs);
    else
        LeaveCriticalSection(&This->cs);

    return S_OK;
}

HRESULT init_base_dmo(struct base_dmo *This)
{
    This->refcount = 0;

    This->inputs = heap_alloc_zero(This->inputs_count * sizeof(This->inputs[0]));
    This->outputs = heap_alloc_zero(This->outputs_count * sizeof(This->outputs[0]));

    InitializeCriticalSection(&This->cs);

    return S_OK;
}

void destroy_dmo_stream(struct dmo_stream *stream)
{
    MoDeleteMediaType(stream->current);
    heap_free(stream->types);
}

void destroy_base_dmo(struct base_dmo *This)
{
    int i;

    for (i = 0; i < This->inputs_count; i++)
        destroy_dmo_stream(&This->inputs[i]);
    heap_free(This->inputs);

    for (i = 0; i < This->outputs_count; i++)
        destroy_dmo_stream(&This->outputs[i]);
    heap_free(This->outputs);

    DeleteCriticalSection(&This->cs);
}

static HINSTANCE dsdmo_instance;
LONG module_ref = 0;

typedef struct {
    IClassFactory IClassFactory_iface;
    HRESULT (*fnCreateInstance)(REFIID riid, void **ppv);
} IClassFactoryImpl;

/******************************************************************
 *      IClassFactory implementation
 */
static inline IClassFactoryImpl *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, IClassFactoryImpl, IClassFactory_iface);
}

static HRESULT WINAPI ClassFactory_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    if (ppv == NULL)
        return E_POINTER;

    if (IsEqualGUID(&IID_IUnknown, riid))
        TRACE("(%p)->(IID_IUnknown %p)\n", iface, ppv);
    else if (IsEqualGUID(&IID_IClassFactory, riid))
        TRACE("(%p)->(IID_IClassFactory %p)\n", iface, ppv);
    else {
        FIXME("no interface for %s\n", debugstr_guid(riid));
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    *ppv = iface;
    IClassFactory_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI ClassFactory_AddRef(IClassFactory *iface)
{
    lock_module();

    return 2; /* non-heap based object */
}

static ULONG WINAPI ClassFactory_Release(IClassFactory *iface)
{
    unlock_module();

    return 1; /* non-heap based object */
}

static HRESULT WINAPI ClassFactory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    IClassFactoryImpl *This = impl_from_IClassFactory(iface);

    TRACE("(%s, %p)\n", debugstr_guid(riid), ppv);

    if (outer) {
        *ppv = NULL;
        return CLASS_E_NOAGGREGATION;
    }

    return This->fnCreateInstance(riid, ppv);
}

static HRESULT WINAPI ClassFactory_LockServer(IClassFactory *iface, BOOL dolock)
{
    TRACE("(%d)\n", dolock);

    if (dolock)
        lock_module();
    else
        unlock_module();

    return S_OK;
}

static const IClassFactoryVtbl classfactory_vtbl = {
    ClassFactory_QueryInterface,
    ClassFactory_AddRef,
    ClassFactory_Release,
    ClassFactory_CreateInstance,
    ClassFactory_LockServer
};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    TRACE("%p, %d, %p\n", instance, reason, reserved);
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(instance);
        dsdmo_instance = instance;
        break;
    }
    return TRUE;
}

/*************************************************************************
 *              DllGetClassObject (DSDMO.@)
 */
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    TRACE("%s, %s, %p\n", debugstr_guid(rclsid), debugstr_guid(riid), ppv);

    return CLASS_E_CLASSNOTAVAILABLE;
}

/******************************************************************
 *              DllCanUnloadNow (DSDMO.@)
 */
HRESULT WINAPI DllCanUnloadNow(void)
{
    TRACE("() ref=%d\n", module_ref);

    return module_ref ? S_FALSE : S_OK;
}

/***********************************************************************
 *              DllRegisterServer (DSDMO.@)
 */
HRESULT WINAPI DllRegisterServer(void)
{
    return __wine_register_resources( dsdmo_instance );
}

/***********************************************************************
 *              DllUnregisterServer (DSDMO.@)
 */
HRESULT WINAPI DllUnregisterServer(void)
{
    return __wine_unregister_resources( dsdmo_instance );
}
