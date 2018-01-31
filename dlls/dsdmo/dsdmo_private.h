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

#endif	/* __WINE_DSDMO_PRIVATE_H */
