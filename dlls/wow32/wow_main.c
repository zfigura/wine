/*
 * Win32 Windows-on-Windows support
 *
 * Copyright 2005 Alexandre Julliard
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "wownt32.h"

#undef WOWHandle32
#undef WOWHandle16
#undef WOWCallback16
#undef WOWCallback16Ex

BOOL   WINAPI K32WOWGetDescriptor(DWORD,LPLDT_ENTRY);
LPVOID WINAPI K32WOWGetVDMPointer(DWORD,DWORD,BOOL);
LPVOID WINAPI K32WOWGetVDMPointerFix(DWORD,DWORD,BOOL);
void   WINAPI K32WOWGetVDMPointerUnfix(DWORD);
WORD   WINAPI K32WOWGlobalAlloc16(WORD,DWORD);
WORD   WINAPI K32WOWGlobalFree16(WORD);
DWORD  WINAPI K32WOWGlobalLock16(WORD);
BOOL   WINAPI K32WOWGlobalUnlock16(WORD);
DWORD  WINAPI K32WOWGlobalAllocLock16(WORD,DWORD,WORD *);
WORD   WINAPI K32WOWGlobalUnlockFree16(DWORD);
DWORD  WINAPI K32WOWGlobalLockSize16(WORD,PDWORD);
void   WINAPI K32WOWYield16(void);
void   WINAPI K32WOWDirectedYield16(WORD);

/**********************************************************************
 *           WOWGetDescriptor   (WOW32.1)
 */
BOOL WINAPI WOWGetDescriptor( DWORD segptr, LPLDT_ENTRY ldtent )
{
    return K32WOWGetDescriptor( segptr, ldtent );
}

/**********************************************************************
 *           WOWGetVDMPointer   (WOW32.@)
 */
LPVOID WINAPI WOWGetVDMPointer( DWORD vp, DWORD dwBytes, BOOL fProtectedMode )
{
    return K32WOWGetVDMPointer( vp, dwBytes, fProtectedMode );
}

/**********************************************************************
 *           WOWGetVDMPointerFix   (WOW32.@)
 */
LPVOID WINAPI WOWGetVDMPointerFix( DWORD vp, DWORD dwBytes, BOOL fProtectedMode )
{
    return K32WOWGetVDMPointerFix( vp, dwBytes, fProtectedMode );
}

/**********************************************************************
 *           WOWGetVDMPointerUnfix   (WOW32.@)
 */
void WINAPI WOWGetVDMPointerUnfix( DWORD vp )
{
    K32WOWGetVDMPointerUnfix( vp );
}

/**********************************************************************
 *           WOWGlobalAlloc16   (WOW32.@)
 */
WORD WINAPI WOWGlobalAlloc16( WORD wFlags, DWORD cb )
{
    return K32WOWGlobalAlloc16( wFlags, cb );
}

/**********************************************************************
 *           WOWGlobalFree16   (WOW32.@)
 */
WORD WINAPI WOWGlobalFree16( WORD hMem )
{
    return K32WOWGlobalFree16( hMem );
}

/**********************************************************************
 *           WOWGlobalLock16   (WOW32.@)
 */
DWORD WINAPI WOWGlobalLock16( WORD hMem )
{
    return K32WOWGlobalLock16( hMem );
}

/**********************************************************************
 *           WOWGlobalUnlock16   (WOW32.@)
 */
BOOL WINAPI WOWGlobalUnlock16( WORD hMem )
{
    return K32WOWGlobalUnlock16( hMem );
}

/**********************************************************************
 *           WOWGlobalAllocLock16   (WOW32.@)
 */
DWORD WINAPI WOWGlobalAllocLock16( WORD wFlags, DWORD cb, WORD *phMem )
{
    return K32WOWGlobalAllocLock16( wFlags, cb, phMem );
}

/**********************************************************************
 *           WOWGlobalLockSize16   (WOW32.@)
 */
DWORD WINAPI WOWGlobalLockSize16( WORD hMem, PDWORD pcb )
{
    return K32WOWGlobalLockSize16( hMem, pcb );
}

/**********************************************************************
 *           WOWGlobalUnlockFree16   (WOW32.@)
 */
WORD WINAPI WOWGlobalUnlockFree16( DWORD vpMem )
{
    return K32WOWGlobalUnlockFree16( vpMem );
}

/**********************************************************************
 *           WOWYield16   (WOW32.@)
 */
void WINAPI WOWYield16(void)
{
    K32WOWYield16();
}

/**********************************************************************
 *           WOWDirectedYield16   (WOW32.@)
 */
void WINAPI WOWDirectedYield16( WORD htask16 )
{
    K32WOWDirectedYield16( htask16 );
}

/***********************************************************************
 *           WOWHandle32   (WOW32.@)
 */
HANDLE WINAPI WOWHandle32( WORD handle, WOW_HANDLE_TYPE type )
{
    return K32WOWHandle32( handle, type );
}

/***********************************************************************
 *           WOWHandle16   (WOW32.@)
 */
WORD WINAPI WOWHandle16( HANDLE handle, WOW_HANDLE_TYPE type )
{
    return K32WOWHandle16( handle, type );
}

/**********************************************************************
 *           WOWCallback16Ex   (WOW32.@)
 */
BOOL WINAPI WOWCallback16Ex( DWORD vpfn16, DWORD dwFlags,
                             DWORD cbArgs, LPVOID pArgs, LPDWORD pdwRetCode )
{
    return K32WOWCallback16Ex( vpfn16, dwFlags, cbArgs, pArgs, pdwRetCode );
}

/**********************************************************************
 *           WOWCallback16   (WOW32.@)
 */
DWORD WINAPI WOWCallback16( DWORD vpfn16, DWORD dwParam )
{
    return K32WOWCallback16( vpfn16, dwParam );
}
