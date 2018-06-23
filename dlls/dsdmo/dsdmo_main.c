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
#include "mediaobj.h"
#include "rpcproxy.h"
#include "wine/debug.h"

#include "dsdmo_classes.h"
#include "dsdmo_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dsdmo);

static HINSTANCE dsdmo_instance;

struct class_factory
{
    IClassFactory IClassFactory_iface;
    const CLSID *clsid;
};

static inline struct class_factory *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, struct class_factory, IClassFactory_iface);
}

static HRESULT WINAPI ClassFactory_QueryInterface(IClassFactory *iface, REFIID iid, void **obj)
{
    TRACE("(%p, %s, %p)\n", iface, debugstr_guid(iid), obj);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory))
    {
        IClassFactory_AddRef(iface);
        *obj = iface;
        return S_OK;
    }

    *obj = NULL;
    WARN("no interface for %s\n", debugstr_guid(iid));
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

static HRESULT WINAPI ClassFactory_CreateInstance(IClassFactory *iface,
    IUnknown *outer, REFIID iid, void **obj)
{
    struct class_factory *This = impl_from_IClassFactory(iface);

    TRACE("(%p, %s, %p)\n", outer, debugstr_guid(iid), obj);

    return create_effect(This->clsid, outer, iid, obj);
}

static HRESULT WINAPI ClassFactory_LockServer(IClassFactory *iface, BOOL lock)
{
    FIXME("(%d) stub\n", lock);
    return S_OK;
}

static const IClassFactoryVtbl ClassFactory_vtbl = {
    ClassFactory_QueryInterface,
    ClassFactory_AddRef,
    ClassFactory_Release,
    ClassFactory_CreateInstance,
    ClassFactory_LockServer
};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    TRACE("(%p, %d, %p)\n", instance, reason, reserved);
    switch (reason)
    {
    case DLL_WINE_PREATTACH:
        return FALSE;   /* prefer native version */
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(instance);
        dsdmo_instance = instance;
        break;
    }
    return TRUE;
}

static struct class_factory effects[] =
{
    { { &ClassFactory_vtbl }, &CLSID_DirectSoundChorusDMO       },
    { { &ClassFactory_vtbl }, &CLSID_DirectSoundCompressorDMO   },
    { { &ClassFactory_vtbl }, &CLSID_DirectSoundDistortionDMO   },
    { { &ClassFactory_vtbl }, &CLSID_DirectSoundEchoDMO         },
    { { &ClassFactory_vtbl }, &CLSID_DirectSoundFlangerDMO      },
    { { &ClassFactory_vtbl }, &CLSID_DirectSoundGargleDMO       },
    { { &ClassFactory_vtbl }, &CLSID_DirectSoundI3DL2ReverbDMO  },
    { { &ClassFactory_vtbl }, &CLSID_DirectSoundParamEqDMO      },
    { { &ClassFactory_vtbl }, &CLSID_DirectSoundWavesReverbDMO  },
};

/*************************************************************************
 *              DllGetClassObject (DSDMO.@)
 */
HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **obj)
{
    int i;

    TRACE("(%s, %s, %p)\n", debugstr_guid(clsid), debugstr_guid(iid), obj);

    for (i = 0; i < ARRAY_SIZE(effects); i++)
    {
        if (IsEqualGUID(clsid, effects[i].clsid))
            return IClassFactory_QueryInterface(&effects[i].IClassFactory_iface, iid, obj);
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}

/******************************************************************
 *              DllCanUnloadNow (DSDMO.@)
 */
HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
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
