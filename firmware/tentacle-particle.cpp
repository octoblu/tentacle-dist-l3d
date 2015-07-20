#include "tentacle-concat.h"
/* pb_arduino_decode.cpp -- Helper functions to decode from an Arduino Stream
 *
 * 2013 Alejandro Morell Garcia <alejandro.morell@gmail.com>
 */

//include file removed

// Make library cross-compatiable
// with Arduino, GNU C++ for tests, and Spark.
#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#include "Stream.h"
#elif defined(SPARK)
#include "application.h"
#include "spark_wiring_stream.h"
#endif


bool os_read(pb_istream_t *stream, uint8_t *buf, size_t count);

void pb_istream_from_stream(Stream &stream, pb_istream_t &istream) {
    istream.callback = &os_read;
    istream.state = &stream;
    istream.bytes_left = (size_t)-1;
#ifndef PB_NO_ERRMSG
    istream.errmsg = NULL;
#endif
}

bool os_read(pb_istream_t *stream, uint8_t *buf, size_t count) {
    Stream *s = static_cast<Stream *>(stream->state);
    while (count > 0) {
      size_t readCount = s->readBytes((char *)buf, count);
      count -= readCount;
    }
    return true;
}
/* pb_arduino_encode.h -- helper functions to encode to an Arduino Print object
 *
 * 2013 Alejandro Morell Garcia <alejandro.morell@gmail.com>
 */

//include file removed

bool os_write(pb_ostream_t *stream, const uint8_t *buf, size_t count);

void pb_ostream_from_stream(Print &stream, pb_ostream_t &ostream) {
    ostream.callback = &os_write;
    ostream.state = &stream;
    ostream.max_size = (size_t)-1;
    ostream.bytes_written = 0;
#ifndef PB_NO_ERRMSG
    ostream.errmsg = NULL;
#endif
}

bool os_write(pb_ostream_t *stream, const uint8_t *buf, size_t count) {
    if (stream == NULL || buf == NULL) {
        return false;
    }

    Print *s = static_cast<Print *>(stream->state);
    return (s->write(buf, count) == count);
}
/* pb_common.c: Common support functions for pb_encode.c and pb_decode.c.
 *
 * 2014 Petteri Aimonen <jpa@kapsi.fi>
 */

//include file removed
#ifdef __cplusplus
extern "C" {
#endif

bool pb_field_iter_begin(pb_field_iter_t *iter, const pb_field_t *fields, void *dest_struct)
{
    iter->start = fields;
    iter->pos = fields;
    iter->required_field_index = 0;
    iter->dest_struct = dest_struct;
    iter->pData = (char*)dest_struct + iter->pos->data_offset;
    iter->pSize = (char*)iter->pData + iter->pos->size_offset;

    return (iter->pos->tag != 0);
}

bool pb_field_iter_next(pb_field_iter_t *iter)
{
    const pb_field_t *prev_field = iter->pos;

    if (prev_field->tag == 0)
    {
        /* Handle empty message types, where the first field is already the terminator.
         * In other cases, the iter->pos never points to the terminator. */
        return false;
    }

    iter->pos++;

    if (iter->pos->tag == 0)
    {
        /* Wrapped back to beginning, reinitialize */
        (void)pb_field_iter_begin(iter, iter->start, iter->dest_struct);
        return false;
    }
    else
    {
        /* Increment the pointers based on previous field size */
        size_t prev_size = prev_field->data_size;

        if (PB_HTYPE(prev_field->type) == PB_HTYPE_ONEOF &&
            PB_HTYPE(iter->pos->type) == PB_HTYPE_ONEOF)
        {
            /* Don't advance pointers inside unions */
            prev_size = 0;
            iter->pData = (char*)iter->pData - prev_field->data_offset;
        }
        else if (PB_ATYPE(prev_field->type) == PB_ATYPE_STATIC &&
                 PB_HTYPE(prev_field->type) == PB_HTYPE_REPEATED)
        {
            /* In static arrays, the data_size tells the size of a single entry and
             * array_size is the number of entries */
            prev_size *= prev_field->array_size;
        }
        else if (PB_ATYPE(prev_field->type) == PB_ATYPE_POINTER)
        {
            /* Pointer fields always have a constant size in the main structure.
             * The data_size only applies to the dynamically allocated area. */
            prev_size = sizeof(void*);
        }

        if (PB_HTYPE(prev_field->type) == PB_HTYPE_REQUIRED)
        {
            /* Count the required fields, in order to check their presence in the
             * decoder. */
            iter->required_field_index++;
        }

        iter->pData = (char*)iter->pData + prev_size + iter->pos->data_offset;
        iter->pSize = (char*)iter->pData + iter->pos->size_offset;
        return true;
    }
}

bool pb_field_iter_find(pb_field_iter_t *iter, uint32_t tag)
{
    const pb_field_t *start = iter->pos;

    do {
        if (iter->pos->tag == tag &&
            PB_LTYPE(iter->pos->type) != PB_LTYPE_EXTENSION)
        {
            /* Found the wanted field */
            return true;
        }

        (void)pb_field_iter_next(iter);
    } while (iter->pos != start);

    /* Searched all the way back to start, and found nothing. */
    return false;
}

#ifdef __cplusplus
}
#endif
/* pb_decode.c -- decode a protobuf using minimal resources
 *
 * 2011 Petteri Aimonen <jpa@kapsi.fi>
 */

/* Use the GCC warn_unused_result attribute to check that all return values
 * are propagated correctly. On other compilers and gcc before 3.4.0 just
 * ignore the annotation.
 */
#if !defined(__GNUC__) || ( __GNUC__ < 3) || (__GNUC__ == 3 && __GNUC_MINOR__ < 4)
    #define checkreturn
#else
    #define checkreturn __attribute__((warn_unused_result))
#endif

//include file removed
//include file removed
//include file removed

#ifdef __cplusplus
extern "C" {
#endif

/**************************************
 * Declarations internal to this file *
 **************************************/

typedef bool (*pb_decoder_t)(pb_istream_t *stream, const pb_field_t *field, void *dest) checkreturn;

static bool checkreturn buf_read(pb_istream_t *stream, uint8_t *buf, size_t count);
static bool checkreturn pb_decode_varint32(pb_istream_t *stream, uint32_t *dest);
static bool checkreturn read_raw_value(pb_istream_t *stream, pb_wire_type_t wire_type, uint8_t *buf, size_t *size);
static bool checkreturn decode_static_field(pb_istream_t *stream, pb_wire_type_t wire_type, pb_field_iter_t *iter);
static bool checkreturn decode_callback_field(pb_istream_t *stream, pb_wire_type_t wire_type, pb_field_iter_t *iter);
static bool checkreturn decode_field(pb_istream_t *stream, pb_wire_type_t wire_type, pb_field_iter_t *iter);
static void iter_from_extension(pb_field_iter_t *iter, pb_extension_t *extension);
static bool checkreturn default_extension_decoder(pb_istream_t *stream, pb_extension_t *extension, uint32_t tag, pb_wire_type_t wire_type);
static bool checkreturn decode_extension(pb_istream_t *stream, uint32_t tag, pb_wire_type_t wire_type, pb_field_iter_t *iter);
static bool checkreturn find_extension_field(pb_field_iter_t *iter);
static void pb_field_set_to_default(pb_field_iter_t *iter);
static void pb_message_set_to_defaults(const pb_field_t fields[], void *dest_struct);
static bool checkreturn pb_dec_varint(pb_istream_t *stream, const pb_field_t *field, void *dest);
static bool checkreturn pb_dec_uvarint(pb_istream_t *stream, const pb_field_t *field, void *dest);
static bool checkreturn pb_dec_svarint(pb_istream_t *stream, const pb_field_t *field, void *dest);
static bool checkreturn pb_dec_fixed32(pb_istream_t *stream, const pb_field_t *field, void *dest);
static bool checkreturn pb_dec_fixed64(pb_istream_t *stream, const pb_field_t *field, void *dest);
static bool checkreturn pb_dec_bytes(pb_istream_t *stream, const pb_field_t *field, void *dest);
static bool checkreturn pb_dec_string(pb_istream_t *stream, const pb_field_t *field, void *dest);
static bool checkreturn pb_dec_submessage(pb_istream_t *stream, const pb_field_t *field, void *dest);
static bool checkreturn pb_skip_varint(pb_istream_t *stream);
static bool checkreturn pb_skip_string(pb_istream_t *stream);

#ifdef PB_ENABLE_MALLOC
static bool checkreturn allocate_field(pb_istream_t *stream, void *pData, size_t data_size, size_t array_size);
static bool checkreturn pb_release_union_field(pb_istream_t *stream, pb_field_iter_t *iter);
static void pb_release_single_field(const pb_field_iter_t *iter);
#endif

/* --- Function pointers to field decoders ---
 * Order in the array must match pb_action_t LTYPE numbering.
 */
static const pb_decoder_t PB_DECODERS[PB_LTYPES_COUNT] = {
    &pb_dec_varint,
    &pb_dec_uvarint,
    &pb_dec_svarint,
    &pb_dec_fixed32,
    &pb_dec_fixed64,

    &pb_dec_bytes,
    &pb_dec_string,
    &pb_dec_submessage,
    NULL /* extensions */
};

/*******************************
 * pb_istream_t implementation *
 *******************************/

static bool checkreturn buf_read(pb_istream_t *stream, uint8_t *buf, size_t count)
{
    uint8_t *source = (uint8_t*)stream->state;
    stream->state = source + count;

    if (buf != NULL)
    {
        while (count--)
            *buf++ = *source++;
    }

    return true;
}

bool checkreturn pb_read(pb_istream_t *stream, uint8_t *buf, size_t count)
{
#ifndef PB_BUFFER_ONLY
	if (buf == NULL && stream->callback != buf_read)
	{
		/* Skip input bytes */
		uint8_t tmp[16];
		while (count > 16)
		{
			if (!pb_read(stream, tmp, 16))
				return false;

			count -= 16;
		}

		return pb_read(stream, tmp, count);
	}
#endif

    if (stream->bytes_left < count)
        PB_RETURN_ERROR(stream, "end-of-stream");

#ifndef PB_BUFFER_ONLY
    if (!stream->callback(stream, buf, count))
        PB_RETURN_ERROR(stream, "io error");
#else
    if (!buf_read(stream, buf, count))
        return false;
#endif

    stream->bytes_left -= count;
    return true;
}

/* Read a single byte from input stream. buf may not be NULL.
 * This is an optimization for the varint decoding. */
static bool checkreturn pb_readbyte(pb_istream_t *stream, uint8_t *buf)
{
    if (stream->bytes_left == 0)
        PB_RETURN_ERROR(stream, "end-of-stream");

#ifndef PB_BUFFER_ONLY
    if (!stream->callback(stream, buf, 1))
        PB_RETURN_ERROR(stream, "io error");
#else
    *buf = *(uint8_t*)stream->state;
    stream->state = (uint8_t*)stream->state + 1;
#endif

    stream->bytes_left--;

    return true;
}

pb_istream_t pb_istream_from_buffer(uint8_t *buf, size_t bufsize)
{
    pb_istream_t stream;
#ifdef PB_BUFFER_ONLY
    stream.callback = NULL;
#else
    stream.callback = &buf_read;
#endif
    stream.state = buf;
    stream.bytes_left = bufsize;
#ifndef PB_NO_ERRMSG
    stream.errmsg = NULL;
#endif
    return stream;
}

/********************
 * Helper functions *
 ********************/

static bool checkreturn pb_decode_varint32(pb_istream_t *stream, uint32_t *dest)
{
    uint8_t byte;
    uint32_t result;

    if (!pb_readbyte(stream, &byte))
        return false;

    if ((byte & 0x80) == 0)
    {
        /* Quick case, 1 byte value */
        result = byte;
    }
    else
    {
        /* Multibyte case */
        uint8_t bitpos = 7;
        result = byte & 0x7F;

        do
        {
            if (bitpos >= 32)
                PB_RETURN_ERROR(stream, "varint overflow");

            if (!pb_readbyte(stream, &byte))
                return false;

            result |= (uint32_t)(byte & 0x7F) << bitpos;
            bitpos = (uint8_t)(bitpos + 7);
        } while (byte & 0x80);
   }

   *dest = result;
   return true;
}

bool checkreturn pb_decode_varint(pb_istream_t *stream, uint64_t *dest)
{
    uint8_t byte;
    uint8_t bitpos = 0;
    uint64_t result = 0;

    do
    {
        if (bitpos >= 64)
            PB_RETURN_ERROR(stream, "varint overflow");

        if (!pb_readbyte(stream, &byte))
            return false;

        result |= (uint64_t)(byte & 0x7F) << bitpos;
        bitpos = (uint8_t)(bitpos + 7);
    } while (byte & 0x80);

    *dest = result;
    return true;
}

bool checkreturn pb_skip_varint(pb_istream_t *stream)
{
    uint8_t byte;
    do
    {
        if (!pb_read(stream, &byte, 1))
            return false;
    } while (byte & 0x80);
    return true;
}

bool checkreturn pb_skip_string(pb_istream_t *stream)
{
    uint32_t length;
    if (!pb_decode_varint32(stream, &length))
        return false;

    return pb_read(stream, NULL, length);
}

bool checkreturn pb_decode_tag(pb_istream_t *stream, pb_wire_type_t *wire_type, uint32_t *tag, bool *eof)
{
    uint32_t temp;
    *eof = false;
    *wire_type = (pb_wire_type_t) 0;
    *tag = 0;

    if (!pb_decode_varint32(stream, &temp))
    {
        if (stream->bytes_left == 0)
            *eof = true;

        return false;
    }

    if (temp == 0)
    {
        *eof = true; /* Special feature: allow 0-terminated messages. */
        return false;
    }

    *tag = temp >> 3;
    *wire_type = (pb_wire_type_t)(temp & 7);
    return true;
}

bool checkreturn pb_skip_field(pb_istream_t *stream, pb_wire_type_t wire_type)
{
    switch (wire_type)
    {
        case PB_WT_VARINT: return pb_skip_varint(stream);
        case PB_WT_64BIT: return pb_read(stream, NULL, 8);
        case PB_WT_STRING: return pb_skip_string(stream);
        case PB_WT_32BIT: return pb_read(stream, NULL, 4);
        default: PB_RETURN_ERROR(stream, "invalid wire_type");
    }
}

/* Read a raw value to buffer, for the purpose of passing it to callback as
 * a substream. Size is maximum size on call, and actual size on return.
 */
static bool checkreturn read_raw_value(pb_istream_t *stream, pb_wire_type_t wire_type, uint8_t *buf, size_t *size)
{
    size_t max_size = *size;
    switch (wire_type)
    {
        case PB_WT_VARINT:
            *size = 0;
            do
            {
                (*size)++;
                if (*size > max_size) return false;
                if (!pb_read(stream, buf, 1)) return false;
            } while (*buf++ & 0x80);
            return true;

        case PB_WT_64BIT:
            *size = 8;
            return pb_read(stream, buf, 8);

        case PB_WT_32BIT:
            *size = 4;
            return pb_read(stream, buf, 4);

        default: PB_RETURN_ERROR(stream, "invalid wire_type");
    }
}

/* Decode string length from stream and return a substream with limited length.
 * Remember to close the substream using pb_close_string_substream().
 */
bool checkreturn pb_make_string_substream(pb_istream_t *stream, pb_istream_t *substream)
{
    uint32_t size;
    if (!pb_decode_varint32(stream, &size))
        return false;

    *substream = *stream;
    if (substream->bytes_left < size)
        PB_RETURN_ERROR(stream, "parent stream too short");

    substream->bytes_left = size;
    stream->bytes_left -= size;
    return true;
}

void pb_close_string_substream(pb_istream_t *stream, pb_istream_t *substream)
{
    stream->state = substream->state;

#ifndef PB_NO_ERRMSG
    stream->errmsg = substream->errmsg;
#endif
}

/*************************
 * Decode a single field *
 *************************/

static bool checkreturn decode_static_field(pb_istream_t *stream, pb_wire_type_t wire_type, pb_field_iter_t *iter)
{
    pb_type_t type;
    pb_decoder_t func;

    type = iter->pos->type;
    func = PB_DECODERS[PB_LTYPE(type)];

    switch (PB_HTYPE(type))
    {
        case PB_HTYPE_REQUIRED:
            return func(stream, iter->pos, iter->pData);

        case PB_HTYPE_OPTIONAL:
            *(bool*)iter->pSize = true;
            return func(stream, iter->pos, iter->pData);

        case PB_HTYPE_REPEATED:
            if (wire_type == PB_WT_STRING
                && PB_LTYPE(type) <= PB_LTYPE_LAST_PACKABLE)
            {
                /* Packed array */
                bool status = true;
                pb_size_t *size = (pb_size_t*)iter->pSize;
                pb_istream_t substream;
                if (!pb_make_string_substream(stream, &substream))
                    return false;

                while (substream.bytes_left > 0 && *size < iter->pos->array_size)
                {
                    void *pItem = (uint8_t*)iter->pData + iter->pos->data_size * (*size);
                    if (!func(&substream, iter->pos, pItem))
                    {
                        status = false;
                        break;
                    }
                    (*size)++;
                }
                pb_close_string_substream(stream, &substream);

                if (substream.bytes_left != 0)
                    PB_RETURN_ERROR(stream, "array overflow");

                return status;
            }
            else
            {
                /* Repeated field */
                pb_size_t *size = (pb_size_t*)iter->pSize;
                void *pItem = (uint8_t*)iter->pData + iter->pos->data_size * (*size);
                if (*size >= iter->pos->array_size)
                    PB_RETURN_ERROR(stream, "array overflow");

                (*size)++;
                return func(stream, iter->pos, pItem);
            }

        case PB_HTYPE_ONEOF:
            *(pb_size_t*)iter->pSize = iter->pos->tag;
            if (PB_LTYPE(type) == PB_LTYPE_SUBMESSAGE)
            {
                /* We memset to zero so that any callbacks are set to NULL.
                 * Then set any default values. */
                memset(iter->pData, 0, iter->pos->data_size);
                pb_message_set_to_defaults((const pb_field_t*)iter->pos->ptr, iter->pData);
            }
            return func(stream, iter->pos, iter->pData);

        default:
            PB_RETURN_ERROR(stream, "invalid field type");
    }
}

#ifdef PB_ENABLE_MALLOC
/* Allocate storage for the field and store the pointer at iter->pData.
 * array_size is the number of entries to reserve in an array.
 * Zero size is not allowed, use pb_free() for releasing.
 */
static bool checkreturn allocate_field(pb_istream_t *stream, void *pData, size_t data_size, size_t array_size)
{
    void *ptr = *(void**)pData;

    if (data_size == 0 || array_size == 0)
        PB_RETURN_ERROR(stream, "invalid size");

    /* Check for multiplication overflows.
     * This code avoids the costly division if the sizes are small enough.
     * Multiplication is safe as long as only half of bits are set
     * in either multiplicand.
     */
    {
        const size_t check_limit = (size_t)1 << (sizeof(size_t) * 4);
        if (data_size >= check_limit || array_size >= check_limit)
        {
            const size_t size_max = (size_t)-1;
            if (size_max / array_size < data_size)
            {
                PB_RETURN_ERROR(stream, "size too large");
            }
        }
    }

    /* Allocate new or expand previous allocation */
    /* Note: on failure the old pointer will remain in the structure,
     * the message must be freed by caller also on error return. */
    ptr = pb_realloc(ptr, array_size * data_size);
    if (ptr == NULL)
        PB_RETURN_ERROR(stream, "realloc failed");

    *(void**)pData = ptr;
    return true;
}

/* Clear a newly allocated item in case it contains a pointer, or is a submessage. */
static void initialize_pointer_field(void *pItem, pb_field_iter_t *iter)
{
    if (PB_LTYPE(iter->pos->type) == PB_LTYPE_STRING ||
        PB_LTYPE(iter->pos->type) == PB_LTYPE_BYTES)
    {
        *(void**)pItem = NULL;
    }
    else if (PB_LTYPE(iter->pos->type) == PB_LTYPE_SUBMESSAGE)
    {
        pb_message_set_to_defaults((const pb_field_t *) iter->pos->ptr, pItem);
    }
}
#endif

static bool checkreturn decode_pointer_field(pb_istream_t *stream, pb_wire_type_t wire_type, pb_field_iter_t *iter)
{
#ifndef PB_ENABLE_MALLOC
    PB_UNUSED(wire_type);
    PB_UNUSED(iter);
    PB_RETURN_ERROR(stream, "no malloc support");
#else
    pb_type_t type;
    pb_decoder_t func;

    type = iter->pos->type;
    func = PB_DECODERS[PB_LTYPE(type)];

    switch (PB_HTYPE(type))
    {
        case PB_HTYPE_REQUIRED:
        case PB_HTYPE_OPTIONAL:
        case PB_HTYPE_ONEOF:
            if (PB_LTYPE(type) == PB_LTYPE_SUBMESSAGE &&
                *(void**)iter->pData != NULL)
            {
                /* Duplicate field, have to release the old allocation first. */
                pb_release_single_field(iter);
            }

            if (PB_HTYPE(type) == PB_HTYPE_ONEOF)
            {
                *(pb_size_t*)iter->pSize = iter->pos->tag;
            }

            if (PB_LTYPE(type) == PB_LTYPE_STRING ||
                PB_LTYPE(type) == PB_LTYPE_BYTES)
            {
                return func(stream, iter->pos, iter->pData);
            }
            else
            {
                if (!allocate_field(stream, iter->pData, iter->pos->data_size, 1))
                    return false;

                initialize_pointer_field(*(void**)iter->pData, iter);
                return func(stream, iter->pos, *(void**)iter->pData);
            }

        case PB_HTYPE_REPEATED:
            if (wire_type == PB_WT_STRING
                && PB_LTYPE(type) <= PB_LTYPE_LAST_PACKABLE)
            {
                /* Packed array, multiple items come in at once. */
                bool status = true;
                pb_size_t *size = (pb_size_t*)iter->pSize;
                size_t allocated_size = *size;
                void *pItem;
                pb_istream_t substream;

                if (!pb_make_string_substream(stream, &substream))
                    return false;

                while (substream.bytes_left)
                {
                    if ((size_t)*size + 1 > allocated_size)
                    {
                        /* Allocate more storage. This tries to guess the
                         * number of remaining entries. Round the division
                         * upwards. */
                        allocated_size += (substream.bytes_left - 1) / iter->pos->data_size + 1;

                        if (!allocate_field(&substream, iter->pData, iter->pos->data_size, allocated_size))
                        {
                            status = false;
                            break;
                        }
                    }

                    /* Decode the array entry */
                    pItem = *(uint8_t**)iter->pData + iter->pos->data_size * (*size);
                    initialize_pointer_field(pItem, iter);
                    if (!func(&substream, iter->pos, pItem))
                    {
                        status = false;
                        break;
                    }

                    if (*size == PB_SIZE_MAX)
                    {
#ifndef PB_NO_ERRMSG
                        stream->errmsg = "too many array entries";
#endif
                        status = false;
                        break;
                    }

                    (*size)++;
                }
                pb_close_string_substream(stream, &substream);

                return status;
            }
            else
            {
                /* Normal repeated field, i.e. only one item at a time. */
                pb_size_t *size = (pb_size_t*)iter->pSize;
                void *pItem;

                if (*size == PB_SIZE_MAX)
                    PB_RETURN_ERROR(stream, "too many array entries");

                (*size)++;
                if (!allocate_field(stream, iter->pData, iter->pos->data_size, *size))
                    return false;

                pItem = *(uint8_t**)iter->pData + iter->pos->data_size * (*size - 1);
                initialize_pointer_field(pItem, iter);
                return func(stream, iter->pos, pItem);
            }

        default:
            PB_RETURN_ERROR(stream, "invalid field type");
    }
#endif
}

static bool checkreturn decode_callback_field(pb_istream_t *stream, pb_wire_type_t wire_type, pb_field_iter_t *iter)
{
    pb_callback_t *pCallback = (pb_callback_t*)iter->pData;

#ifdef PB_OLD_CALLBACK_STYLE
    void *arg = pCallback->arg;
#else
    void **arg = &(pCallback->arg);
#endif

    if (pCallback->funcs.decode == NULL)
        return pb_skip_field(stream, wire_type);

    if (wire_type == PB_WT_STRING)
    {
        pb_istream_t substream;

        if (!pb_make_string_substream(stream, &substream))
            return false;

        do
        {
            if (!pCallback->funcs.decode(&substream, iter->pos, arg))
                PB_RETURN_ERROR(stream, "callback failed");
        } while (substream.bytes_left);

        pb_close_string_substream(stream, &substream);
        return true;
    }
    else
    {
        /* Copy the single scalar value to stack.
         * This is required so that we can limit the stream length,
         * which in turn allows to use same callback for packed and
         * not-packed fields. */
        pb_istream_t substream;
        uint8_t buffer[10];
        size_t size = sizeof(buffer);

        if (!read_raw_value(stream, wire_type, buffer, &size))
            return false;
        substream = pb_istream_from_buffer(buffer, size);

        return pCallback->funcs.decode(&substream, iter->pos, arg);
    }
}

static bool checkreturn decode_field(pb_istream_t *stream, pb_wire_type_t wire_type, pb_field_iter_t *iter)
{
#ifdef PB_ENABLE_MALLOC
    /* When decoding an oneof field, check if there is old data that must be
     * released first. */
    if (PB_HTYPE(iter->pos->type) == PB_HTYPE_ONEOF)
    {
        if (!pb_release_union_field(stream, iter))
            return false;
    }
#endif

    switch (PB_ATYPE(iter->pos->type))
    {
        case PB_ATYPE_STATIC:
            return decode_static_field(stream, wire_type, iter);

        case PB_ATYPE_POINTER:
            return decode_pointer_field(stream, wire_type, iter);

        case PB_ATYPE_CALLBACK:
            return decode_callback_field(stream, wire_type, iter);

        default:
            PB_RETURN_ERROR(stream, "invalid field type");
    }
}

static void iter_from_extension(pb_field_iter_t *iter, pb_extension_t *extension)
{
    /* Fake a field iterator for the extension field.
     * It is not actually safe to advance this iterator, but decode_field
     * will not even try to. */
    const pb_field_t *field = (const pb_field_t*)extension->type->arg;
    (void)pb_field_iter_begin(iter, field, extension->dest);
    iter->pData = extension->dest;
    iter->pSize = &extension->found;

    if (PB_ATYPE(field->type) == PB_ATYPE_POINTER)
    {
        /* For pointer extensions, the pointer is stored directly
         * in the extension structure. This avoids having an extra
         * indirection. */
        iter->pData = &extension->dest;
    }
}

/* Default handler for extension fields. Expects a pb_field_t structure
 * in extension->type->arg. */
static bool checkreturn default_extension_decoder(pb_istream_t *stream,
    pb_extension_t *extension, uint32_t tag, pb_wire_type_t wire_type)
{
    const pb_field_t *field = (const pb_field_t*)extension->type->arg;
    pb_field_iter_t iter;

    if (field->tag != tag)
        return true;

    iter_from_extension(&iter, extension);
    extension->found = true;
    return decode_field(stream, wire_type, &iter);
}

/* Try to decode an unknown field as an extension field. Tries each extension
 * decoder in turn, until one of them handles the field or loop ends. */
static bool checkreturn decode_extension(pb_istream_t *stream,
    uint32_t tag, pb_wire_type_t wire_type, pb_field_iter_t *iter)
{
    pb_extension_t *extension = *(pb_extension_t* const *)iter->pData;
    size_t pos = stream->bytes_left;

    while (extension != NULL && pos == stream->bytes_left)
    {
        bool status;
        if (extension->type->decode)
            status = extension->type->decode(stream, extension, tag, wire_type);
        else
            status = default_extension_decoder(stream, extension, tag, wire_type);

        if (!status)
            return false;

        extension = extension->next;
    }

    return true;
}

/* Step through the iterator until an extension field is found or until all
 * entries have been checked. There can be only one extension field per
 * message. Returns false if no extension field is found. */
static bool checkreturn find_extension_field(pb_field_iter_t *iter)
{
    const pb_field_t *start = iter->pos;

    do {
        if (PB_LTYPE(iter->pos->type) == PB_LTYPE_EXTENSION)
            return true;
        (void)pb_field_iter_next(iter);
    } while (iter->pos != start);

    return false;
}

/* Initialize message fields to default values, recursively */
static void pb_field_set_to_default(pb_field_iter_t *iter)
{
    pb_type_t type;
    type = iter->pos->type;

    if (PB_LTYPE(type) == PB_LTYPE_EXTENSION)
    {
        pb_extension_t *ext = *(pb_extension_t* const *)iter->pData;
        while (ext != NULL)
        {
            pb_field_iter_t ext_iter;
            ext->found = false;
            iter_from_extension(&ext_iter, ext);
            pb_field_set_to_default(&ext_iter);
            ext = ext->next;
        }
    }
    else if (PB_ATYPE(type) == PB_ATYPE_STATIC)
    {
        bool init_data = true;
        if (PB_HTYPE(type) == PB_HTYPE_OPTIONAL)
        {
            /* Set has_field to false. Still initialize the optional field
             * itself also. */
            *(bool*)iter->pSize = false;
        }
        else if (PB_HTYPE(type) == PB_HTYPE_REPEATED ||
                 PB_HTYPE(type) == PB_HTYPE_ONEOF)
        {
            /* REPEATED: Set array count to 0, no need to initialize contents.
               ONEOF: Set which_field to 0. */
            *(pb_size_t*)iter->pSize = 0;
            init_data = false;
        }

        if (init_data)
        {
            if (PB_LTYPE(iter->pos->type) == PB_LTYPE_SUBMESSAGE)
            {
                /* Initialize submessage to defaults */
                pb_message_set_to_defaults((const pb_field_t *) iter->pos->ptr, iter->pData);
            }
            else if (iter->pos->ptr != NULL)
            {
                /* Initialize to default value */
                memcpy(iter->pData, iter->pos->ptr, iter->pos->data_size);
            }
            else
            {
                /* Initialize to zeros */
                memset(iter->pData, 0, iter->pos->data_size);
            }
        }
    }
    else if (PB_ATYPE(type) == PB_ATYPE_POINTER)
    {
        /* Initialize the pointer to NULL. */
        *(void**)iter->pData = NULL;

        /* Initialize array count to 0. */
        if (PB_HTYPE(type) == PB_HTYPE_REPEATED ||
            PB_HTYPE(type) == PB_HTYPE_ONEOF)
        {
            *(pb_size_t*)iter->pSize = 0;
        }
    }
    else if (PB_ATYPE(type) == PB_ATYPE_CALLBACK)
    {
        /* Don't overwrite callback */
    }
}

static void pb_message_set_to_defaults(const pb_field_t fields[], void *dest_struct)
{
    pb_field_iter_t iter;

    if (!pb_field_iter_begin(&iter, fields, dest_struct))
        return; /* Empty message type */

    do
    {
        pb_field_set_to_default(&iter);
    } while (pb_field_iter_next(&iter));
}

/*********************
 * Decode all fields *
 *********************/

bool checkreturn pb_decode_noinit(pb_istream_t *stream, const pb_field_t fields[], void *dest_struct)
{
    uint8_t fields_seen[(PB_MAX_REQUIRED_FIELDS + 7) / 8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t extension_range_start = 0;
    pb_field_iter_t iter;

    /* Return value ignored, as empty message types will be correctly handled by
     * pb_field_iter_find() anyway. */
    (void)pb_field_iter_begin(&iter, fields, dest_struct);

    while (stream->bytes_left)
    {
        uint32_t tag;
        pb_wire_type_t wire_type;
        bool eof;

        if (!pb_decode_tag(stream, &wire_type, &tag, &eof))
        {
            if (eof)
                break;
            else
                return false;
        }

        if (!pb_field_iter_find(&iter, tag))
        {
            /* No match found, check if it matches an extension. */
            if (tag >= extension_range_start)
            {
                if (!find_extension_field(&iter))
                    extension_range_start = (uint32_t)-1;
                else
                    extension_range_start = iter.pos->tag;

                if (tag >= extension_range_start)
                {
                    size_t pos = stream->bytes_left;

                    if (!decode_extension(stream, tag, wire_type, &iter))
                        return false;

                    if (pos != stream->bytes_left)
                    {
                        /* The field was handled */
                        continue;
                    }
                }
            }

            /* No match found, skip data */
            if (!pb_skip_field(stream, wire_type))
                return false;
            continue;
        }

        if (PB_HTYPE(iter.pos->type) == PB_HTYPE_REQUIRED
            && iter.required_field_index < PB_MAX_REQUIRED_FIELDS)
        {
            fields_seen[iter.required_field_index >> 3] |= (uint8_t)(1 << (iter.required_field_index & 7));
        }

        if (!decode_field(stream, wire_type, &iter))
            return false;
    }

    /* Check that all required fields were present. */
    {
        /* First figure out the number of required fields by
         * seeking to the end of the field array. Usually we
         * are already close to end after decoding.
         */
        unsigned req_field_count;
        pb_type_t last_type;
        unsigned i;
        do {
            req_field_count = iter.required_field_index;
            last_type = iter.pos->type;
        } while (pb_field_iter_next(&iter));

        /* Fixup if last field was also required. */
        if (PB_HTYPE(last_type) == PB_HTYPE_REQUIRED && iter.pos->tag != 0)
            req_field_count++;

        /* Check the whole bytes */
        for (i = 0; i < (req_field_count >> 3); i++)
        {
            if (fields_seen[i] != 0xFF)
                PB_RETURN_ERROR(stream, "missing required field");
        }

        /* Check the remaining bits */
        if (fields_seen[req_field_count >> 3] != (0xFF >> (8 - (req_field_count & 7))))
            PB_RETURN_ERROR(stream, "missing required field");
    }

    return true;
}

bool checkreturn pb_decode(pb_istream_t *stream, const pb_field_t fields[], void *dest_struct)
{
    bool status;
    pb_message_set_to_defaults(fields, dest_struct);
    status = pb_decode_noinit(stream, fields, dest_struct);

#ifdef PB_ENABLE_MALLOC
    if (!status)
        pb_release(fields, dest_struct);
#endif

    return status;
}

bool pb_decode_delimited(pb_istream_t *stream, const pb_field_t fields[], void *dest_struct)
{
    pb_istream_t substream;
    bool status;

    if (!pb_make_string_substream(stream, &substream))
        return false;

    status = pb_decode(&substream, fields, dest_struct);
    pb_close_string_substream(stream, &substream);
    return status;
}

#ifdef PB_ENABLE_MALLOC
/* Given an oneof field, if there has already been a field inside this oneof,
 * release it before overwriting with a different one. */
static bool pb_release_union_field(pb_istream_t *stream, pb_field_iter_t *iter)
{
    pb_size_t old_tag = *(pb_size_t*)iter->pSize; /* Previous which_ value */
    pb_size_t new_tag = iter->pos->tag; /* New which_ value */

    if (old_tag == 0)
        return true; /* Ok, no old data in union */

    if (old_tag == new_tag)
        return true; /* Ok, old data is of same type => merge */

    /* Release old data. The find can fail if the message struct contains
     * invalid data. */
    if (!pb_field_iter_find(iter, old_tag))
        PB_RETURN_ERROR(stream, "invalid union tag");

    pb_release_single_field(iter);

    /* Restore iterator to where it should be.
     * This shouldn't fail unless the pb_field_t structure is corrupted. */
    if (!pb_field_iter_find(iter, new_tag))
        PB_RETURN_ERROR(stream, "iterator error");

    return true;
}

static void pb_release_single_field(const pb_field_iter_t *iter)
{
    pb_type_t type;
    type = iter->pos->type;

    if (PB_HTYPE(type) == PB_HTYPE_ONEOF)
    {
        if (*(pb_size_t*)iter->pSize != iter->pos->tag)
            return; /* This is not the current field in the union */
    }

    /* Release anything contained inside an extension or submsg.
     * This has to be done even if the submsg itself is statically
     * allocated. */
    if (PB_LTYPE(type) == PB_LTYPE_EXTENSION)
    {
        /* Release fields from all extensions in the linked list */
        pb_extension_t *ext = *(pb_extension_t**)iter->pData;
        while (ext != NULL)
        {
            pb_field_iter_t ext_iter;
            iter_from_extension(&ext_iter, ext);
            pb_release_single_field(&ext_iter);
            ext = ext->next;
        }
    }
    else if (PB_LTYPE(type) == PB_LTYPE_SUBMESSAGE)
    {
        /* Release fields in submessage or submsg array */
        void *pItem = iter->pData;
        pb_size_t count = 1;

        if (PB_ATYPE(type) == PB_ATYPE_POINTER)
        {
            pItem = *(void**)iter->pData;
        }

        if (PB_HTYPE(type) == PB_HTYPE_REPEATED)
        {
            count = *(pb_size_t*)iter->pSize;
        }

        if (pItem)
        {
            while (count--)
            {
                pb_release((const pb_field_t*)iter->pos->ptr, pItem);
                pItem = (uint8_t*)pItem + iter->pos->data_size;
            }
        }
    }

    if (PB_ATYPE(type) == PB_ATYPE_POINTER)
    {
        if (PB_HTYPE(type) == PB_HTYPE_REPEATED &&
            (PB_LTYPE(type) == PB_LTYPE_STRING ||
             PB_LTYPE(type) == PB_LTYPE_BYTES))
        {
            /* Release entries in repeated string or bytes array */
            void **pItem = *(void***)iter->pData;
            pb_size_t count = *(pb_size_t*)iter->pSize;
            while (count--)
            {
                pb_free(*pItem);
                *pItem++ = NULL;
            }
        }

        if (PB_HTYPE(type) == PB_HTYPE_REPEATED)
        {
            /* We are going to release the array, so set the size to 0 */
            *(pb_size_t*)iter->pSize = 0;
        }

        /* Release main item */
        pb_free(*(void**)iter->pData);
        *(void**)iter->pData = NULL;
    }
}

void pb_release(const pb_field_t fields[], void *dest_struct)
{
    pb_field_iter_t iter;

    if (!pb_field_iter_begin(&iter, fields, dest_struct))
        return; /* Empty message type */

    do
    {
        pb_release_single_field(&iter);
    } while (pb_field_iter_next(&iter));
}
#endif

/* Field decoders */

bool pb_decode_svarint(pb_istream_t *stream, int64_t *dest)
{
    uint64_t value;
    if (!pb_decode_varint(stream, &value))
        return false;

    if (value & 1)
        *dest = (int64_t)(~(value >> 1));
    else
        *dest = (int64_t)(value >> 1);

    return true;
}

bool pb_decode_fixed32(pb_istream_t *stream, void *dest)
{
    #ifdef __BIG_ENDIAN__
    uint8_t *bytes = (uint8_t*)dest;
    uint8_t lebytes[4];

    if (!pb_read(stream, lebytes, 4))
        return false;

    bytes[0] = lebytes[3];
    bytes[1] = lebytes[2];
    bytes[2] = lebytes[1];
    bytes[3] = lebytes[0];
    return true;
    #else
    return pb_read(stream, (uint8_t*)dest, 4);
    #endif
}

bool pb_decode_fixed64(pb_istream_t *stream, void *dest)
{
    #ifdef __BIG_ENDIAN__
    uint8_t *bytes = (uint8_t*)dest;
    uint8_t lebytes[8];

    if (!pb_read(stream, lebytes, 8))
        return false;

    bytes[0] = lebytes[7];
    bytes[1] = lebytes[6];
    bytes[2] = lebytes[5];
    bytes[3] = lebytes[4];
    bytes[4] = lebytes[3];
    bytes[5] = lebytes[2];
    bytes[6] = lebytes[1];
    bytes[7] = lebytes[0];
    return true;
    #else
    return pb_read(stream, (uint8_t*)dest, 8);
    #endif
}

static bool checkreturn pb_dec_varint(pb_istream_t *stream, const pb_field_t *field, void *dest)
{
    uint64_t value;
    int64_t svalue;
    int64_t clamped;
    if (!pb_decode_varint(stream, &value))
        return false;

    /* See issue 97: Google's C++ protobuf allows negative varint values to
     * be cast as int32_t, instead of the int64_t that should be used when
     * encoding. Previous nanopb versions had a bug in encoding. In order to
     * not break decoding of such messages, we cast <=32 bit fields to
     * int32_t first to get the sign correct.
     */
    if (field->data_size == 8)
        svalue = (int64_t)value;
    else
        svalue = (int32_t)value;

    switch (field->data_size)
    {
        case 1: clamped = *(int8_t*)dest = (int8_t)svalue; break;
        case 2: clamped = *(int16_t*)dest = (int16_t)svalue; break;
        case 4: clamped = *(int32_t*)dest = (int32_t)svalue; break;
        case 8: clamped = *(int64_t*)dest = svalue; break;
        default: PB_RETURN_ERROR(stream, "invalid data_size");
    }

    if (clamped != svalue)
        PB_RETURN_ERROR(stream, "integer too large");

    return true;
}

static bool checkreturn pb_dec_uvarint(pb_istream_t *stream, const pb_field_t *field, void *dest)
{
    uint64_t value, clamped;
    if (!pb_decode_varint(stream, &value))
        return false;

    switch (field->data_size)
    {
        case 1: clamped = *(uint8_t*)dest = (uint8_t)value; break;
        case 2: clamped = *(uint16_t*)dest = (uint16_t)value; break;
        case 4: clamped = *(uint32_t*)dest = (uint32_t)value; break;
        case 8: clamped = *(uint64_t*)dest = value; break;
        default: PB_RETURN_ERROR(stream, "invalid data_size");
    }

    if (clamped != value)
        PB_RETURN_ERROR(stream, "integer too large");

    return true;
}

static bool checkreturn pb_dec_svarint(pb_istream_t *stream, const pb_field_t *field, void *dest)
{
    int64_t value, clamped;
    if (!pb_decode_svarint(stream, &value))
        return false;

    switch (field->data_size)
    {
        case 1: clamped = *(int8_t*)dest = (int8_t)value; break;
        case 2: clamped = *(int16_t*)dest = (int16_t)value; break;
        case 4: clamped = *(int32_t*)dest = (int32_t)value; break;
        case 8: clamped = *(int64_t*)dest = value; break;
        default: PB_RETURN_ERROR(stream, "invalid data_size");
    }

    if (clamped != value)
        PB_RETURN_ERROR(stream, "integer too large");

    return true;
}

static bool checkreturn pb_dec_fixed32(pb_istream_t *stream, const pb_field_t *field, void *dest)
{
    PB_UNUSED(field);
    return pb_decode_fixed32(stream, dest);
}

static bool checkreturn pb_dec_fixed64(pb_istream_t *stream, const pb_field_t *field, void *dest)
{
    PB_UNUSED(field);
    return pb_decode_fixed64(stream, dest);
}

static bool checkreturn pb_dec_bytes(pb_istream_t *stream, const pb_field_t *field, void *dest)
{
    uint32_t size;
    size_t alloc_size;
    pb_bytes_array_t *bdest;

    if (!pb_decode_varint32(stream, &size))
        return false;

    if (size > PB_SIZE_MAX)
        PB_RETURN_ERROR(stream, "bytes overflow");

    alloc_size = PB_BYTES_ARRAY_T_ALLOCSIZE(size);
    if (size > alloc_size)
        PB_RETURN_ERROR(stream, "size too large");

    if (PB_ATYPE(field->type) == PB_ATYPE_POINTER)
    {
#ifndef PB_ENABLE_MALLOC
        PB_RETURN_ERROR(stream, "no malloc support");
#else
        if (!allocate_field(stream, dest, alloc_size, 1))
            return false;
        bdest = *(pb_bytes_array_t**)dest;
#endif
    }
    else
    {
        if (alloc_size > field->data_size)
            PB_RETURN_ERROR(stream, "bytes overflow");
        bdest = (pb_bytes_array_t*)dest;
    }

    bdest->size = (pb_size_t)size;
    return pb_read(stream, bdest->bytes, size);
}

static bool checkreturn pb_dec_string(pb_istream_t *stream, const pb_field_t *field, void *dest)
{
    uint32_t size;
    size_t alloc_size;
    bool status;
    if (!pb_decode_varint32(stream, &size))
        return false;

    /* Space for null terminator */
    alloc_size = size + 1;

    if (alloc_size < size)
        PB_RETURN_ERROR(stream, "size too large");

    if (PB_ATYPE(field->type) == PB_ATYPE_POINTER)
    {
#ifndef PB_ENABLE_MALLOC
        PB_RETURN_ERROR(stream, "no malloc support");
#else
        if (!allocate_field(stream, dest, alloc_size, 1))
            return false;
        dest = *(void**)dest;
#endif
    }
    else
    {
        if (alloc_size > field->data_size)
            PB_RETURN_ERROR(stream, "string overflow");
    }

    status = pb_read(stream, (uint8_t*)dest, size);
    *((uint8_t*)dest + size) = 0;
    return status;
}

static bool checkreturn pb_dec_submessage(pb_istream_t *stream, const pb_field_t *field, void *dest)
{
    bool status;
    pb_istream_t substream;
    const pb_field_t* submsg_fields = (const pb_field_t*)field->ptr;

    if (!pb_make_string_substream(stream, &substream))
        return false;

    if (field->ptr == NULL)
        PB_RETURN_ERROR(stream, "invalid field descriptor");

    /* New array entries need to be initialized, while required and optional
     * submessages have already been initialized in the top-level pb_decode. */
    if (PB_HTYPE(field->type) == PB_HTYPE_REPEATED)
        status = pb_decode(&substream, submsg_fields, dest);
    else
        status = pb_decode_noinit(&substream, submsg_fields, dest);

    pb_close_string_substream(stream, &substream);
    return status;
}

#ifdef __cplusplus
}
#endif
/* pb_encode.c -- encode a protobuf using minimal resources
 *
 * 2011 Petteri Aimonen <jpa@kapsi.fi>
 */

//include file removed
//include file removed
//include file removed

/* Use the GCC warn_unused_result attribute to check that all return values
 * are propagated correctly. On other compilers and gcc before 3.4.0 just
 * ignore the annotation.
 */
#if !defined(__GNUC__) || ( __GNUC__ < 3) || (__GNUC__ == 3 && __GNUC_MINOR__ < 4)
    #define checkreturn
#else
    #define checkreturn __attribute__((warn_unused_result))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**************************************
 * Declarations internal to this file *
 **************************************/
typedef bool (*pb_encoder_t)(pb_ostream_t *stream, const pb_field_t *field, const void *src) checkreturn;

static bool checkreturn buf_write(pb_ostream_t *stream, const uint8_t *buf, size_t count);
static bool checkreturn encode_array(pb_ostream_t *stream, const pb_field_t *field, const void *pData, size_t count, pb_encoder_t func);
static bool checkreturn encode_field(pb_ostream_t *stream, const pb_field_t *field, const void *pData);
static bool checkreturn default_extension_encoder(pb_ostream_t *stream, const pb_extension_t *extension);
static bool checkreturn encode_extension_field(pb_ostream_t *stream, const pb_field_t *field, const void *pData);
static bool checkreturn pb_enc_varint(pb_ostream_t *stream, const pb_field_t *field, const void *src);
static bool checkreturn pb_enc_uvarint(pb_ostream_t *stream, const pb_field_t *field, const void *src);
static bool checkreturn pb_enc_svarint(pb_ostream_t *stream, const pb_field_t *field, const void *src);
static bool checkreturn pb_enc_fixed32(pb_ostream_t *stream, const pb_field_t *field, const void *src);
static bool checkreturn pb_enc_fixed64(pb_ostream_t *stream, const pb_field_t *field, const void *src);
static bool checkreturn pb_enc_bytes(pb_ostream_t *stream, const pb_field_t *field, const void *src);
static bool checkreturn pb_enc_string(pb_ostream_t *stream, const pb_field_t *field, const void *src);
static bool checkreturn pb_enc_submessage(pb_ostream_t *stream, const pb_field_t *field, const void *src);

/* --- Function pointers to field encoders ---
 * Order in the array must match pb_action_t LTYPE numbering.
 */
static const pb_encoder_t PB_ENCODERS[PB_LTYPES_COUNT] = {
    &pb_enc_varint,
    &pb_enc_uvarint,
    &pb_enc_svarint,
    &pb_enc_fixed32,
    &pb_enc_fixed64,

    &pb_enc_bytes,
    &pb_enc_string,
    &pb_enc_submessage,
    NULL /* extensions */
};

/*******************************
 * pb_ostream_t implementation *
 *******************************/

static bool checkreturn buf_write(pb_ostream_t *stream, const uint8_t *buf, size_t count)
{
    uint8_t *dest = (uint8_t*)stream->state;
    stream->state = dest + count;

    while (count--)
        *dest++ = *buf++;

    return true;
}

pb_ostream_t pb_ostream_from_buffer(uint8_t *buf, size_t bufsize)
{
    pb_ostream_t stream;
#ifdef PB_BUFFER_ONLY
    stream.callback = (void*)1; /* Just a marker value */
#else
    stream.callback = &buf_write;
#endif
    stream.state = buf;
    stream.max_size = bufsize;
    stream.bytes_written = 0;
#ifndef PB_NO_ERRMSG
    stream.errmsg = NULL;
#endif
    return stream;
}

bool checkreturn pb_write(pb_ostream_t *stream, const uint8_t *buf, size_t count)
{
    if (stream->callback != NULL)
    {
        if (stream->bytes_written + count > stream->max_size)
            PB_RETURN_ERROR(stream, "stream full");

#ifdef PB_BUFFER_ONLY
        if (!buf_write(stream, buf, count))
            PB_RETURN_ERROR(stream, "io error");
#else
        if (!stream->callback(stream, buf, count))
            PB_RETURN_ERROR(stream, "io error");
#endif
    }

    stream->bytes_written += count;
    return true;
}

/*************************
 * Encode a single field *
 *************************/

/* Encode a static array. Handles the size calculations and possible packing. */
static bool checkreturn encode_array(pb_ostream_t *stream, const pb_field_t *field,
                         const void *pData, size_t count, pb_encoder_t func)
{
    size_t i;
    const void *p;
    size_t size;

    if (count == 0)
        return true;

    if (PB_ATYPE(field->type) != PB_ATYPE_POINTER && count > field->array_size)
        PB_RETURN_ERROR(stream, "array max size exceeded");

    /* We always pack arrays if the datatype allows it. */
    if (PB_LTYPE(field->type) <= PB_LTYPE_LAST_PACKABLE)
    {
        if (!pb_encode_tag(stream, PB_WT_STRING, field->tag))
            return false;

        /* Determine the total size of packed array. */
        if (PB_LTYPE(field->type) == PB_LTYPE_FIXED32)
        {
            size = 4 * count;
        }
        else if (PB_LTYPE(field->type) == PB_LTYPE_FIXED64)
        {
            size = 8 * count;
        }
        else
        {
            pb_ostream_t sizestream = PB_OSTREAM_SIZING;
            p = pData;
            for (i = 0; i < count; i++)
            {
                if (!func(&sizestream, field, p))
                    return false;
                p = (const char*)p + field->data_size;
            }
            size = sizestream.bytes_written;
        }

        if (!pb_encode_varint(stream, (uint64_t)size))
            return false;

        if (stream->callback == NULL)
            return pb_write(stream, NULL, size); /* Just sizing.. */

        /* Write the data */
        p = pData;
        for (i = 0; i < count; i++)
        {
            if (!func(stream, field, p))
                return false;
            p = (const char*)p + field->data_size;
        }
    }
    else
    {
        p = pData;
        for (i = 0; i < count; i++)
        {
            if (!pb_encode_tag_for_field(stream, field))
                return false;

            /* Normally the data is stored directly in the array entries, but
             * for pointer-type string and bytes fields, the array entries are
             * actually pointers themselves also. So we have to dereference once
             * more to get to the actual data. */
            if (PB_ATYPE(field->type) == PB_ATYPE_POINTER &&
                (PB_LTYPE(field->type) == PB_LTYPE_STRING ||
                 PB_LTYPE(field->type) == PB_LTYPE_BYTES))
            {
                if (!func(stream, field, *(const void* const*)p))
                    return false;
            }
            else
            {
                if (!func(stream, field, p))
                    return false;
            }
            p = (const char*)p + field->data_size;
        }
    }

    return true;
}

/* Encode a field with static or pointer allocation, i.e. one whose data
 * is available to the encoder directly. */
static bool checkreturn encode_basic_field(pb_ostream_t *stream,
    const pb_field_t *field, const void *pData)
{
    pb_encoder_t func;
    const void *pSize;
    bool implicit_has = true;

    func = PB_ENCODERS[PB_LTYPE(field->type)];

    if (field->size_offset)
        pSize = (const char*)pData + field->size_offset;
    else
        pSize = &implicit_has;

    if (PB_ATYPE(field->type) == PB_ATYPE_POINTER)
    {
        /* pData is a pointer to the field, which contains pointer to
         * the data. If the 2nd pointer is NULL, it is interpreted as if
         * the has_field was false.
         */

        pData = *(const void* const*)pData;
        implicit_has = (pData != NULL);
    }

    switch (PB_HTYPE(field->type))
    {
        case PB_HTYPE_REQUIRED:
            if (!pData)
                PB_RETURN_ERROR(stream, "missing required field");
            if (!pb_encode_tag_for_field(stream, field))
                return false;
            if (!func(stream, field, pData))
                return false;
            break;

        case PB_HTYPE_OPTIONAL:
            if (*(const bool*)pSize)
            {
                if (!pb_encode_tag_for_field(stream, field))
                    return false;

                if (!func(stream, field, pData))
                    return false;
            }
            break;

        case PB_HTYPE_REPEATED:
            if (!encode_array(stream, field, pData, *(const pb_size_t*)pSize, func))
                return false;
            break;

        case PB_HTYPE_ONEOF:
            if (*(const pb_size_t*)pSize == field->tag)
            {
                if (!pb_encode_tag_for_field(stream, field))
                    return false;

                if (!func(stream, field, pData))
                    return false;
            }
            break;

        default:
            PB_RETURN_ERROR(stream, "invalid field type");
    }

    return true;
}

/* Encode a field with callback semantics. This means that a user function is
 * called to provide and encode the actual data. */
static bool checkreturn encode_callback_field(pb_ostream_t *stream,
    const pb_field_t *field, const void *pData)
{
    const pb_callback_t *callback = (const pb_callback_t*)pData;

#ifdef PB_OLD_CALLBACK_STYLE
    const void *arg = callback->arg;
#else
    void * const *arg = &(callback->arg);
#endif

    if (callback->funcs.encode != NULL)
    {
        if (!callback->funcs.encode(stream, field, arg))
            PB_RETURN_ERROR(stream, "callback error");
    }
    return true;
}

/* Encode a single field of any callback or static type. */
static bool checkreturn encode_field(pb_ostream_t *stream,
    const pb_field_t *field, const void *pData)
{
    switch (PB_ATYPE(field->type))
    {
        case PB_ATYPE_STATIC:
        case PB_ATYPE_POINTER:
            return encode_basic_field(stream, field, pData);

        case PB_ATYPE_CALLBACK:
            return encode_callback_field(stream, field, pData);

        default:
            PB_RETURN_ERROR(stream, "invalid field type");
    }
}

/* Default handler for extension fields. Expects to have a pb_field_t
 * pointer in the extension->type->arg field. */
static bool checkreturn default_extension_encoder(pb_ostream_t *stream,
    const pb_extension_t *extension)
{
    const pb_field_t *field = (const pb_field_t*)extension->type->arg;

    if (PB_ATYPE(field->type) == PB_ATYPE_POINTER)
    {
        /* For pointer extensions, the pointer is stored directly
         * in the extension structure. This avoids having an extra
         * indirection. */
        return encode_field(stream, field, &extension->dest);
    }
    else
    {
        return encode_field(stream, field, extension->dest);
    }
}

/* Walk through all the registered extensions and give them a chance
 * to encode themselves. */
static bool checkreturn encode_extension_field(pb_ostream_t *stream,
    const pb_field_t *field, const void *pData)
{
    const pb_extension_t *extension = *(const pb_extension_t* const *)pData;
    PB_UNUSED(field);

    while (extension)
    {
        bool status;
        if (extension->type->encode)
            status = extension->type->encode(stream, extension);
        else
            status = default_extension_encoder(stream, extension);

        if (!status)
            return false;

        extension = extension->next;
    }

    return true;
}

/*********************
 * Encode all fields *
 *********************/

static void *remove_const(const void *p)
{
    /* Note: this casts away const, in order to use the common field iterator
     * logic for both encoding and decoding. */
    union {
        void *p1;
        const void *p2;
    } t;
    t.p2 = p;
    return t.p1;
}

bool checkreturn pb_encode(pb_ostream_t *stream, const pb_field_t fields[], const void *src_struct)
{
    pb_field_iter_t iter;
    if (!pb_field_iter_begin(&iter, fields, remove_const(src_struct)))
        return true; /* Empty message type */

    do {
        if (PB_LTYPE(iter.pos->type) == PB_LTYPE_EXTENSION)
        {
            /* Special case for the extension field placeholder */
            if (!encode_extension_field(stream, iter.pos, iter.pData))
                return false;
        }
        else
        {
            /* Regular field */
            if (!encode_field(stream, iter.pos, iter.pData))
                return false;
        }
    } while (pb_field_iter_next(&iter));

    return true;
}

bool pb_encode_delimited(pb_ostream_t *stream, const pb_field_t fields[], const void *src_struct)
{
    return pb_encode_submessage(stream, fields, src_struct);
}

bool pb_get_encoded_size(size_t *size, const pb_field_t fields[], const void *src_struct)
{
    pb_ostream_t stream = PB_OSTREAM_SIZING;

    if (!pb_encode(&stream, fields, src_struct))
        return false;

    *size = stream.bytes_written;
    return true;
}

/********************
 * Helper functions *
 ********************/
bool checkreturn pb_encode_varint(pb_ostream_t *stream, uint64_t value)
{
    uint8_t buffer[10];
    size_t i = 0;

    if (value == 0)
        return pb_write(stream, (uint8_t*)&value, 1);

    while (value)
    {
        buffer[i] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
        i++;
    }
    buffer[i-1] &= 0x7F; /* Unset top bit on last byte */

    return pb_write(stream, buffer, i);
}

bool checkreturn pb_encode_svarint(pb_ostream_t *stream, int64_t value)
{
    uint64_t zigzagged;
    if (value < 0)
        zigzagged = ~((uint64_t)value << 1);
    else
        zigzagged = (uint64_t)value << 1;

    return pb_encode_varint(stream, zigzagged);
}

bool checkreturn pb_encode_fixed32(pb_ostream_t *stream, const void *value)
{
    #ifdef __BIG_ENDIAN__
    const uint8_t *bytes = value;
    uint8_t lebytes[4];
    lebytes[0] = bytes[3];
    lebytes[1] = bytes[2];
    lebytes[2] = bytes[1];
    lebytes[3] = bytes[0];
    return pb_write(stream, lebytes, 4);
    #else
    return pb_write(stream, (const uint8_t*)value, 4);
    #endif
}

bool checkreturn pb_encode_fixed64(pb_ostream_t *stream, const void *value)
{
    #ifdef __BIG_ENDIAN__
    const uint8_t *bytes = value;
    uint8_t lebytes[8];
    lebytes[0] = bytes[7];
    lebytes[1] = bytes[6];
    lebytes[2] = bytes[5];
    lebytes[3] = bytes[4];
    lebytes[4] = bytes[3];
    lebytes[5] = bytes[2];
    lebytes[6] = bytes[1];
    lebytes[7] = bytes[0];
    return pb_write(stream, lebytes, 8);
    #else
    return pb_write(stream, (const uint8_t*)value, 8);
    #endif
}

bool checkreturn pb_encode_tag(pb_ostream_t *stream, pb_wire_type_t wiretype, uint32_t field_number)
{
    uint64_t tag = ((uint64_t)field_number << 3) | wiretype;
    return pb_encode_varint(stream, tag);
}

bool checkreturn pb_encode_tag_for_field(pb_ostream_t *stream, const pb_field_t *field)
{
    pb_wire_type_t wiretype;
    switch (PB_LTYPE(field->type))
    {
        case PB_LTYPE_VARINT:
        case PB_LTYPE_UVARINT:
        case PB_LTYPE_SVARINT:
            wiretype = PB_WT_VARINT;
            break;

        case PB_LTYPE_FIXED32:
            wiretype = PB_WT_32BIT;
            break;

        case PB_LTYPE_FIXED64:
            wiretype = PB_WT_64BIT;
            break;

        case PB_LTYPE_BYTES:
        case PB_LTYPE_STRING:
        case PB_LTYPE_SUBMESSAGE:
            wiretype = PB_WT_STRING;
            break;

        default:
            PB_RETURN_ERROR(stream, "invalid field type");
    }

    return pb_encode_tag(stream, wiretype, field->tag);
}

bool checkreturn pb_encode_string(pb_ostream_t *stream, const uint8_t *buffer, size_t size)
{
    if (!pb_encode_varint(stream, (uint64_t)size))
        return false;

    return pb_write(stream, buffer, size);
}

bool checkreturn pb_encode_submessage(pb_ostream_t *stream, const pb_field_t fields[], const void *src_struct)
{
    /* First calculate the message size using a non-writing substream. */
    pb_ostream_t substream = PB_OSTREAM_SIZING;
    size_t size;
    bool status;

    if (!pb_encode(&substream, fields, src_struct))
    {
#ifndef PB_NO_ERRMSG
        stream->errmsg = substream.errmsg;
#endif
        return false;
    }

    size = substream.bytes_written;

    if (!pb_encode_varint(stream, (uint64_t)size))
        return false;

    if (stream->callback == NULL)
        return pb_write(stream, NULL, size); /* Just sizing */

    if (stream->bytes_written + size > stream->max_size)
        PB_RETURN_ERROR(stream, "stream full");

    /* Use a substream to verify that a callback doesn't write more than
     * what it did the first time. */
    substream.callback = stream->callback;
    substream.state = stream->state;
    substream.max_size = size;
    substream.bytes_written = 0;
#ifndef PB_NO_ERRMSG
    substream.errmsg = NULL;
#endif

    status = pb_encode(&substream, fields, src_struct);

    stream->bytes_written += substream.bytes_written;
    stream->state = substream.state;
#ifndef PB_NO_ERRMSG
    stream->errmsg = substream.errmsg;
#endif

    if (substream.bytes_written != size)
        PB_RETURN_ERROR(stream, "submsg size changed");

    return status;
}

/* Field encoders */

static bool checkreturn pb_enc_varint(pb_ostream_t *stream, const pb_field_t *field, const void *src)
{
    int64_t value = 0;

    /* Cases 1 and 2 are for compilers that have smaller types for bool
     * or enums, and for int_size option. */
    switch (field->data_size)
    {
        case 1: value = *(const int8_t*)src; break;
        case 2: value = *(const int16_t*)src; break;
        case 4: value = *(const int32_t*)src; break;
        case 8: value = *(const int64_t*)src; break;
        default: PB_RETURN_ERROR(stream, "invalid data_size");
    }

    return pb_encode_varint(stream, (uint64_t)value);
}

static bool checkreturn pb_enc_uvarint(pb_ostream_t *stream, const pb_field_t *field, const void *src)
{
    uint64_t value = 0;

    switch (field->data_size)
    {
        case 1: value = *(const uint8_t*)src; break;
        case 2: value = *(const uint16_t*)src; break;
        case 4: value = *(const uint32_t*)src; break;
        case 8: value = *(const uint64_t*)src; break;
        default: PB_RETURN_ERROR(stream, "invalid data_size");
    }

    return pb_encode_varint(stream, value);
}

static bool checkreturn pb_enc_svarint(pb_ostream_t *stream, const pb_field_t *field, const void *src)
{
    int64_t value = 0;

    switch (field->data_size)
    {
        case 1: value = *(const int8_t*)src; break;
        case 2: value = *(const int16_t*)src; break;
        case 4: value = *(const int32_t*)src; break;
        case 8: value = *(const int64_t*)src; break;
        default: PB_RETURN_ERROR(stream, "invalid data_size");
    }

    return pb_encode_svarint(stream, value);
}

static bool checkreturn pb_enc_fixed64(pb_ostream_t *stream, const pb_field_t *field, const void *src)
{
    PB_UNUSED(field);
    return pb_encode_fixed64(stream, src);
}

static bool checkreturn pb_enc_fixed32(pb_ostream_t *stream, const pb_field_t *field, const void *src)
{
    PB_UNUSED(field);
    return pb_encode_fixed32(stream, src);
}

static bool checkreturn pb_enc_bytes(pb_ostream_t *stream, const pb_field_t *field, const void *src)
{
    const pb_bytes_array_t *bytes = (const pb_bytes_array_t*)src;

    if (src == NULL)
    {
        /* Threat null pointer as an empty bytes field */
        return pb_encode_string(stream, NULL, 0);
    }

    if (PB_ATYPE(field->type) == PB_ATYPE_STATIC &&
        PB_BYTES_ARRAY_T_ALLOCSIZE(bytes->size) > field->data_size)
    {
        PB_RETURN_ERROR(stream, "bytes size exceeded");
    }

    return pb_encode_string(stream, bytes->bytes, bytes->size);
}

static bool checkreturn pb_enc_string(pb_ostream_t *stream, const pb_field_t *field, const void *src)
{
    size_t size = 0;
    size_t max_size = field->data_size;
    const char *p = (const char*)src;

    if (PB_ATYPE(field->type) == PB_ATYPE_POINTER)
        max_size = (size_t)-1;

    if (src == NULL)
    {
        size = 0; /* Threat null pointer as an empty string */
    }
    else
    {
        /* strnlen() is not always available, so just use a loop */
        while (size < max_size && *p != '\0')
        {
            size++;
            p++;
        }
    }

    return pb_encode_string(stream, (const uint8_t*)src, size);
}

static bool checkreturn pb_enc_submessage(pb_ostream_t *stream, const pb_field_t *field, const void *src)
{
    if (field->ptr == NULL)
        PB_RETURN_ERROR(stream, "invalid field descriptor");

    return pb_encode_submessage(stream, (const pb_field_t*)field->ptr, src);
}

#ifdef __cplusplus
}
#endif
//include file removed

MeshbluCredentials::MeshbluCredentials(const char* uuid, const char* token) {
  this->uuid = uuid;
  this->token = token;
}

const char* MeshbluCredentials::getUuid() const {
  return uuid;
}

const char* MeshbluCredentials::getToken() const {
  return token;
}
//include file removed

// Make library cross-compatiable
// with Arduino, GNU C++ for tests, and Spark.
#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#elif defined(SPARK)
#include "application.h"
#endif

Tentacle::Tentacle(size_t numPins) {
  this->numPins = numPins;
  configuredPinActions = new Action[numPins];
  resetPinActions();
}

Action* Tentacle::getConfiguredPinActions() {
  return configuredPinActions;
}

void Tentacle::resetPinActions() {
  for(int i = 0; i < numPins; i++) {
    configuredPinActions[i] = Action_ignore;
  }
}

Tentacle::~Tentacle() {
  delete configuredPinActions;
}

Tentacle& Tentacle::configurePin(int number, Action action) {

  if( number < 0 || number >= numPins) {
    return *this;
  }

  configuredPinActions[number] = action;
  if(action == Action_ignore) {
      return *this;
  }


  setMode(number, action);

  return *this;
}

Tentacle& Tentacle::configurePins(Action* actions) {

  for(int i = 0; i < numPins; i++) {
    configurePin(i, actions[i]);
  }

  return *this;
}

int Tentacle::processPin(int number, int value) {
  if( number < 0 || number >= numPins) {
    return -1;
  }

  Action action = configuredPinActions[number];
  switch(action) {

    case Action_digitalWrite:
      digitalWrite(number, value);
      return value;
    break;

    case Action_analogWrite:
      analogWrite(number, value);
      return value;
    break;
  }

  return processPin(number);
}

int Tentacle::processPin(int number) {
  if( number < 0 || number >= numPins) {
    return -1;
  }

  Action action = configuredPinActions[number];
  switch(action) {
    case Action_digitalRead:
    case Action_digitalReadPullup:
      return digitalRead(number);
    break;

    case Action_analogRead:
    case Action_analogReadPullup:
      return analogRead(number);
    break;

    default:
    break;
  }

  return -1;
}

int Tentacle::getNumPins() const {
  return numPins;
}
//include file removed
// Make library cross-compatiable
// with Arduino, GNU C++ for tests, and Spark.
#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#elif defined(SPARK)
#include "application.h"
#define NUM_DIGITAL_PINS 18
#endif

TentacleArduino::TentacleArduino() : Tentacle(NUM_DIGITAL_PINS) {
}

const MeshbluCredentials& TentacleArduino::getCredentials() {
  const MeshbluCredentials credentials(NULL, NULL);
  return credentials;
}

Tentacle& TentacleArduino::setCredentials(const char* uuid, const char* token) {
  return *this;
}

Tentacle& TentacleArduino::setMode(int number, Action action){

  switch(action) {
    case Action_digitalRead :
    case Action_analogRead  :
    default:
      pinMode(number, INPUT);
    break;

    case Action_digitalReadPullup:
    case Action_analogReadPullup:
      pinMode(number, INPUT_PULLUP);

    case Action_digitalWrite :
    case Action_servoWrite   :
    case Action_pwmWrite     :
      pinMode(number, OUTPUT);
    break;
  }

  return *this;
}

Tentacle& TentacleArduino::digitalWrite(int number, int value){
  ::digitalWrite(number, value);

  return *this;
}

Tentacle& TentacleArduino::analogWrite(int number, int value){
  ::analogWrite(number, value);

  return *this;
}

bool TentacleArduino::digitalRead(int number){
  return ::digitalRead(number);

}

int TentacleArduino::analogRead(int number){
  return ::analogRead(number);
}
/* Automatically generated nanopb constant definitions */
/* Generated by nanopb-0.3.3 at Sun Jul 19 23:12:30 2015. */

//include file removed

#if PB_PROTO_HEADER_VERSION != 30
#error Regenerate this file with the current version of nanopb generator.
#endif



const pb_field_t TentacleMessage_fields[9] = {
    PB_FIELD(  1, UINT32  , OPTIONAL, STATIC  , FIRST, TentacleMessage, version, version, 0),
    PB_FIELD(  2, ENUM    , OPTIONAL, STATIC  , OTHER, TentacleMessage, topic, version, 0),
    PB_FIELD(  3, BOOL    , OPTIONAL, STATIC  , OTHER, TentacleMessage, response, topic, 0),
    PB_FIELD(  4, MESSAGE , REPEATED, CALLBACK, OTHER, TentacleMessage, pins, response, &Pin_fields),
    PB_FIELD(  5, MESSAGE , OPTIONAL, STATIC  , OTHER, TentacleMessage, authentication, pins, &MeshbluAuthentication_fields),
    PB_FIELD(  6, BOOL    , OPTIONAL, STATIC  , OTHER, TentacleMessage, broadcastPins, authentication, 0),
    PB_FIELD(  7, UINT32  , OPTIONAL, STATIC  , OTHER, TentacleMessage, broadcastInterval, broadcastPins, 0),
    PB_FIELD(  8, STRING  , OPTIONAL, CALLBACK, OTHER, TentacleMessage, customData, broadcastInterval, 0),
    PB_LAST_FIELD
};

const pb_field_t Pin_fields[4] = {
    PB_FIELD(  1, UINT32  , OPTIONAL, STATIC  , FIRST, Pin, number, number, 0),
    PB_FIELD(  2, UINT32  , OPTIONAL, STATIC  , OTHER, Pin, value, number, 0),
    PB_FIELD(  3, ENUM    , OPTIONAL, STATIC  , OTHER, Pin, action, value, 0),
    PB_LAST_FIELD
};

const pb_field_t MeshbluAuthentication_fields[3] = {
    PB_FIELD(  1, STRING  , OPTIONAL, STATIC  , FIRST, MeshbluAuthentication, uuid, uuid, 0),
    PB_FIELD(  2, STRING  , OPTIONAL, STATIC  , OTHER, MeshbluAuthentication, token, uuid, 0),
    PB_LAST_FIELD
};


/* Check that field information fits in pb_field_t */
#if !defined(PB_FIELD_32BIT)
/* If you get an error here, it means that you need to define PB_FIELD_32BIT
 * compile-time option. You can do that in pb.h or on compiler command line.
 * 
 * The reason you need to do this is that some of your messages contain tag
 * numbers or field sizes that are larger than what can fit in 8 or 16 bit
 * field descriptors.
 */
PB_STATIC_ASSERT((pb_membersize(TentacleMessage, pins) < 65536 && pb_membersize(TentacleMessage, authentication) < 65536), YOU_MUST_DEFINE_PB_FIELD_32BIT_FOR_MESSAGES_TentacleMessage_Pin_MeshbluAuthentication)
#endif

#if !defined(PB_FIELD_16BIT) && !defined(PB_FIELD_32BIT)
/* If you get an error here, it means that you need to define PB_FIELD_16BIT
 * compile-time option. You can do that in pb.h or on compiler command line.
 * 
 * The reason you need to do this is that some of your messages contain tag
 * numbers or field sizes that are larger than what can fit in the default
 * 8 bit descriptors.
 */
PB_STATIC_ASSERT((pb_membersize(TentacleMessage, pins) < 256 && pb_membersize(TentacleMessage, authentication) < 256), YOU_MUST_DEFINE_PB_FIELD_16BIT_FOR_MESSAGES_TentacleMessage_Pin_MeshbluAuthentication)
#endif


//include file removed
#include <stddef.h>

Pseudopod::Pseudopod(Stream &input, Print &output, Tentacle& tentacle) {
  pb_ostream_from_stream(output, pbOutput);
  pb_istream_from_stream(input, pbInput);

  this->tentacle = &tentacle;
  messagePinActions = new Action[tentacle.getNumPins()];
  resetPinActions();
}

bool Pseudopod::shouldBroadcastPins() {
  return broadcastPins;
}

int Pseudopod::getBroadcastInterval() {
  return broadcastInterval;
}

bool Pseudopod::isConfigured() {
  return configured;
}

void Pseudopod::resetPinActions() {
  for(int i = 0; i < tentacle->getNumPins(); i++) {
    messagePinActions[i] = Action_ignore;
  }
}

size_t Pseudopod::sendPins() {
  pbOutput.bytes_written = 0;

  currentMessage = {};
  currentMessage.topic = TentacleMessageTopic_action;
  currentMessage.has_topic = true;
  currentMessage.response = true;
  currentMessage.has_response = true;
  currentMessage.pins.funcs.encode = &Pseudopod::pinEncode;
  currentMessage.pins.arg = (void*)this;

  bool status = pb_encode_delimited(&pbOutput, TentacleMessage_fields, &currentMessage);

  return pbOutput.bytes_written;
}

size_t Pseudopod::sendPins(Action* actions) {
  resetPinActions();

  for(int i = 0; i < tentacle->getNumPins(); i++) {
    messagePinActions[i] = actions[i];
  }

  return sendPins();
}

size_t Pseudopod::sendConfiguredPins() {
  return sendPins(tentacle->getConfiguredPinActions());
}

size_t Pseudopod::authenticate(const char *uuid, const char *token) {
  pbOutput.bytes_written = 0;

  currentMessage = {};

  currentMessage.topic = TentacleMessageTopic_authentication;
  currentMessage.has_topic = true;
  currentMessage.authentication = {};
  currentMessage.has_authentication = true;

  strncpy(currentMessage.authentication.uuid, uuid, 36);
  currentMessage.authentication.has_uuid = true;

  strncpy(currentMessage.authentication.token, token, 40);

  currentMessage.authentication.has_token = true;

  bool status = pb_encode_delimited(&pbOutput, TentacleMessage_fields, &currentMessage);

  return pbOutput.bytes_written;
}

size_t Pseudopod::requestConfiguration() {
  pbOutput.bytes_written = 0;

  currentMessage = {};

  currentMessage.topic = TentacleMessageTopic_config;
  currentMessage.has_topic = true;

  bool status = pb_encode_delimited(&pbOutput, TentacleMessage_fields, &currentMessage);

  return pbOutput.bytes_written;
}

size_t Pseudopod::registerDevice() {
  return 0;
}

bool Pseudopod::isConnected() {
  pbOutput.bytes_written = 0;

  currentMessage = {};

  currentMessage.topic = TentacleMessageTopic_ping;
  currentMessage.has_topic = true;

  bool status = pb_encode_delimited(&pbOutput, TentacleMessage_fields, &currentMessage);

  return pbOutput.bytes_written != 0;
}

TentacleMessageTopic Pseudopod::readMessage() {
  resetPinActions();

  currentMessage = {};
  currentMessage.pins.funcs.decode = &Pseudopod::pinDecode;
  currentMessage.pins.arg = (void*) this;

  bool status = pb_decode_delimited(&pbInput, TentacleMessage_fields, &currentMessage);

  if (currentMessage.topic == TentacleMessageTopic_config) {
    for(int i = 0; i < tentacle->getNumPins(); i++) {
        tentacle->configurePin(i, messagePinActions[i]);
    }

    configured = true;
    broadcastPins = currentMessage.broadcastPins;
    broadcastInterval = currentMessage.broadcastInterval;
  }

  return currentMessage.topic;
}

bool Pseudopod::pinEncode(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
  Pseudopod *pseudopod = (Pseudopod*) *arg;
  Action action;
  Pin pin;

  for(int i = 0; i < pseudopod->tentacle->getNumPins(); i++) {
    action = pseudopod->messagePinActions[i];
    if(action == Action_ignore) {
      continue;
    }

    pin = {};
    pin.has_action = true;
    pin.action = action;
    pin.has_number = true;
    pin.number = i;
    int value = pseudopod->tentacle->processPin(i);

    if(value != -1) {
      pin.has_value = true;
      pin.value = value;
    }

    if (!pb_encode_tag_for_field(stream, field)) {
      return false;
    }

    if(!pb_encode_submessage(stream, Pin_fields, &pin)) {
      return false;
    }
  }

  return true;
}


bool Pseudopod::pinDecode(pb_istream_t *stream, const pb_field_t *field, void **arg) {
  Pseudopod *pseudopod = (Pseudopod*) *arg;
  Pin pin = {};

  if (!pb_decode(stream, Pin_fields, &pin)) {
    return false;
  }

  TentacleMessage& message = pseudopod->currentMessage;
  pseudopod->messagePinActions[pin.number] = pin.action;

  if(message.topic == TentacleMessageTopic_action) {
    pseudopod->tentacle->processPin(pin.number, pin.value);
  }

  return true;
}
