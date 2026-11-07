#include "mbus_internal.h"

#include <string.h>

typedef struct {
    uint16_t manufacturer;
    mbus_manufacturer_decoder decoder;
    void *context;
} decoder_entry;

static decoder_entry decoders[MBUS_MAX_MANUFACTURER_DECODERS];
static size_t decoder_count;
static size_t dispatch_depth;

mbus_status mbus_register_manufacturer_decoder(
    uint16_t manufacturer, mbus_manufacturer_decoder decoder, void *context)
{
    size_t i;
    if (decoder == NULL)
        return MBUS_INVALID;
    if (dispatch_depth != 0)
        return MBUS_INVALID;
    for (i = 0; i < decoder_count; i++) {
        if (decoders[i].manufacturer == manufacturer) {
            decoders[i].decoder = decoder;
            decoders[i].context = context;
            return MBUS_OK;
        }
    }
    if (decoder_count == MBUS_MAX_MANUFACTURER_DECODERS)
        return MBUS_LIMIT;
    decoders[decoder_count].manufacturer = manufacturer;
    decoders[decoder_count].decoder = decoder;
    decoders[decoder_count].context = context;
    decoder_count++;
    return MBUS_OK;
}

mbus_status mbus_unregister_manufacturer_decoder(uint16_t manufacturer)
{
    size_t i;
    if (dispatch_depth != 0)
        return MBUS_INVALID;
    for (i = 0; i < decoder_count; i++) {
        if (decoders[i].manufacturer == manufacturer) {
            memmove(&decoders[i], &decoders[i + 1],
                    (decoder_count - i - 1) * sizeof(decoders[0]));
            decoder_count--;
            memset(&decoders[decoder_count], 0, sizeof(decoders[0]));
            return MBUS_OK;
        }
    }
    return MBUS_INVALID;
}

mbus_status mbus_dispatch_manufacturer_decoder(
    mbus_manufacturer_block_kind kind, uint16_t manufacturer,
    const uint8_t *data, size_t length, size_t offset,
    mbus_record *record, mbus_result *result, int *handled,
    const char **error)
{
    size_t i;
    if (handled == NULL)
        return mbus_fail(error, MBUS_INVALID, "null manufacturer decoder result");
    *handled = 0;
    for (i = 0; i < decoder_count; i++) {
        mbus_status status;
        if (decoders[i].manufacturer != manufacturer)
            continue;
        if (error != NULL)
            *error = NULL;
        dispatch_depth++;
        status = decoders[i].decoder(kind, manufacturer, data, length, offset,
                                     record, result, decoders[i].context, error);
        dispatch_depth--;
        if (status == MBUS_UNSUPPORTED) {
            if (error != NULL)
                *error = NULL;
            return MBUS_OK;
        }
        if (status != MBUS_OK) {
            if (error != NULL && *error == NULL)
                *error = "manufacturer-specific decoder failed";
            return status;
        }
        *handled = 1;
        return MBUS_OK;
    }
    return MBUS_OK;
}

mbus_status mbus_validate_manufacturer_records(const mbus_result *result,
                                               size_t first,
                                               const char **error)
{
    size_t i;
    if (result->telegram_length > MBUS_MAX_TELEGRAM)
        return mbus_fail(error, MBUS_INVALID,
                         "manufacturer decoder changed the telegram length");
    if (result->record_count > MBUS_MAX_RECORDS)
        return mbus_fail(error, MBUS_LIMIT,
                         "manufacturer decoder returned too many records");
    if (result->record_count < first)
        return mbus_fail(error, MBUS_INVALID,
                         "manufacturer decoder removed decoded records");
    for (i = first; i < result->record_count; i++) {
        const mbus_record *record = &result->records[i];
        if (record->record_offset > result->telegram_length ||
            record->record_length > result->telegram_length - record->record_offset ||
            record->value_offset > result->telegram_length ||
            record->value_length > result->telegram_length - record->value_offset)
            return mbus_fail(error, MBUS_INVALID,
                             "manufacturer decoder returned an invalid record offset");
        if (!record->unsupported && (record->quantity == NULL || record->unit == NULL))
            return mbus_fail(error, MBUS_INVALID,
                             "manufacturer decoder returned an incomplete record");
    }
    return MBUS_OK;
}
