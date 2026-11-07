#include "mbus_internal.h"

#include <string.h>

void mbus_context_init(mbus_context *ctx)
{
    if (ctx != NULL)
        memset(ctx, 0, sizeof(*ctx));
}

mbus_status mbus_register_manufacturer_decoder(
    mbus_context *ctx, uint16_t manufacturer, mbus_manufacturer_decoder decoder,
    void *context)
{
    size_t i;
    if (ctx == NULL || decoder == NULL)
        return MBUS_INVALID;
    if (ctx->dispatch_depth != 0)
        return MBUS_INVALID;
    for (i = 0; i < ctx->decoder_count; i++) {
        if (ctx->decoders[i].manufacturer == manufacturer) {
            ctx->decoders[i].decoder = decoder;
            ctx->decoders[i].context = context;
            return MBUS_OK;
        }
    }
    if (ctx->decoder_count == MBUS_MAX_MANUFACTURER_DECODERS)
        return MBUS_LIMIT;
    ctx->decoders[ctx->decoder_count].manufacturer = manufacturer;
    ctx->decoders[ctx->decoder_count].decoder = decoder;
    ctx->decoders[ctx->decoder_count].context = context;
    ctx->decoder_count++;
    return MBUS_OK;
}

mbus_status mbus_unregister_manufacturer_decoder(mbus_context *ctx,
                                                 uint16_t manufacturer)
{
    size_t i;
    if (ctx == NULL || ctx->dispatch_depth != 0)
        return MBUS_INVALID;
    for (i = 0; i < ctx->decoder_count; i++) {
        if (ctx->decoders[i].manufacturer == manufacturer) {
            memmove(&ctx->decoders[i], &ctx->decoders[i + 1],
                    (ctx->decoder_count - i - 1) * sizeof(ctx->decoders[0]));
            ctx->decoder_count--;
            memset(&ctx->decoders[ctx->decoder_count], 0, sizeof(ctx->decoders[0]));
            return MBUS_OK;
        }
    }
    return MBUS_INVALID;
}

mbus_status mbus_dispatch_manufacturer_decoder(
    mbus_context *ctx, mbus_manufacturer_block_kind kind, uint16_t manufacturer,
    const uint8_t *data, size_t length, size_t offset,
    mbus_record *record, mbus_result *result, int *handled,
    const char **error)
{
    size_t i;
    if (handled == NULL)
        return mbus_fail(error, MBUS_INVALID, "null manufacturer decoder result");
    *handled = 0;
    for (i = 0; i < ctx->decoder_count; i++) {
        mbus_status status;
        if (ctx->decoders[i].manufacturer != manufacturer)
            continue;
        if (error != NULL)
            *error = NULL;
        ctx->dispatch_depth++;
        status = ctx->decoders[i].decoder(kind, manufacturer, data, length, offset,
                                     record, result, ctx->decoders[i].context, error);
        ctx->dispatch_depth--;
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
