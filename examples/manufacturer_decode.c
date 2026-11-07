#include "mbus.h"

#include <stdio.h>
#include <string.h>

/* These formats and this use of manufacturer code 0x2c2d are illustrative. */
enum { EXAMPLE_MANUFACTURER = 0x2c2d };

typedef struct {
    double multiplier;
} example_context;

static void set_example_value(mbus_record *record, uint8_t raw,
                              const example_context *context)
{
    record->unsupported = 0;
    record->quantity = "example_counter";
    record->unit = "1";
    record->has_value = 1;
    record->value_type = MBUS_VALUE_UNSIGNED;
    record->raw_value.unsigned_value = raw;
    record->value = raw * context->multiplier;
}

static mbus_status example_decoder(
    mbus_manufacturer_block_kind kind, uint16_t manufacturer,
    const uint8_t *data, size_t length, size_t offset,
    mbus_record *record, mbus_result *result, void *context,
    const char **error)
{
    const example_context *settings = context;
    (void)manufacturer; /* Registration already selects the manufacturer. */

    /* Example format: a one-byte manufacturer-specific VIF value. */
    if (kind == MBUS_MANUFACTURER_VIF && record->vif == 0x7f &&
        record->dif == 0x01 && length == 1) {
        set_example_value(record, data[0], settings);
        return MBUS_OK;
    }

    /* Example CI A0 payload: a 01 tag followed by a one-byte counter. */
    if (kind == MBUS_MANUFACTURER_CI && result->ci == 0xa0 &&
        length == 2 && data[0] == 0x01) {
        mbus_record *added;
        if (result->record_count >= MBUS_MAX_RECORDS) {
            if (error != NULL)
                *error = "no space for example manufacturer record";
            return MBUS_LIMIT;
        }
        added = &result->records[result->record_count++];
        memset(added, 0, sizeof(*added));
        added->manufacturer_specific = 1;
        added->record_offset = offset;
        added->record_length = length;
        added->value_offset = offset + 1;
        added->value_length = 1;
        set_example_value(added, data[1], settings);
        return MBUS_OK;
    }

    /* Unknown VIF/VIFE, CI, or DIF: leave the result untouched. */
    return MBUS_UNSUPPORTED;
}

static int decode(const uint8_t *telegram, size_t length, mbus_result *result)
{
    const char *error = NULL;
    mbus_status status = mbus_decode(telegram, length, MBUS_WIRELESS, result, &error);
    if (status != MBUS_OK) {
        fprintf(stderr, "decode failed (%d): %s\n", (int)status,
                error != NULL ? error : "no diagnostic");
        return 0;
    }
    return 1;
}

int main(void)
{
    static const uint8_t vif_telegram[] = {
        0x14, 0x44, 0x2d, 0x2c, 0x12, 0x34, 0x56, 0x78, 0x01, 0x07, 0x78,
        0x01, 0x7f, 0x42, 0x02, 0x7f, 0x34, 0x12, 0x01, 0x03, 0x05
    };
    static const uint8_t ci_telegram[] = {
        0x0c, 0x44, 0x2d, 0x2c, 0x12, 0x34, 0x56, 0x78, 0x01, 0x07,
        0xa0, 0x01, 0x2a
    };
    static mbus_result result;
    example_context context = {2.0};
    mbus_status status;
    int ok = 0;

    status = mbus_register_manufacturer_decoder(EXAMPLE_MANUFACTURER,
                                                 example_decoder, &context);
    if (status != MBUS_OK) {
        fprintf(stderr, "registration failed (%d)\n", (int)status);
        return 1;
    }
    if (!decode(vif_telegram, sizeof(vif_telegram), &result))
        goto cleanup;
    if (result.record_count != 3 || result.records[0].unsupported ||
        result.records[0].raw_value.unsigned_value != 0x42 ||
        result.records[0].value != 132.0 ||
        !result.records[1].manufacturer_specific || !result.records[1].unsupported ||
        result.records[2].unsupported || !result.records[2].has_value ||
        result.records[2].value != 5.0) {
        fprintf(stderr, "unexpected VIF decode result\n");
        goto cleanup;
    }
    printf("VIF counter: %.0f; unknown block: undecoded; following energy: %.0f Wh\n",
           result.records[0].value, result.records[2].value);

    if (!decode(ci_telegram, sizeof(ci_telegram), &result))
        goto cleanup;
    if (!result.manufacturer_data_decoded || result.record_count != 1 ||
        result.records[0].raw_value.unsigned_value != 0x2a ||
        result.records[0].value != 84.0) {
        fprintf(stderr, "unexpected CI decode result\n");
        goto cleanup;
    }
    printf("CI counter: %.0f\n", result.records[0].value);
    ok = 1;

cleanup:
    status = mbus_unregister_manufacturer_decoder(EXAMPLE_MANUFACTURER);
    if (status != MBUS_OK) {
        fprintf(stderr, "unregistration failed (%d)\n", (int)status);
        return 1;
    }
    return ok ? 0 : 1;
}
