/*
 * Blob and DXBC functions.
 *
 * Copyright 2010 Rico Schüller
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
 *
 */

#include "d3dcompiler_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3dcompiler);

struct d3dcompiler_blob
{
    ID3DBlob ID3DBlob_iface;
    LONG refcount;

    SIZE_T size;
    void *data;
};

static inline struct d3dcompiler_blob *impl_from_ID3DBlob(ID3DBlob *iface)
{
    return CONTAINING_RECORD(iface, struct d3dcompiler_blob, ID3DBlob_iface);
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE d3dcompiler_blob_QueryInterface(ID3DBlob *iface, REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D10Blob)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3dcompiler_blob_AddRef(ID3DBlob *iface)
{
    struct d3dcompiler_blob *blob = impl_from_ID3DBlob(iface);
    ULONG refcount = InterlockedIncrement(&blob->refcount);

    TRACE("%p increasing refcount to %u\n", blob, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3dcompiler_blob_Release(ID3DBlob *iface)
{
    struct d3dcompiler_blob *blob = impl_from_ID3DBlob(iface);
    ULONG refcount = InterlockedDecrement(&blob->refcount);

    TRACE("%p decreasing refcount to %u\n", blob, refcount);

    if (!refcount)
    {
        HeapFree(GetProcessHeap(), 0, blob->data);
        HeapFree(GetProcessHeap(), 0, blob);
    }

    return refcount;
}

/* ID3DBlob methods */

static void * STDMETHODCALLTYPE d3dcompiler_blob_GetBufferPointer(ID3DBlob *iface)
{
    struct d3dcompiler_blob *blob = impl_from_ID3DBlob(iface);

    TRACE("iface %p\n", iface);

    return blob->data;
}

static SIZE_T STDMETHODCALLTYPE d3dcompiler_blob_GetBufferSize(ID3DBlob *iface)
{
    struct d3dcompiler_blob *blob = impl_from_ID3DBlob(iface);

    TRACE("iface %p\n", iface);

    return blob->size;
}

static const struct ID3D10BlobVtbl d3dcompiler_blob_vtbl =
{
    /* IUnknown methods */
    d3dcompiler_blob_QueryInterface,
    d3dcompiler_blob_AddRef,
    d3dcompiler_blob_Release,
    /* ID3DBlob methods */
    d3dcompiler_blob_GetBufferPointer,
    d3dcompiler_blob_GetBufferSize,
};

static HRESULT d3dcompiler_blob_init(struct d3dcompiler_blob *blob, SIZE_T data_size)
{
    blob->ID3DBlob_iface.lpVtbl = &d3dcompiler_blob_vtbl;
    blob->refcount = 1;
    blob->size = data_size;

    blob->data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, data_size);
    if (!blob->data)
    {
        ERR("Failed to allocate D3D blob data memory\n");
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT WINAPI D3DCreateBlob(SIZE_T data_size, ID3DBlob **blob)
{
    struct d3dcompiler_blob *object;
    HRESULT hr;

    TRACE("data_size %lu, blob %p\n", data_size, blob);

    if (!blob)
    {
        WARN("Invalid blob specified.\n");
        return D3DERR_INVALIDCALL;
    }

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;

    hr = d3dcompiler_blob_init(object, data_size);
    if (FAILED(hr))
    {
        WARN("Failed to initialize blob, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    *blob = &object->ID3DBlob_iface;

    TRACE("Created ID3DBlob %p\n", *blob);

    return S_OK;
}

/*
 * This code implements the MD5 message-digest algorithm.
 * It is based on code in the public domain written by Colin
 * Plumb in 1993. The algorithm is due to Ron Rivest.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 */

#define DXBC_CHECKSUM_BLOCK_SIZE 64

C_ASSERT(sizeof(unsigned int) == 4);

struct md5_ctx
{
    unsigned int i[2];
    unsigned int buf[4];
    unsigned char in[DXBC_CHECKSUM_BLOCK_SIZE];
    unsigned char digest[16];
};

/* The four core functions - F1 is optimized somewhat */

/* #define F1(x, y, z) (x & y | ~x & z) */
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f, w, x, y, z, data, s) \
        (w += f(x, y, z) + data,  w = w << s | w >> (32 - s),  w += x)

/*
 * The core of the MD5 algorithm, this alters an existing MD5 hash to
 * reflect the addition of 16 longwords of new data. md5_update blocks
 * the data and converts bytes into longwords for this routine.
 */
static void md5_transform(unsigned int buf[4], const unsigned int in[16])
{
    unsigned int a, b, c, d;

    a = buf[0];
    b = buf[1];
    c = buf[2];
    d = buf[3];

    MD5STEP(F1, a, b, c, d, in[0] + 0xd76aa478, 7);
    MD5STEP(F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
    MD5STEP(F1, c, d, a, b, in[2] + 0x242070db, 17);
    MD5STEP(F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
    MD5STEP(F1, a, b, c, d, in[4] + 0xf57c0faf, 7);
    MD5STEP(F1, d, a, b, c, in[5] + 0x4787c62a, 12);
    MD5STEP(F1, c, d, a, b, in[6] + 0xa8304613, 17);
    MD5STEP(F1, b, c, d, a, in[7] + 0xfd469501, 22);
    MD5STEP(F1, a, b, c, d, in[8] + 0x698098d8, 7);
    MD5STEP(F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
    MD5STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
    MD5STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
    MD5STEP(F1, a, b, c, d, in[12] + 0x6b901122, 7);
    MD5STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
    MD5STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
    MD5STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

    MD5STEP(F2, a, b, c, d, in[1] + 0xf61e2562, 5);
    MD5STEP(F2, d, a, b, c, in[6] + 0xc040b340, 9);
    MD5STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
    MD5STEP(F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
    MD5STEP(F2, a, b, c, d, in[5] + 0xd62f105d, 5);
    MD5STEP(F2, d, a, b, c, in[10] + 0x02441453, 9);
    MD5STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
    MD5STEP(F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
    MD5STEP(F2, a, b, c, d, in[9] + 0x21e1cde6, 5);
    MD5STEP(F2, d, a, b, c, in[14] + 0xc33707d6, 9);
    MD5STEP(F2, c, d, a, b, in[3] + 0xf4d50d87, 14);
    MD5STEP(F2, b, c, d, a, in[8] + 0x455a14ed, 20);
    MD5STEP(F2, a, b, c, d, in[13] + 0xa9e3e905, 5);
    MD5STEP(F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
    MD5STEP(F2, c, d, a, b, in[7] + 0x676f02d9, 14);
    MD5STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

    MD5STEP(F3, a, b, c, d, in[5] + 0xfffa3942, 4);
    MD5STEP(F3, d, a, b, c, in[8] + 0x8771f681, 11);
    MD5STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
    MD5STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
    MD5STEP(F3, a, b, c, d, in[1] + 0xa4beea44, 4);
    MD5STEP(F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
    MD5STEP(F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);
    MD5STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
    MD5STEP(F3, a, b, c, d, in[13] + 0x289b7ec6, 4);
    MD5STEP(F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
    MD5STEP(F3, c, d, a, b, in[3] + 0xd4ef3085, 16);
    MD5STEP(F3, b, c, d, a, in[6] + 0x04881d05, 23);
    MD5STEP(F3, a, b, c, d, in[9] + 0xd9d4d039, 4);
    MD5STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
    MD5STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
    MD5STEP(F3, b, c, d, a, in[2] + 0xc4ac5665, 23);

    MD5STEP(F4, a, b, c, d, in[0] + 0xf4292244, 6);
    MD5STEP(F4, d, a, b, c, in[7] + 0x432aff97, 10);
    MD5STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
    MD5STEP(F4, b, c, d, a, in[5] + 0xfc93a039, 21);
    MD5STEP(F4, a, b, c, d, in[12] + 0x655b59c3, 6);
    MD5STEP(F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
    MD5STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
    MD5STEP(F4, b, c, d, a, in[1] + 0x85845dd1, 21);
    MD5STEP(F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);
    MD5STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
    MD5STEP(F4, c, d, a, b, in[6] + 0xa3014314, 15);
    MD5STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
    MD5STEP(F4, a, b, c, d, in[4] + 0xf7537e82, 6);
    MD5STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
    MD5STEP(F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);
    MD5STEP(F4, b, c, d, a, in[9] + 0xeb86d391, 21);

    buf[0] += a;
    buf[1] += b;
    buf[2] += c;
    buf[3] += d;
}

/*
 * Note: this code is harmless on little-endian machines.
 */
static void byte_reverse(unsigned char *buf, unsigned longs)
{
    unsigned int t;

    do
    {
        t = ((unsigned)buf[3] << 8 | buf[2]) << 16 |
            ((unsigned)buf[1] << 8 | buf[0]);
        *(unsigned int *)buf = t;
        buf += 4;
    } while (--longs);
}

/*
 * Start MD5 accumulation. Set bit count to 0 and buffer to mysterious
 * initialization constants.
 */
static void md5_init(struct md5_ctx *ctx)
{
    ctx->buf[0] = 0x67452301;
    ctx->buf[1] = 0xefcdab89;
    ctx->buf[2] = 0x98badcfe;
    ctx->buf[3] = 0x10325476;

    ctx->i[0] = ctx->i[1] = 0;
}

/*
 * Update context to reflect the concatenation of another buffer full
 * of bytes.
 */
static void md5_update(struct md5_ctx *ctx, const unsigned char *buf, unsigned int len)
{
    unsigned int t;

    /* Update bitcount */
    t = ctx->i[0];

    if ((ctx->i[0] = t + (len << 3)) < t)
        ctx->i[1]++;        /* Carry from low to high */

    ctx->i[1] += len >> 29;
    t = (t >> 3) & 0x3f;

    /* Handle any leading odd-sized chunks */
    if (t)
    {
        unsigned char *p = (unsigned char *)ctx->in + t;
        t = DXBC_CHECKSUM_BLOCK_SIZE - t;

        if (len < t)
        {
            memcpy(p, buf, len);
            return;
        }

        memcpy(p, buf, t);
        byte_reverse(ctx->in, 16);

        md5_transform(ctx->buf, (unsigned int *)ctx->in);

        buf += t;
        len -= t;
    }

    /* Process data in 64-byte chunks */
    while (len >= DXBC_CHECKSUM_BLOCK_SIZE)
    {
        memcpy(ctx->in, buf, DXBC_CHECKSUM_BLOCK_SIZE);
        byte_reverse(ctx->in, 16);

        md5_transform(ctx->buf, (unsigned int *)ctx->in);

        buf += DXBC_CHECKSUM_BLOCK_SIZE;
        len -= DXBC_CHECKSUM_BLOCK_SIZE;
    }

    /* Handle any remaining bytes of data. */
    memcpy(ctx->in, buf, len);
}

static void dxbc_checksum_final(struct md5_ctx *ctx)
{
    unsigned int padding;
    unsigned int length;
    unsigned int count;
    unsigned char *p;

    /* Compute number of bytes mod 64 */
    count = (ctx->i[0] >> 3) & 0x3F;

    /* Set the first char of padding to 0x80.  This is safe since there is
       always at least one byte free */
    p = ctx->in + count;
    *p++ = 0x80;
    ++count;

    /* Bytes of padding needed to make 64 bytes */
    padding = DXBC_CHECKSUM_BLOCK_SIZE - count;

    /* Pad out to 56 mod 64 */
    if (padding < 8)
    {
        /* Two lots of padding:  Pad the first block to 64 bytes */
        memset(p, 0, padding);
        byte_reverse(ctx->in, 16);
        md5_transform(ctx->buf, (unsigned int *)ctx->in);

        /* Now fill the next block */
        memset(ctx->in, 0, DXBC_CHECKSUM_BLOCK_SIZE);
    }
    else
    {
        /* Make place for bitcount at the beginning of the block */
        memmove(&ctx->in[4], ctx->in, count);

        /* Pad block to 60 bytes */
        memset(p + 4, 0, padding - 4);
    }

    /* Append length in bits and transform */
    length = ctx->i[0];
    memcpy(&ctx->in[0], &length, sizeof(length));
    byte_reverse(&ctx->in[4], 14);
    length = ctx->i[0] >> 2 | 0x1;
    memcpy(&ctx->in[DXBC_CHECKSUM_BLOCK_SIZE - 4], &length, sizeof(length));

    md5_transform(ctx->buf, (unsigned int *)ctx->in);
    byte_reverse((unsigned char *)ctx->buf, 4);
    memcpy(ctx->digest, ctx->buf, 16);
}

#define DXBC_CHECKSUM_SKIP_BYTE_COUNT 20

static void dxbc_compute_checksum(const void *dxbc, size_t size, uint32_t checksum[4])
{
    const uint8_t *ptr = dxbc;
    struct md5_ctx ctx;

    assert(size > DXBC_CHECKSUM_SKIP_BYTE_COUNT);
    ptr += DXBC_CHECKSUM_SKIP_BYTE_COUNT;
    size -= DXBC_CHECKSUM_SKIP_BYTE_COUNT;

    md5_init(&ctx);
    md5_update(&ctx, ptr, size);
    dxbc_checksum_final(&ctx);

    memcpy(checksum, ctx.digest, sizeof(ctx.digest));
}

void skip_dword_unknown(const char **ptr, unsigned int count)
{
    unsigned int i;
    DWORD d;

    FIXME("Skipping %u unknown DWORDs:\n", count);
    for (i = 0; i < count; ++i)
    {
        read_dword(ptr, &d);
        FIXME("\t0x%08x\n", d);
    }
}

HRESULT dxbc_add_section(struct dxbc *dxbc, DWORD tag, const void *data, DWORD data_size)
{
    TRACE("dxbc %p, tag %s, size %#x.\n", dxbc, debugstr_an((const char *)&tag, 4), data_size);

    if (dxbc->count >= dxbc->size)
    {
        struct dxbc_section *new_sections;
        DWORD new_size = dxbc->size << 1;

        new_sections = HeapReAlloc(GetProcessHeap(), 0, dxbc->sections, new_size * sizeof(*dxbc->sections));
        if (!new_sections)
            return E_OUTOFMEMORY;

        dxbc->sections = new_sections;
        dxbc->size = new_size;
    }

    dxbc->sections[dxbc->count].tag = tag;
    dxbc->sections[dxbc->count].data_size = data_size;
    dxbc->sections[dxbc->count].data = data;
    ++dxbc->count;

    return S_OK;
}

HRESULT dxbc_init(struct dxbc *dxbc, unsigned int size)
{
    TRACE("dxbc %p, size %u.\n", dxbc, size);

    if (!size) size = 2;

    dxbc->sections = HeapAlloc(GetProcessHeap(), 0, size * sizeof(*dxbc->sections));
    if (!dxbc->sections)
        return E_OUTOFMEMORY;

    dxbc->size = size;
    dxbc->count = 0;

    return S_OK;
}

HRESULT dxbc_parse(const char *data, SIZE_T data_size, struct dxbc *dxbc)
{
    DWORD tag, total_size, chunk_count, version;
    const char *ptr = data;
    unsigned int i;
    HRESULT hr;

    if (!data)
    {
        WARN("No data supplied.\n");
        return E_FAIL;
    }

    read_dword(&ptr, &tag);
    TRACE("Tag: %s.\n", debugstr_an((const char *)&tag, 4));

    if (tag != TAG_DXBC)
    {
        WARN("Wrong tag %s.\n", debugstr_an((const char *)&tag, 4));
        return E_FAIL;
    }

    WARN("Ignoring DXBC checksum.\n");
    skip_dword_unknown(&ptr, 4);

    read_dword(&ptr, &version);
    TRACE("Version: %#x.\n", version);
    if (version != 1)
    {
        WARN("Got unexpected DXBC version %#x.\n", version);
        return E_INVALIDARG;
    }

    read_dword(&ptr, &total_size);
    TRACE("Total size: %#x.\n", total_size);

    if (data_size != total_size)
    {
        WARN("Wrong size supplied.\n");
        return D3DERR_INVALIDCALL;
    }

    read_dword(&ptr, &chunk_count);
    TRACE("Chunk count: %#x.\n", chunk_count);

    if (FAILED(hr = dxbc_init(dxbc, chunk_count)))
        return hr;

    for (i = 0; i < chunk_count; ++i)
    {
        DWORD chunk_tag, chunk_size;
        const char *chunk_ptr;
        DWORD chunk_offset;

        read_dword(&ptr, &chunk_offset);
        TRACE("Chunk %u at offset %#x.\n", i, chunk_offset);

        chunk_ptr = data + chunk_offset;

        read_dword(&chunk_ptr, &chunk_tag);
        read_dword(&chunk_ptr, &chunk_size);

        if (FAILED(hr = dxbc_add_section(dxbc, chunk_tag, chunk_ptr, chunk_size)))
            return hr;
    }

    return hr;
}

void dxbc_destroy(struct dxbc *dxbc)
{
    TRACE("dxbc %p.\n", dxbc);

    HeapFree(GetProcessHeap(), 0, dxbc->sections);
}

HRESULT dxbc_write_blob(struct dxbc *dxbc, ID3DBlob **blob)
{
    DWORD size = 32, offset = size + 4 * dxbc->count, checksum[4];
    ID3DBlob *object;
    char *data, *ptr;
    unsigned int i;
    HRESULT hr;

    TRACE("dxbc %p, blob %p.\n", dxbc, blob);

    for (i = 0; i < dxbc->count; ++i)
        size += 12 + dxbc->sections[i].data_size;

    if (FAILED(hr = D3DCreateBlob(size, &object)))
        return hr;

    ptr = data = ID3D10Blob_GetBufferPointer(object);

    write_dword(&ptr, TAG_DXBC);

    /* The checksum is computed when all data is generated. */
    write_dword(&ptr, 0);
    write_dword(&ptr, 0);
    write_dword(&ptr, 0);
    write_dword(&ptr, 0);

    /* Version. */
    write_dword(&ptr, 1);

    write_dword(&ptr, size);
    write_dword(&ptr, dxbc->count);

    for (i = 0; i < dxbc->count; ++i)
    {
        write_dword(&ptr, offset);
        offset += 8 + dxbc->sections[i].data_size;
    }

    for (i = 0; i < dxbc->count; ++i)
    {
        write_dword(&ptr, dxbc->sections[i].tag);
        write_dword(&ptr, dxbc->sections[i].data_size);
        memcpy(ptr, dxbc->sections[i].data, dxbc->sections[i].data_size);
        ptr += dxbc->sections[i].data_size;
    }

    dxbc_compute_checksum(data, size, checksum);
    memcpy((DWORD *)data + 1, checksum, sizeof(checksum));

    *blob = object;
    return S_OK;
}

static BOOL check_blob_part(DWORD tag, D3D_BLOB_PART part)
{
    BOOL add = FALSE;

    switch(part)
    {
        case D3D_BLOB_INPUT_SIGNATURE_BLOB:
            if (tag == TAG_ISGN) add = TRUE;
            break;

        case D3D_BLOB_OUTPUT_SIGNATURE_BLOB:
            if (tag == TAG_OSGN || tag == TAG_OSG5) add = TRUE;
            break;

        case D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB:
            if (tag == TAG_ISGN || tag == TAG_OSGN || tag == TAG_OSG5) add = TRUE;
            break;

        case D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB:
            if (tag == TAG_PCSG) add = TRUE;
            break;

        case D3D_BLOB_ALL_SIGNATURE_BLOB:
            if (tag == TAG_ISGN || tag == TAG_OSGN || tag == TAG_OSG5 || tag == TAG_PCSG) add = TRUE;
            break;

        case D3D_BLOB_DEBUG_INFO:
            if (tag == TAG_SDBG) add = TRUE;
            break;

        case D3D_BLOB_LEGACY_SHADER:
            if (tag == TAG_Aon9) add = TRUE;
            break;

        case D3D_BLOB_XNA_PREPASS_SHADER:
            if (tag == TAG_XNAP) add = TRUE;
            break;

        case D3D_BLOB_XNA_SHADER:
            if (tag == TAG_XNAS) add = TRUE;
            break;

        default:
            FIXME("Unhandled D3D_BLOB_PART %s.\n", debug_d3dcompiler_d3d_blob_part(part));
            break;
    }

    TRACE("%s tag %s\n", add ? "Add" : "Skip", debugstr_an((const char *)&tag, 4));

    return add;
}

static HRESULT d3dcompiler_get_blob_part(const void *data, SIZE_T data_size, D3D_BLOB_PART part, UINT flags, ID3DBlob **blob)
{
    struct dxbc src_dxbc, dst_dxbc;
    HRESULT hr;
    unsigned int i, count;

    if (!data || !data_size || flags || !blob)
    {
        WARN("Invalid arguments: data %p, data_size %lu, flags %#x, blob %p\n", data, data_size, flags, blob);
        return D3DERR_INVALIDCALL;
    }

    if (part > D3D_BLOB_TEST_COMPILE_PERF
            || (part < D3D_BLOB_TEST_ALTERNATE_SHADER && part > D3D_BLOB_XNA_SHADER))
    {
        WARN("Invalid D3D_BLOB_PART: part %s\n", debug_d3dcompiler_d3d_blob_part(part));
        return D3DERR_INVALIDCALL;
    }

    hr = dxbc_parse(data, data_size, &src_dxbc);
    if (FAILED(hr))
    {
        WARN("Failed to parse blob part\n");
        return hr;
    }

    hr = dxbc_init(&dst_dxbc, 0);
    if (FAILED(hr))
    {
        dxbc_destroy(&src_dxbc);
        WARN("Failed to init dxbc\n");
        return hr;
    }

    for (i = 0; i < src_dxbc.count; ++i)
    {
        struct dxbc_section *section = &src_dxbc.sections[i];

        if (check_blob_part(section->tag, part))
        {
            hr = dxbc_add_section(&dst_dxbc, section->tag, section->data, section->data_size);
            if (FAILED(hr))
            {
                dxbc_destroy(&src_dxbc);
                dxbc_destroy(&dst_dxbc);
                WARN("Failed to add section to dxbc\n");
                return hr;
            }
        }
    }

    count = dst_dxbc.count;

    switch(part)
    {
        case D3D_BLOB_INPUT_SIGNATURE_BLOB:
        case D3D_BLOB_OUTPUT_SIGNATURE_BLOB:
        case D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB:
        case D3D_BLOB_DEBUG_INFO:
        case D3D_BLOB_LEGACY_SHADER:
        case D3D_BLOB_XNA_PREPASS_SHADER:
        case D3D_BLOB_XNA_SHADER:
            if (count != 1) count = 0;
            break;

        case D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB:
            if (count != 2) count = 0;
            break;

        case D3D_BLOB_ALL_SIGNATURE_BLOB:
            if (count != 3) count = 0;
            break;

        default:
            FIXME("Unhandled D3D_BLOB_PART %s.\n", debug_d3dcompiler_d3d_blob_part(part));
            break;
    }

    if (count == 0)
    {
        dxbc_destroy(&src_dxbc);
        dxbc_destroy(&dst_dxbc);
        WARN("Nothing to write into the blob (count = 0)\n");
        return E_FAIL;
    }

    /* some parts aren't full DXBCs, they contain only the data */
    if (count == 1 && (part == D3D_BLOB_DEBUG_INFO || part == D3D_BLOB_LEGACY_SHADER || part == D3D_BLOB_XNA_PREPASS_SHADER
            || part == D3D_BLOB_XNA_SHADER))
    {
        hr = D3DCreateBlob(dst_dxbc.sections[0].data_size, blob);
        if (SUCCEEDED(hr))
        {
            memcpy(ID3D10Blob_GetBufferPointer(*blob), dst_dxbc.sections[0].data, dst_dxbc.sections[0].data_size);
        }
        else
        {
            WARN("Could not create blob\n");
        }
    }
    else
    {
        hr = dxbc_write_blob(&dst_dxbc, blob);
        if (FAILED(hr))
        {
            WARN("Failed to write blob part\n");
        }
    }

    dxbc_destroy(&src_dxbc);
    dxbc_destroy(&dst_dxbc);

    return hr;
}

static BOOL check_blob_strip(DWORD tag, UINT flags)
{
    BOOL add = TRUE;

    if (flags & D3DCOMPILER_STRIP_TEST_BLOBS) FIXME("Unhandled flag D3DCOMPILER_STRIP_TEST_BLOBS.\n");

    switch(tag)
    {
        case TAG_RDEF:
        case TAG_STAT:
            if (flags & D3DCOMPILER_STRIP_REFLECTION_DATA) add = FALSE;
            break;

        case TAG_SDBG:
            if (flags & D3DCOMPILER_STRIP_DEBUG_INFO) add = FALSE;
            break;

        default:
            break;
    }

    TRACE("%s tag %s\n", add ? "Add" : "Skip", debugstr_an((const char *)&tag, 4));

    return add;
}

static HRESULT d3dcompiler_strip_shader(const void *data, SIZE_T data_size, UINT flags, ID3DBlob **blob)
{
    struct dxbc src_dxbc, dst_dxbc;
    HRESULT hr;
    unsigned int i;

    if (!blob)
    {
        WARN("NULL for blob specified\n");
        return E_FAIL;
    }

    if (!data || !data_size)
    {
        WARN("Invalid arguments: data %p, data_size %lu\n", data, data_size);
        return D3DERR_INVALIDCALL;
    }

    hr = dxbc_parse(data, data_size, &src_dxbc);
    if (FAILED(hr))
    {
        WARN("Failed to parse blob part\n");
        return hr;
    }

    /* src_dxbc.count >= dst_dxbc.count */
    hr = dxbc_init(&dst_dxbc, src_dxbc.count);
    if (FAILED(hr))
    {
        dxbc_destroy(&src_dxbc);
        WARN("Failed to init dxbc\n");
        return hr;
    }

    for (i = 0; i < src_dxbc.count; ++i)
    {
        struct dxbc_section *section = &src_dxbc.sections[i];

        if (check_blob_strip(section->tag, flags))
        {
            hr = dxbc_add_section(&dst_dxbc, section->tag, section->data, section->data_size);
            if (FAILED(hr))
            {
                dxbc_destroy(&src_dxbc);
                dxbc_destroy(&dst_dxbc);
                WARN("Failed to add section to dxbc\n");
                return hr;
            }
        }
    }

    hr = dxbc_write_blob(&dst_dxbc, blob);
    if (FAILED(hr))
    {
        WARN("Failed to write blob part\n");
    }

    dxbc_destroy(&src_dxbc);
    dxbc_destroy(&dst_dxbc);

    return hr;
}

HRESULT WINAPI D3DGetBlobPart(const void *data, SIZE_T data_size, D3D_BLOB_PART part, UINT flags, ID3DBlob **blob)
{
    TRACE("data %p, data_size %lu, part %s, flags %#x, blob %p\n", data,
           data_size, debug_d3dcompiler_d3d_blob_part(part), flags, blob);

    return d3dcompiler_get_blob_part(data, data_size, part, flags, blob);
}

HRESULT WINAPI D3DGetInputSignatureBlob(const void *data, SIZE_T data_size, ID3DBlob **blob)
{
    TRACE("data %p, data_size %lu, blob %p\n", data, data_size, blob);

    return d3dcompiler_get_blob_part(data, data_size, D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, blob);
}

HRESULT WINAPI D3DGetOutputSignatureBlob(const void *data, SIZE_T data_size, ID3DBlob **blob)
{
    TRACE("data %p, data_size %lu, blob %p\n", data, data_size, blob);

    return d3dcompiler_get_blob_part(data, data_size, D3D_BLOB_OUTPUT_SIGNATURE_BLOB, 0, blob);
}

HRESULT WINAPI D3DGetInputAndOutputSignatureBlob(const void *data, SIZE_T data_size, ID3DBlob **blob)
{
    TRACE("data %p, data_size %lu, blob %p\n", data, data_size, blob);

    return d3dcompiler_get_blob_part(data, data_size, D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB, 0, blob);
}

HRESULT WINAPI D3DGetDebugInfo(const void *data, SIZE_T data_size, ID3DBlob **blob)
{
    TRACE("data %p, data_size %lu, blob %p\n", data, data_size, blob);

    return d3dcompiler_get_blob_part(data, data_size, D3D_BLOB_DEBUG_INFO, 0, blob);
}

HRESULT WINAPI D3DStripShader(const void *data, SIZE_T data_size, UINT flags, ID3D10Blob **blob)
{
    TRACE("data %p, data_size %lu, flags %#x, blob %p\n", data, data_size, flags, blob);

    return d3dcompiler_strip_shader(data, data_size, flags, blob);
}

HRESULT WINAPI D3DReadFileToBlob(const WCHAR *filename, ID3DBlob **contents)
{
    struct d3dcompiler_blob *object;
    SIZE_T data_size;
    DWORD read_size;
    HANDLE file;
    HRESULT hr;

    TRACE("filename %s, contents %p.\n", debugstr_w(filename), contents);

    file = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());

    data_size = GetFileSize(file, NULL);
    if (data_size == INVALID_FILE_SIZE)
    {
        CloseHandle(file);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (!(object = heap_alloc_zero(sizeof(*object))))
    {
        CloseHandle(file);
        return E_OUTOFMEMORY;
    }

    if (FAILED(hr = d3dcompiler_blob_init(object, data_size)))
    {
        WARN("Failed to initialize blob, hr %#x.\n", hr);
        CloseHandle(file);
        heap_free(object);
        return hr;
    }

    if (!ReadFile(file, object->data, data_size, &read_size, NULL) || (read_size != data_size))
    {
        WARN("Failed to read file contents.\n");
        CloseHandle(file);
        heap_free(object->data);
        heap_free(object);
        return E_FAIL;
    }
    CloseHandle(file);
    object->size = read_size;

    *contents = &object->ID3DBlob_iface;

    TRACE("Returning ID3DBlob %p.\n", *contents);

    return S_OK;
}

HRESULT WINAPI D3DWriteBlobToFile(ID3DBlob* blob, const WCHAR *filename, BOOL overwrite)
{
    FIXME("blob %p, filename %s, overwrite %d\n", blob, debugstr_w(filename), overwrite);

    return E_NOTIMPL;
}
