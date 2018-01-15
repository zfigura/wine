/*
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

#include "windef.h"
#include "wingdi.h"
#include "mmsystem.h"
#include "vfw.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(iyuv);

#define I420_MAGIC mmioFOURCC('i','4','2','0')
#define compare_fourcc(fcc1, fcc2) (((fcc1)^(fcc2))&~0x20202020)

static DWORD_PTR IYUV_Open(ICINFO *icinfo)
{
    if (icinfo && compare_fourcc(icinfo->fccType, ICTYPE_VIDEO)) return 0;

    return 1;
}

static LRESULT IYUV_Close(void)
{
    return 1;
}

static LRESULT IYUV_DecompressQuery(BITMAPINFO *in, BITMAPINFO *out)
{
    if (compare_fourcc(in->bmiHeader.biCompression, I420_MAGIC))
        return ICERR_BADFORMAT;

    if (out)
    {
        if (in->bmiHeader.biPlanes != out->bmiHeader.biPlanes ||
            in->bmiHeader.biWidth != out->bmiHeader.biWidth ||
            in->bmiHeader.biHeight != out->bmiHeader.biHeight)
            return ICERR_BADFORMAT;

        if (out->bmiHeader.biBitCount != 32)
        {
            FIXME("unsupported output bpp %d\n", out->bmiHeader.biBitCount);
            return ICERR_BADFORMAT;
        }
    }

    return ICERR_OK;
}

unsigned char clamp(float x)
{
    if (x > 255) x = 255;
    if (x < 0) x = 0;
    return x;
}

static LRESULT IYUV_Decompress(ICDECOMPRESS *info, unsigned int size)
{
    unsigned char *input = info->lpInput;
    DWORD *output = info->lpOutput;
    LONG width = info->lpbiInput->biWidth;
    LONG height = info->lpbiInput->biHeight;
    int x,y;

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            int Y = input[y * width + x];
            int U = input[width * height + (y / 2) * (width / 2) + (x / 2)];
            int V = input[width * height + ((width / 2) * (height / 2)) + (y / 2) * (width / 2) + (x / 2)];
            unsigned char r,g,b;

            Y -= 16;
            U -= 128;
            V -= 128;

            r = clamp(1.164 * Y + 2.018 * U);
            g = clamp(1.164 * Y - 0.813 * V - 0.391 * U);
            b = clamp(1.164 * Y + 1.596 * V);
            output[y * width + x] = (r) + (g << 8) + (b << 16);
        }
    }

    return ICERR_OK;
}

LRESULT CALLBACK IYUV_DriverProc(DWORD_PTR id, HDRVR driver, UINT msg, LPARAM lparam1, LPARAM lparam2)
{
    TRACE("(%#lx, %p, %#x, %#lx, %#lx)\n", id, driver, msg, lparam1, lparam2);

    switch (msg) {
    case DRV_LOAD:
    case DRV_ENABLE:
    case DRV_DISABLE:
    case DRV_FREE:
        return 1;
    case DRV_OPEN:
        return (LRESULT) IYUV_Open((ICINFO *)lparam2);
    case DRV_CLOSE:
        return IYUV_Close();
    case ICM_DECOMPRESS_QUERY:
        return IYUV_DecompressQuery((BITMAPINFO *)lparam1, (BITMAPINFO *)lparam2);
    case ICM_DECOMPRESS_BEGIN:
    case ICM_DECOMPRESS_END:
        return ICERR_OK;
    case ICM_DECOMPRESS:
        return IYUV_Decompress((ICDECOMPRESS *)lparam1, lparam2);
    default:
        FIXME("unsupported message %#x (%#lx, %#lx)\n", msg, lparam1, lparam2);
        return ICERR_UNSUPPORTED;
    }
}
