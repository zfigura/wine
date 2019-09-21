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
#include "wine/debug.h"
#include "wine/heap.h"

WINE_DEFAULT_DEBUG_CHANNEL(resampledmo);

static HINSTANCE resampledmo_instance;

struct resampler
{
    IUnknown IUnknown_inner;
    IUnknown *outer_unk;
    LONG refcount;
};

static inline struct resampler *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct resampler, IUnknown_inner);
}

static HRESULT WINAPI inner_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown))
        *out = iface;
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

static HRESULT resampler_create(IUnknown *outer, REFIID iid, void **out)
{
    struct resampler *object;
    HRESULT hr;

    if (!(object = heap_alloc_zero(sizeof(*object))))
        return E_OUTOFMEMORY;

    object->IUnknown_inner.lpVtbl = &inner_vtbl;
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
