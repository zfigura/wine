/*
 * Copyright (C) 2018 Zebediah Figura
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_DSDMO_PRIVATE_H
#define __WINE_DSDMO_PRIVATE_H

/* dsdmo.dll global (for DllCanUnloadNow) */
extern LONG module_ref DECLSPEC_HIDDEN;
static inline void lock_module(void) { InterlockedIncrement( &module_ref ); }
static inline void unlock_module(void) { InterlockedDecrement( &module_ref ); }

struct dmo_stream {
    DMO_MEDIA_TYPE *types;
    DWORD types_count;
    DMO_MEDIA_TYPE *current;
};

struct base_dmo {
    IMediaObject IMediaObject_iface;
    LONG refcount;

    struct dmo_stream *inputs;
    DWORD inputs_count;
    struct dmo_stream *outputs;
    DWORD outputs_count;

    CRITICAL_SECTION cs;
};

extern HRESULT create_I3DL2Reverb(REFIID riid, void **ppv);

extern ULONG WINAPI base_dmo_AddRef(IMediaObject *iface);
extern HRESULT WINAPI base_dmo_GetStreamCount(IMediaObject *iface, DWORD *input, DWORD *output);
extern HRESULT WINAPI base_dmo_GetInputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type);
extern HRESULT WINAPI base_dmo_GetOutputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type);
extern HRESULT WINAPI base_dmo_GetInputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type);
extern HRESULT WINAPI base_dmo_GetOutputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type);
extern HRESULT WINAPI base_dmo_Lock(IMediaObject *iface, LONG lock);

extern HRESULT init_base_dmo(struct base_dmo *This);
extern void destroy_base_dmo(struct base_dmo *This);

#endif	/* __WINE_DSDMO_PRIVATE_H */
