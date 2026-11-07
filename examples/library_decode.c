#include "mbus.h"

#include <stdio.h>

int main(void)
{
    static const uint8_t telegram[] = {
        0x16, 0x44, 0x2d, 0x2c, 0x12, 0x34, 0x56, 0x78, 0x01, 0x07, 0x78,
        0x02, 0x02, 0xd2, 0x04, 0x02, 0x15, 0x2e, 0x16, 0x02, 0x2a, 0x34, 0x23
    };
    static mbus_result result;
    const char *error = NULL;
    mbus_status status = mbus_decode(telegram, sizeof(telegram), MBUS_WIRELESS,
                                     &result, &error);
    size_t i;
    if (status != MBUS_OK) {
        fprintf(stderr, "decode failed (%d): %s\n", (int)status,
                error != NULL ? error : "no diagnostic");
        return 1;
    }
    for (i = 0; i < result.record_count; i++) {
        const mbus_record *record = &result.records[i];
        if (!record->unsupported && record->has_value && record->value_type != MBUS_VALUE_DATETIME &&
            !record->is_compact_profile)
            printf("%s: %.10g %s\n", record->quantity, record->value, record->unit);
    }
    return 0;
}
