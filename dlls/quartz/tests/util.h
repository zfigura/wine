#include "wine/heap.h"

static const WCHAR avifile[] = {'t','e','s','t','.','a','v','i',0};
static const WCHAR mpegfile[] = {'t','e','s','t','.','m','p','g',0};
static const WCHAR mp3file[] = {'t','e','s','t','.','m','p','3',0};
static const WCHAR wavefile[] = {'t','e','s','t','.','w','a','v',0};

WCHAR *load_resource(const WCHAR *name);
IFilterGraph2 *connect_input(IBaseFilter *splitter, const WCHAR *filename);

static inline void copy_media_type(AM_MEDIA_TYPE *dest, const AM_MEDIA_TYPE *src)
{
    *dest = *src;
    if (src->cbFormat)
    {
        dest->pbFormat = heap_alloc(src->cbFormat);
        memcpy(dest->pbFormat, src->pbFormat, src->cbFormat);
    }
}

static inline BOOL compare_media_types(const AM_MEDIA_TYPE *a, const AM_MEDIA_TYPE *b)
{
    return !memcmp(a, b, offsetof(AM_MEDIA_TYPE, pbFormat))
        && !memcmp(a->pbFormat, b->pbFormat, a->cbFormat);
}

struct testpin
{
    IPin IPin_iface;
    LONG ref;
    PIN_DIRECTION dir;
    IBaseFilter *filter;
    IPin *peer;
    AM_MEDIA_TYPE *mt;
    WCHAR name[10];
    WCHAR id[10];

    IEnumMediaTypes IEnumMediaTypes_iface;
    const AM_MEDIA_TYPE *types;
    unsigned int type_count, enum_idx;
    AM_MEDIA_TYPE *request_mt, *accept_mt;

    HRESULT Connect_hr;
    HRESULT EnumMediaTypes_hr;
    HRESULT QueryInternalConnections_hr;

    IAsyncReader *reader;
    IAsyncReader IAsyncReader_iface;
    IMemInputPin IMemInputPin_iface;
};

static inline struct testpin *impl_from_IPin(IPin *iface)
{
    return CONTAINING_RECORD(iface, struct testpin, IPin_iface);
}

HRESULT WINAPI testpin_QueryInterface(IPin *iface, REFIID iid, void **out);
ULONG WINAPI testpin_AddRef(IPin *iface);
ULONG WINAPI testpin_Release(IPin *iface);
HRESULT WINAPI testpin_Disconnect(IPin *iface);
HRESULT WINAPI testpin_ConnectedTo(IPin *iface, IPin **peer);
HRESULT WINAPI testpin_ConnectionMediaType(IPin *iface, AM_MEDIA_TYPE *mt);
HRESULT WINAPI testpin_QueryPinInfo(IPin *iface, PIN_INFO *info);
HRESULT WINAPI testpin_QueryDirection(IPin *iface, PIN_DIRECTION *dir);
HRESULT WINAPI testpin_QueryId(IPin *iface, WCHAR **id);
HRESULT WINAPI testpin_QueryAccept(IPin *iface, const AM_MEDIA_TYPE *mt);
HRESULT WINAPI testpin_EnumMediaTypes(IPin *iface, IEnumMediaTypes **out);
HRESULT WINAPI testpin_QueryInternalConnections(IPin *iface, IPin **out, ULONG *count);
HRESULT WINAPI testpin_EndOfStream(IPin *iface);
HRESULT WINAPI testpin_BeginFlush(IPin *iface);
HRESULT WINAPI testpin_EndFlush(IPin *iface);
HRESULT WINAPI testpin_NewSegment(IPin *iface, REFERENCE_TIME start, REFERENCE_TIME stop, double rate);

HRESULT WINAPI no_Connect(IPin *iface, IPin *peer, const AM_MEDIA_TYPE *mt);
HRESULT WINAPI no_ReceiveConnection(IPin *iface, IPin *peer, const AM_MEDIA_TYPE *mt);

const IAsyncReaderVtbl testreader_vtbl;
const IMemInputPinVtbl testmeminput_vtbl;

void testpin_init(struct testpin *pin, const IPinVtbl *vtbl, PIN_DIRECTION dir);

struct testfilter
{
    IBaseFilter IBaseFilter_iface;
    LONG ref;
    IFilterGraph *graph;
    WCHAR *name;
    IReferenceClock *clock;
    FILTER_STATE state;
    REFERENCE_TIME start_time;

    IEnumPins IEnumPins_iface;
    struct testpin *pins;
    unsigned int pin_count, enum_idx;

    IAMFilterMiscFlags IAMFilterMiscFlags_iface;
    ULONG misc_flags;

    IMediaSeeking IMediaSeeking_iface;
};

void testfilter_init(struct testfilter *filter, struct testpin *pins, int pin_count);
