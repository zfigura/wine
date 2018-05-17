/*
 * Copyright (C) 2007 Alexandre Julliard
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

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>

#define NONAMELESSUNION
#define NONAMELESSSTRUCT

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "ddk/ntddk.h"
#include "ddk/wdm.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ntoskrnl);

/***********************************************************************
 *           KeInitializeEvent   (NTOSKRNL.EXE.@)
 */
void WINAPI KeInitializeEvent( PRKEVENT event, EVENT_TYPE type, BOOLEAN state )
{
    TRACE("(%p, %#x, %d\n", event, type, state);

    event->Header.Type = type;
    event->Header.Size = sizeof(KEVENT) / sizeof(DWORD);
    event->Header.SignalState = state;
    InitializeListHead(&event->Header.WaitListHead);
}

/***********************************************************************
 *           KeClearEvent (NTOSKRNL.EXE.@)
 */
VOID WINAPI KeClearEvent(PRKEVENT event)
{
    TRACE("(%p)\n", event);

    event->Header.SignalState = FALSE;
}

/***********************************************************************
 *           KeResetEvent   (NTOSKRNL.EXE.@)
 */
LONG WINAPI KeResetEvent( PRKEVENT event )
{
    FIXME("(%p): stub\n", event);
    return 0;
}

/***********************************************************************
 *           KeSetEvent   (NTOSKRNL.EXE.@)
 */
LONG WINAPI KeSetEvent( PRKEVENT event, KPRIORITY increment, BOOLEAN wait )
{
    LONG old = event->Header.SignalState;

    FIXME("(%p, %d, %d): stub\n", event, increment, wait);

    event->Header.SignalState = TRUE;
    return old;
}

/***********************************************************************
 *           KeInitializeMutex   (NTOSKRNL.EXE.@)
 */
void WINAPI KeInitializeMutex(PRKMUTEX mutex, ULONG level)
{
    FIXME( "stub: %p, %u\n", mutex, level );
}

/***********************************************************************
 *           KeWaitForMutexObject   (NTOSKRNL.EXE.@)
 */
NTSTATUS WINAPI KeWaitForMutexObject(PRKMUTEX mutex, KWAIT_REASON reason, KPROCESSOR_MODE mode,
                                     BOOLEAN alertable, LARGE_INTEGER *timeout)
{
    FIXME( "stub: %p, %d, %d, %d, %p\n", mutex, reason, mode, alertable, timeout );
    return STATUS_NOT_IMPLEMENTED;
}

/***********************************************************************
 *           KeReleaseMutex   (NTOSKRNL.EXE.@)
 */
LONG WINAPI KeReleaseMutex(PRKMUTEX mutex, BOOLEAN wait)
{
    FIXME( "stub: %p, %d\n", mutex, wait );
    return STATUS_NOT_IMPLEMENTED;
}

/***********************************************************************
 *           ExAcquireFastMutexUnsafe  (NTOSKRNL.EXE.@)
 */
#ifdef DEFINE_FASTCALL1_ENTRYPOINT
DEFINE_FASTCALL1_ENTRYPOINT(ExAcquireFastMutexUnsafe)
void WINAPI __regs_ExAcquireFastMutexUnsafe(FAST_MUTEX *mutex)
#else
void WINAPI ExAcquireFastMutexUnsafe(FAST_MUTEX *mutex)
#endif
{
    FIXME("(%p): stub\n", mutex);
}


/***********************************************************************
 *           ExReleaseFastMutexUnsafe  (NTOSKRNL.EXE.@)
 */
#ifdef DEFINE_FASTCALL1_ENTRYPOINT
DEFINE_FASTCALL1_ENTRYPOINT(ExReleaseFastMutexUnsafe)
void WINAPI __regs_ExReleaseFastMutexUnsafe(FAST_MUTEX *mutex)
#else
void WINAPI ExReleaseFastMutexUnsafe(FAST_MUTEX *mutex)
#endif
{
    FIXME("(%p): stub\n", mutex);
}

/***********************************************************************
 *           KeInitializeSemaphore   (NTOSKRNL.EXE.@)
 */
void WINAPI KeInitializeSemaphore( PRKSEMAPHORE semaphore, LONG count, LONG limit )
{
    FIXME( "(%p %d %d) stub\n", semaphore, count, limit );
}

/***********************************************************************
 *           KeReleaseSemaphore   (NTOSKRNL.EXE.@)
 */
LONG WINAPI KeReleaseSemaphore( PRKSEMAPHORE semaphore, KPRIORITY increment,
                                LONG count, BOOLEAN wait )
{
    FIXME("(%p %d %d %d) stub\n", semaphore, increment, count, wait );
    return 0;
}

/***********************************************************************
 *           KeInitializeSpinLock   (NTOSKRNL.EXE.@)
 */
void WINAPI KeInitializeSpinLock( KSPIN_LOCK *spinlock )
{
    FIXME( "stub: %p\n", spinlock );
}

/***********************************************************************
 *           KeAcquireInStackQueuedSpinLock (NTOSKRNL.EXE.@)
 */
#ifdef DEFINE_FASTCALL2_ENTRYPOINT
DEFINE_FASTCALL2_ENTRYPOINT( KeAcquireInStackQueuedSpinLock )
void WINAPI DECLSPEC_HIDDEN __regs_KeAcquireInStackQueuedSpinLock( KSPIN_LOCK *spinlock,
                                                                   KLOCK_QUEUE_HANDLE *handle )
#else
void WINAPI KeAcquireInStackQueuedSpinLock( KSPIN_LOCK *spinlock, KLOCK_QUEUE_HANDLE *handle )
#endif
{
    FIXME( "stub: %p %p\n", spinlock, handle );
}

/***********************************************************************
 *           KeReleaseInStackQueuedSpinLock (NTOSKRNL.EXE.@)
 */
#ifdef DEFINE_FASTCALL1_ENTRYPOINT
DEFINE_FASTCALL1_ENTRYPOINT( KeReleaseInStackQueuedSpinLock )
void WINAPI DECLSPEC_HIDDEN __regs_KeReleaseInStackQueuedSpinLock( KLOCK_QUEUE_HANDLE *handle )
#else
void WINAPI KeReleaseInStackQueuedSpinLock( KLOCK_QUEUE_HANDLE *handle )
#endif
{
    FIXME( "stub: %p\n", handle );
}

/***********************************************************************
 *           KeAcquireSpinLockRaiseToDpc (NTOSKRNL.EXE.@)
 */
KIRQL WINAPI KeAcquireSpinLockRaiseToDpc(KSPIN_LOCK *spinlock)
{
    FIXME( "stub: %p\n", spinlock );
    return 0;
}

/***********************************************************************
 *           KeReleaseSpinLock (NTOSKRNL.EXE.@)
 */
void WINAPI KeReleaseSpinLock( KSPIN_LOCK *spinlock, KIRQL irql )
{
    FIXME( "stub: %p %u\n", spinlock, irql );
}

/***********************************************************************
 *           KeInitializeTimerEx   (NTOSKRNL.EXE.@)
 */
void WINAPI KeInitializeTimerEx( KTIMER *timer, TIMER_TYPE type )
{
    FIXME( "stub: %p %d\n", timer, type );
}

/***********************************************************************
 *           KeInitializeTimer   (NTOSKRNL.EXE.@)
 */
void WINAPI KeInitializeTimer( PKTIMER timer )
{
    KeInitializeTimerEx(timer, NotificationTimer);
}

/***********************************************************************
 *           KeSetTimerEx (NTOSKRNL.EXE.@)
 */
BOOL WINAPI KeSetTimerEx( KTIMER *timer, LARGE_INTEGER duetime, LONG period, KDPC *dpc )
{
    FIXME("stub: %p %s %u %p\n", timer, wine_dbgstr_longlong(duetime.QuadPart), period, dpc);
    return TRUE;
}

/***********************************************************************
 *           KeDelayExecutionThread  (NTOSKRNL.EXE.@)
 */
NTSTATUS WINAPI KeDelayExecutionThread(KPROCESSOR_MODE waitmode, BOOLEAN alertable, LARGE_INTEGER *timeout)
{
    FIXME("(%u, %u, %p): stub\n", waitmode, alertable, timeout);
    return STATUS_NOT_IMPLEMENTED;
}

/***********************************************************************
 *           KeWaitForSingleObject   (NTOSKRNL.EXE.@)
 */
NTSTATUS WINAPI KeWaitForSingleObject(void *object, KWAIT_REASON reason, KPROCESSOR_MODE mode,
                                      BOOLEAN alertable, LARGE_INTEGER *timeout)
{
    FIXME( "stub: %p, %d, %d, %d, %p\n", object, reason, mode, alertable, timeout );
    return STATUS_NOT_IMPLEMENTED;
}

/***********************************************************************
 *           KeWaitForMultipleObjects   (NTOSKRNL.EXE.@)
 */
NTSTATUS WINAPI KeWaitForMultipleObjects(ULONG count, void **objects, WAIT_TYPE type,
                                         KWAIT_REASON reason, KPROCESSOR_MODE mode,
                                         BOOLEAN alertable, LARGE_INTEGER *timeout,
                                         KWAIT_BLOCK *waitblocks)
{
    FIXME( "stub: %u, %p, %d, %d, %d, %d, %p, %p\n",
        count, objects, type, reason, mode, alertable, timeout, waitblocks );
    return STATUS_NOT_IMPLEMENTED;
}
