/* Pinned Wuffs 0.3.5 or its explicit GTOS concrete-API release adaptation. */
#if defined(GTOS_NATIVE_COMPONENT) && (!defined(__i386__) || defined(__linux__))
#error A freestanding non-Linux i386 compiler is required for the GTOS component.
#endif
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE__CORE
#define WUFFS_CONFIG__MODULE__BASE__INTERFACES
#define WUFFS_CONFIG__MODULE__BASE__PIXCONV
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__AVOID_CPU_ARCH
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#if defined(GTOS_PNG_USE_DERIVED_RELEASE)
#include "wuffs-png-gtos.c"
#else
#include "wuffs-v0.3.c"
#endif
#include "png_decode.h"

_Static_assert(WUFFS_VERSION_MAJOR == 0 && WUFFS_VERSION_MINOR == 3 &&
               WUFFS_VERSION_PATCH == 5, "pinned Wuffs 0.3.5 required");
_Static_assert(sizeof(wuffs_png__decoder) <= UINT32_MAX, "PNG context size ABI");
_Static_assert(_Alignof(wuffs_png__decoder) <= 8, "PNG context alignment ABI");
_Static_assert(sizeof(gtos_png_requirements) == 24, "PNG requirements ABI");

typedef struct png_region {
    uintptr_t start;
    uintptr_t end;
    uint32_t bytes;
} png_region;

typedef struct png_envelope {
    uint32_t width, height;
    uint32_t last_nonempty_idat_end;
    uint32_t palette_entries;
    unsigned char color_type;
    unsigned char transparent_bgra[3];
    unsigned char restore_transparent_rgb;
} png_envelope;

static uint32_t png_u32be(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static gtos_png_status png_region_set(png_region* r, const void* p, uint32_t n) {
    if (!p) return GTOS_PNG_BAD_ARGUMENT;
    r->start = (uintptr_t)p;
    if ((uint64_t)n > (uint64_t)(UINTPTR_MAX - r->start))
        return GTOS_PNG_POINTER_OVERFLOW;
    r->end = r->start + (uintptr_t)n;
    r->bytes = n;
    return GTOS_PNG_OK;
}

static gtos_png_status png_regions_disjoint(const png_region* r, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        for (unsigned j = i + 1; j < n; ++j)
            if (r[i].bytes && r[j].bytes &&
                r[i].start < r[j].end && r[j].start < r[i].end)
                return GTOS_PNG_OVERLAP;
    return GTOS_PNG_OK;
}

static gtos_png_status png_arguments(void* context, uint32_t context_bytes,
    const unsigned char* input, uint32_t input_bytes,
    gtos_png_requirements* requirements, png_region* regions) {
    if (!context || !input || !requirements || ((uintptr_t)context & 7u) ||
        ((uintptr_t)requirements % _Alignof(gtos_png_requirements)))
        return GTOS_PNG_BAD_ARGUMENT;
    gtos_png_status result = png_region_set(regions, context, context_bytes);
    if (result) return result;
    result = png_region_set(regions + 1, input, input_bytes);
    if (result) return result;
    result = png_region_set(regions + 2, requirements, sizeof(*requirements));
    if (result) return result;
    if (context_bytes < sizeof(wuffs_png__decoder)) return GTOS_PNG_TOO_SMALL;
    return png_regions_disjoint(regions, 3);
}

static int png_legal_depth(unsigned type, unsigned depth) {
    if (type == 0)
        return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    if (type == 3)
        return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    if (type == 2 || type == 4 || type == 6) return depth == 8 || depth == 16;
    return 0;
}

/* Validates core image semantics and the entire generic chunk envelope. Metadata
 * interpretation is deliberately outside this integer raster contract. */
static gtos_png_status png_preflight(const unsigned char* input, uint32_t bytes,
                                    png_envelope* envelope) {
    static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes < 8) return GTOS_PNG_CORRUPT;
    for (unsigned i = 0; i < 8; ++i)
        if (input[i] != signature[i]) return GTOS_PNG_CORRUPT;
    uint32_t offset = 8;
    unsigned depth = 0, type = 0;
    int header = 0, palette = 0, transparency = 0, idat = 0, idat_ended = 0;
    int unsupported = 0;
    png_envelope parsed = {0};
    while (offset < bytes) {
        const uint32_t remaining = bytes - offset;
        if (remaining < 12) return GTOS_PNG_CORRUPT;
        const unsigned char* chunk = input + offset;
        const uint32_t length = png_u32be(chunk);
        if (length > INT32_MAX || length > remaining - 12) return GTOS_PNG_CORRUPT;
        for (unsigned i = 4; i < 8; ++i)
            if (!((chunk[i] >= 'A' && chunk[i] <= 'Z') ||
                  (chunk[i] >= 'a' && chunk[i] <= 'z')))
                return GTOS_PNG_CORRUPT;
        if (chunk[6] & 32u) return GTOS_PNG_CORRUPT;
        wuffs_crc32__ieee_hasher crc;
        wuffs_base__status status = wuffs_crc32__ieee_hasher__initialize(
            &crc, sizeof(crc), WUFFS_VERSION, 0);
        if (status.repr) return GTOS_PNG_DECODER_FAILURE;
        const uint32_t actual_crc = wuffs_crc32__ieee_hasher__update_u32(
            &crc, wuffs_base__make_slice_u8((uint8_t*)(chunk + 4), (size_t)length + 4));
        if (actual_crc != png_u32be(chunk + 8 + length)) return GTOS_PNG_CORRUPT;
        const uint32_t kind = png_u32be(chunk + 4);
        const unsigned char* data = chunk + 8;
        const uint32_t end = offset + length + 12;
        if (!header && kind != UINT32_C(0x49484452)) return GTOS_PNG_CORRUPT;
        if (kind != UINT32_C(0x49444154) && idat) idat_ended = 1;
        switch (kind) {
        case UINT32_C(0x49484452): /* IHDR */
            if (header || offset != 8 || length != 13) return GTOS_PNG_CORRUPT;
            parsed.width = png_u32be(data);
            parsed.height = png_u32be(data + 4);
            depth = data[8]; type = data[9];
            if (!parsed.width || !parsed.height || parsed.width > INT32_MAX ||
                parsed.height > INT32_MAX || !png_legal_depth(type, depth) ||
                data[10] != 0 || data[11] != 0 || data[12] > 1)
                return GTOS_PNG_CORRUPT;
            parsed.color_type = (unsigned char)type;
            header = 1;
            break;
        case UINT32_C(0x504C5445): /* PLTE */
            if (palette || transparency || idat || type == 0 || type == 4 ||
                length < 3 || length > 768 || length % 3)
                return GTOS_PNG_CORRUPT;
            parsed.palette_entries = length / 3;
            if (type == 3 && parsed.palette_entries > (1u << depth))
                return GTOS_PNG_CORRUPT;
            palette = 1;
            break;
        case UINT32_C(0x74524E53): /* tRNS: high sample bits are ignored by PNG3. */
            if (transparency || idat ||
                !((type == 0 && length == 2) || (type == 2 && length == 6) ||
                  (type == 3 && palette && length <= parsed.palette_entries)))
                return GTOS_PNG_CORRUPT;
            if (type == 0) {
                const uint32_t mask = (1u << depth) - 1u;
                const uint32_t sample = (((uint32_t)data[0] << 8) | data[1]) & mask;
                const unsigned char gray = (unsigned char)(depth == 16 ?
                    sample >> 8 : (sample * 255u) / mask);
                parsed.transparent_bgra[0] = gray;
                parsed.transparent_bgra[1] = gray;
                parsed.transparent_bgra[2] = gray;
                parsed.restore_transparent_rgb = 1;
            } else if (type == 2) {
                const unsigned low_byte = depth == 16 ? 0 : 1;
                parsed.transparent_bgra[0] = data[4 + low_byte];
                parsed.transparent_bgra[1] = data[2 + low_byte];
                parsed.transparent_bgra[2] = data[low_byte];
                parsed.restore_transparent_rgb = 1;
            }
            transparency = 1;
            break;
        case UINT32_C(0x49444154): /* IDAT */
            if (idat_ended || (type == 3 && !palette)) return GTOS_PNG_CORRUPT;
            idat = 1;
            if (length) parsed.last_nonempty_idat_end = end;
            break;
        case UINT32_C(0x49454E44): /* IEND */
            if (length || !idat || !parsed.last_nonempty_idat_end || end != bytes)
                return GTOS_PNG_CORRUPT;
            *envelope = parsed;
            return unsupported ? GTOS_PNG_UNSUPPORTED : GTOS_PNG_OK;
        case UINT32_C(0x6163544C): /* acTL */
        case UINT32_C(0x6663544C): /* fcTL */
        case UINT32_C(0x66644154): /* fdAT */
            unsupported = 1;
            break;
        default:
            if (!(chunk[4] & 32u)) unsupported = 1; /* Unknown critical extension. */
            break;
        }
        offset = end;
    }
    return GTOS_PNG_CORRUPT; /* IEND was absent. */
}

static gtos_png_status png_decode_status(wuffs_base__status status) {
    if (!status.repr) return GTOS_PNG_OK;
    if (status.repr == wuffs_png__error__unsupported_png_file ||
        status.repr == wuffs_png__error__unsupported_png_compression_method ||
        status.repr == wuffs_png__error__unsupported_cgbi_extension ||
        status.repr == wuffs_base__error__unsupported_option ||
        status.repr == wuffs_base__error__unsupported_pixel_swizzler_option)
        return GTOS_PNG_UNSUPPORTED;
    if (status.repr == wuffs_base__error__bad_argument ||
        status.repr == wuffs_base__error__bad_receiver ||
        status.repr == wuffs_base__error__bad_call_sequence ||
        status.repr == wuffs_base__error__bad_workbuf_length ||
        status.repr == wuffs_base__error__interleaved_coroutine_calls ||
        status.repr == wuffs_png__error__internal_error_inconsistent_i_o ||
        status.repr == wuffs_png__error__internal_error_inconsistent_chunk_type ||
        status.repr == wuffs_png__error__internal_error_inconsistent_frame_bounds ||
        status.repr == wuffs_png__error__internal_error_inconsistent_workbuf_length ||
        status.repr == wuffs_png__error__internal_error_zlib_decoder_did_not_exhaust_its_input)
        return GTOS_PNG_DECODER_FAILURE;
    /* Includes zlib dictionary requests: PNG forbids a preset dictionary. */
    return GTOS_PNG_CORRUPT;
}

static gtos_png_status png_prepare(void* context, const unsigned char* input,
    uint32_t input_bytes, png_envelope* envelope,
    gtos_png_requirements* requirements, wuffs_base__io_buffer* source) {
    gtos_png_status result = png_preflight(input, input_bytes, envelope);
    if (result) return result;
    wuffs_png__decoder* decoder = (wuffs_png__decoder*)context;
    wuffs_base__status status = wuffs_png__decoder__initialize(
        decoder, sizeof(*decoder), WUFFS_VERSION, 0);
    if (status.repr) return GTOS_PNG_DECODER_FAILURE;
    *source = wuffs_base__make_io_buffer(
        wuffs_base__make_slice_u8((uint8_t*)input, input_bytes),
        wuffs_base__make_io_buffer_meta(input_bytes, 0, 0, true));
    wuffs_base__image_config config = {0};
    status = wuffs_png__decoder__decode_image_config(decoder, &config, source);
    if (status.repr) return png_decode_status(status);
    if (wuffs_base__pixel_config__width(&config.pixcfg) != envelope->width ||
        wuffs_base__pixel_config__height(&config.pixcfg) != envelope->height)
        return GTOS_PNG_DECODER_FAILURE;
    const wuffs_base__range_ii_u64 work = wuffs_png__decoder__workbuf_len(decoder);
    const uint64_t row = (uint64_t)envelope->width * 4;
    const uint64_t pixels = row * envelope->height;
    if (work.min_incl != work.max_incl || !work.min_incl)
        return GTOS_PNG_DECODER_FAILURE;
    if (work.min_incl > UINT32_MAX || row > UINT32_MAX || pixels > UINT32_MAX)
        return GTOS_PNG_UNSUPPORTED;
    requirements->context_bytes = (uint32_t)sizeof(*decoder);
    requirements->work_bytes = (uint32_t)work.min_incl;
    requirements->width = envelope->width;
    requirements->height = envelope->height;
    requirements->row_bytes = (uint32_t)row;
    requirements->bgra_bytes = (uint32_t)pixels;
    return GTOS_PNG_OK;
}

uint32_t gtos_png_context_bytes(void) {
    return (uint32_t)sizeof(wuffs_png__decoder);
}

gtos_png_status gtos_png_inspect(void* context, uint32_t context_bytes,
    const unsigned char* input, uint32_t input_bytes,
    gtos_png_requirements* requirements) {
    png_region regions[3];
    gtos_png_status result = png_arguments(context, context_bytes, input,
                                         input_bytes, requirements, regions);
    if (result) return result;
    png_envelope envelope;
    gtos_png_requirements prepared;
    wuffs_base__io_buffer source;
    result = png_prepare(context, input, input_bytes, &envelope, &prepared, &source);
    if (result) return result;
    *requirements = prepared;
    return GTOS_PNG_OK;
}

gtos_png_status gtos_png_decode_bgra(void* context, uint32_t context_bytes,
    const unsigned char* input, uint32_t input_bytes,
    unsigned char* work, uint32_t work_bytes,
    unsigned char* bgra, uint32_t bgra_bytes, uint32_t bgra_stride,
    gtos_png_requirements* requirements) {
    png_region regions[5];
    gtos_png_status result = png_arguments(context, context_bytes, input,
                                         input_bytes, requirements, regions);
    if (result) return result;
    result = png_region_set(regions + 3, work, work_bytes);
    if (result) return result;
    result = png_region_set(regions + 4, bgra, bgra_bytes);
    if (result) return result;
    result = png_regions_disjoint(regions, 5);
    if (result) return result;
    png_envelope envelope;
    gtos_png_requirements prepared;
    wuffs_base__io_buffer source;
    result = png_prepare(context, input, input_bytes, &envelope, &prepared, &source);
    if (result) return result;
    if (bgra_stride < prepared.row_bytes) return GTOS_PNG_BAD_ARGUMENT;
    const uint64_t span = (uint64_t)(prepared.height - 1) * bgra_stride +
                          prepared.row_bytes;
    if (work_bytes < prepared.work_bytes || span > bgra_bytes)
        return GTOS_PNG_TOO_SMALL;
    unsigned char palette[1024];
    const int indexed = envelope.color_type == 3;
    wuffs_base__pixel_config pixels = {0};
    wuffs_base__pixel_config__set(&pixels,
        indexed ? WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_NONPREMUL :
                  WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL,
        WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, prepared.width, prepared.height);
    wuffs_base__pixel_buffer buffer = {0};
    wuffs_base__status status = wuffs_base__pixel_buffer__set_interleaved(
        &buffer, &pixels, wuffs_base__make_table_u8(bgra,
            indexed ? prepared.width : prepared.row_bytes,
            prepared.height, bgra_stride),
        wuffs_base__make_slice_u8(indexed ? palette : NULL, indexed ? 1024 : 0));
    if (status.repr) return GTOS_PNG_DECODER_FAILURE;
    wuffs_png__decoder* decoder = (wuffs_png__decoder*)context;
    status = wuffs_png__decoder__decode_frame(decoder, &buffer, &source,
        WUFFS_BASE__PIXEL_BLEND__SRC,
        wuffs_base__make_slice_u8(work, prepared.work_bytes), NULL);
    if (status.repr) return png_decode_status(status);
    if (source.meta.ri != envelope.last_nonempty_idat_end)
        return GTOS_PNG_CORRUPT;
    status = wuffs_png__decoder__decode_frame_config(decoder, NULL, &source);
    if (status.repr != wuffs_base__note__end_of_data)
        return status.repr ? png_decode_status(status) : GTOS_PNG_CORRUPT;
    if (source.meta.ri != input_bytes ||
        wuffs_png__decoder__num_decoded_frames(decoder) != 1)
        return GTOS_PNG_CORRUPT;
    if (indexed) {
        /* Validate all pixels before expansion, including reconstructed Adam7
         * indexes. Expanding each row backwards preserves unread index bytes. */
        for (uint32_t y = 0; y < prepared.height; ++y) {
            const unsigned char* row = bgra + (size_t)y * bgra_stride;
            for (uint32_t x = 0; x < prepared.width; ++x)
                if (row[x] >= envelope.palette_entries) return GTOS_PNG_CORRUPT;
        }
        for (uint32_t y = 0; y < prepared.height; ++y) {
            unsigned char* row = bgra + (size_t)y * bgra_stride;
            for (uint32_t x = prepared.width; x > 0; --x) {
                const unsigned char* color = palette + (size_t)row[x - 1] * 4;
                unsigned char* out = row + (size_t)(x - 1) * 4;
                out[0] = color[0]; out[1] = color[1];
                out[2] = color[2]; out[3] = color[3];
            }
        }
    }
    if (envelope.restore_transparent_rgb) {
        /* Pinned Wuffs clears RGB on gray/RGB tRNS matches. Those formats have
         * no intrinsic alpha: every zero-alpha pixel therefore had exactly the
         * masked/full-depth transparency key before Wuffs cleared its color.
         * Restore that source color after Wuffs has validated the full image;
         * retain its exact 16-bit match decision before high-byte conversion. */
        for (uint32_t y = 0; y < prepared.height; ++y) {
            unsigned char* row = bgra + (size_t)y * bgra_stride;
            for (uint32_t x = 0; x < prepared.width; ++x) {
                unsigned char* pixel = row + (size_t)x * 4;
                if (!pixel[3]) {
                    pixel[0] = envelope.transparent_bgra[0];
                    pixel[1] = envelope.transparent_bgra[1];
                    pixel[2] = envelope.transparent_bgra[2];
                }
            }
        }
    }
    *requirements = prepared;
    return GTOS_PNG_OK;
}
