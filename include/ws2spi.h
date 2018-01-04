/*
 * WS2SPI.H -- definitions to be used with the WinSock service provider.
 *
 * Copyright (C) 2001 Patrik Stridvall
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

#ifndef _WINSOCK2SPI_
#define _WINSOCK2SPI_

#ifndef _WINSOCK2API_
#include <winsock2.h>
#endif /* !defined(_WINSOCK2API_) */

#include <pshpack4.h>

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

typedef struct _WSATHREADID {
    HANDLE ThreadHandle;
    DWORD_PTR Reserved;
} WSATHREADID, *LPWSATHREADID;

typedef BOOL (WINAPI *LPWPUPOSTMESSAGE)(HWND,UINT,WPARAM,LPARAM);

WSAEVENT WINAPI WPUCompleteOverlappedRequest(SOCKET,LPWSAOVERLAPPED,DWORD,DWORD,LPINT);
INT      WINAPI WSCInstallProvider(const LPGUID,LPCWSTR,const LPWSAPROTOCOL_INFOW,
                                   DWORD,LPINT);
INT      WINAPI WSCDeinstallProvider(LPGUID,LPINT);
INT      WINAPI WSCEnableNSProvider(LPGUID,BOOL);
INT      WINAPI WSCEnumProtocols(LPINT,LPWSAPROTOCOL_INFOW,LPDWORD,LPINT);
INT      WINAPI WSCGetProviderPath(LPGUID,LPWSTR,LPINT,LPINT);
INT      WINAPI WSCInstallNameSpace(LPWSTR,LPWSTR,DWORD,DWORD,LPGUID);
INT      WINAPI WSCUnInstallNameSpace(LPGUID);
INT      WINAPI WSCUpdateProvider(LPGUID, const WCHAR *, const LPWSAPROTOCOL_INFOW, DWORD, LPINT);
INT      WINAPI WSCWriteProviderOrder(LPDWORD,DWORD);

typedef INT (WSAAPI *LPNSPCLEANUP)(LPGUID lpProviderId);
typedef INT (WSAAPI *LPNSPLOOKUPSERVICEBEGIN)(LPGUID lpProviderId,LPWSAQUERYSETW lpqsRestrictions,LPWSASERVICECLASSINFOW lpServiceClassInfo,DWORD dwControlFlags,LPHANDLE lphLookup);
typedef INT (WSAAPI *LPNSPLOOKUPSERVICENEXT)(HANDLE hLookup,DWORD dwControlFlags,LPDWORD lpdwBufferLength,LPWSAQUERYSETW lpqsResults);
typedef INT (WSAAPI *LPNSPIOCTL)(HANDLE hLookup,DWORD dwControlCode,LPVOID lpvInBuffer,DWORD cbInBuffer,LPVOID lpvOutBuffer,DWORD cbOutBuffer,LPDWORD lpcbBytesReturned,LPWSACOMPLETION lpCompletion,LPWSATHREADID lpThreadId);
typedef INT (WSAAPI *LPNSPLOOKUPSERVICEEND)(HANDLE hLookup);
typedef INT (WSAAPI *LPNSPSETSERVICE)(LPGUID lpProviderId,LPWSASERVICECLASSINFOW lpServiceClassInfo,LPWSAQUERYSETW lpqsRegInfo,WSAESETSERVICEOP essOperation,DWORD dwControlFlags);
typedef INT (WSAAPI *LPNSPINSTALLSERVICECLASS)(LPGUID lpProviderId,LPWSASERVICECLASSINFOW lpServiceClassInfo);
typedef INT (WSAAPI *LPNSPREMOVESERVICECLASS)(LPGUID lpProviderId,LPGUID lpServiceClassId);
typedef INT (WSAAPI *LPNSPGETSERVICECLASSINFO)(LPGUID lpProviderId,LPDWORD lpdwBufSize,LPWSASERVICECLASSINFOW lpServiceClassInfo);

typedef struct _NSP_ROUTINE {
    DWORD cbSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    LPNSPCLEANUP NSPCleanup;
    LPNSPLOOKUPSERVICEBEGIN NSPLookupServiceBegin;
    LPNSPLOOKUPSERVICENEXT NSPLookupServiceNext;
    LPNSPLOOKUPSERVICEEND NSPLookupServiceEnd;
    LPNSPSETSERVICE NSPSetService;
    LPNSPINSTALLSERVICECLASS NSPInstallServiceClass;
    LPNSPREMOVESERVICECLASS NSPRemoveServiceClass;
    LPNSPGETSERVICECLASSINFO NSPGetServiceClassInfo;
    LPNSPIOCTL NSPIoctl;
} NSP_ROUTINE, *LPNSP_ROUTINE;

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */

#include <poppack.h>

#endif /* !defined(_WINSOCK2SPI_) */
