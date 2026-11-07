#include "mbus.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static mbus_context ctx;

static mbus_result result;
static const char *error;

static mbus_status manufacturer_test_decoder(
    mbus_manufacturer_block_kind kind, uint16_t manufacturer,
    const uint8_t *data, size_t length, size_t offset,
    mbus_record *record, mbus_result *decoded, void *context,
    const char **decoder_error)
{
    (void)decoder_error;
    if (*(const int *)context || manufacturer != 0x2c2d)
        return MBUS_UNSUPPORTED;
    if (offset > decoded->telegram_length ||
        length > decoded->telegram_length - offset ||
        data != decoded->telegram + offset)
        return MBUS_INVALID;
    if (kind == MBUS_MANUFACTURER_VIF || kind == MBUS_MANUFACTURER_VIFE) {
        if (record == NULL || length != 1)
            return MBUS_INVALID;
        record->unsupported = 0;
        record->quantity = "manufacturer_value";
        record->unit = "1";
        record->has_value = 1;
        record->value_type = MBUS_VALUE_UNSIGNED;
        record->raw_value.unsigned_value = data[0];
        record->value = 2.0 * data[0];
        return MBUS_OK;
    }
    return kind == MBUS_MANUFACTURER_CI || kind == MBUS_MANUFACTURER_DIF ?
           MBUS_OK : MBUS_UNSUPPORTED;
}

static size_t wireless(uint8_t *frame, const uint8_t *payload, size_t n)
{
    static const uint8_t header[] = {0, 0x44, 0x2d, 0x2c, 0x12, 0x34, 0x56, 0x78, 1, 7};
    assert(n + sizeof(header) <= 256);
    memcpy(frame, header, sizeof(header));
    memcpy(frame + sizeof(header), payload, n);
    frame[0] = (uint8_t)(n + sizeof(header) - 1);
    return n + sizeof(header);
}

static size_t wired(uint8_t *frame, const uint8_t *payload, size_t n)
{
    size_t i;
    unsigned sum = 0;
    assert(n + 2 <= 255);
    frame[0] = 0x68;
    frame[1] = frame[2] = (uint8_t)(n + 2);
    frame[3] = 0x68;
    frame[4] = 0x08;
    frame[5] = 1;
    memcpy(frame + 6, payload, n);
    for (i = 4; i < n + 6; i++)
        sum += frame[i];
    frame[n + 6] = (uint8_t)sum;
    frame[n + 7] = 0x16;
    return n + 8;
}

static mbus_status decode_wireless(const uint8_t *payload, size_t n)
{
    uint8_t frame[MBUS_MAX_TELEGRAM];
    size_t length = wireless(frame, payload, n);
    return mbus_decode(&ctx, frame, length, MBUS_WIRELESS, &result, &error);
}

static void example(void)
{
    const uint8_t payload[] = {0x78, 0x02, 0x02, 0xd2, 0x04, 0x02, 0x15,
                               0x2e, 0x16, 0x02, 0x2a, 0x34, 0x23};
    uint8_t frame[MBUS_MAX_TELEGRAM];
    size_t n;
    assert(decode_wireless(payload, sizeof(payload)) == MBUS_OK);
    assert(result.record_count == 3 && result.has_link_identity && !result.has_transport_identity);
    assert(strcmp(result.records[0].unit, "Wh") == 0);
    assert(fabs(result.records[0].value - 123.4) < 1e-9);
    assert(fabs(result.records[1].value - 567.8) < 1e-9);
    assert(fabs(result.records[2].value - 901.2) < 1e-9);
    n = wired(frame, payload, sizeof(payload));
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRED, &result, &error) == MBUS_OK);
    assert(result.address == 1 && result.record_count == 3);
    frame[n - 2]++;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRED, &result, &error) == MBUS_INVALID);
}

static void group_and_numbers(void)
{
    const uint8_t payload[] = {
        0x78,
        0x01, 0x03, 0xff,
        0xc2, 0x71, 0x13, 0x34, 0x12,
        0x0a, 0x03, 0x23, 0xf1,
        0x01, 0x03, 0x80,
        0x0a, 0x03, 0x2a, 0x12,
        0x01, 0xfd, 0x17, 0xff,
        0x05, 0x03, 0x00, 0x00, 0xc0, 0x3f,
        0x2f, 0x1f, 0xaa, 0xbb
    };
    assert(decode_wireless(payload, sizeof(payload)) == MBUS_OK);
    assert(result.record_count == 7);
    assert(result.records[0].value_type == MBUS_VALUE_SIGNED && result.records[0].raw_value.signed_value == -1);
    assert(result.records[1].storage == 3 && result.records[1].tariff == 3);
    assert(result.records[1].subunit == 1 && result.records[1].raw_value.signed_value == 0x1234);
    assert(result.records[2].value_type == MBUS_VALUE_SIGNED && result.records[2].raw_value.signed_value == -123);
    assert(result.records[3].invalid_value && !result.records[3].has_value);
    assert(result.records[4].invalid_value && !result.records[4].has_value);
    assert(result.records[3].value_type == MBUS_VALUE_SIGNED && result.records[4].value_type == MBUS_VALUE_SIGNED);
    assert(result.records[5].value_type == MBUS_VALUE_UNSIGNED && result.records[5].raw_value.unsigned_value == 255);
    assert(result.records[6].value_type == MBUS_VALUE_REAL && result.records[6].raw_value.real_value == 1.5f && result.records[6].value == 1.5);
    assert(result.more_records && result.has_manufacturer_data && result.manufacturer_data_length == 2);
}

static void value_types(void)
{
    const uint8_t payload[] = {
        0x78, 0x00, 0x03, 0x08, 0x03,
        0x05, 0x02, 0x00, 0x00, 0xc0, 0x3f,
        0x05, 0x03, 0x00, 0x00, 0x80, 0x7f,
        0x02, 0x6c, 0xff, 0xff, 0x01, 0x6f, 0x12
    };
    assert(decode_wireless(payload, sizeof(payload)) == MBUS_OK);
    assert(result.record_count == 6);
    assert(result.records[0].value_type == MBUS_VALUE_NONE && !result.records[0].has_value);
    assert(result.records[1].value_type == MBUS_VALUE_NONE && !result.records[1].has_value);
    assert(result.records[2].value_type == MBUS_VALUE_REAL && result.records[2].has_value);
    assert(result.records[2].raw_value.real_value == 1.5f && fabs(result.records[2].value - 0.15) < 1e-9);
    assert(result.records[3].value_type == MBUS_VALUE_REAL && result.records[3].invalid_value && !result.records[3].has_value);
    assert(result.records[4].value_type == MBUS_VALUE_DATETIME && result.records[4].invalid_value && !result.records[4].has_value);
    assert(result.records[5].unsupported && result.records[5].value_type == MBUS_VALUE_NONE);
}

static void headers_and_errors(void)
{
    const uint8_t short_header[] = {0x7a, 1, 2, 0, 0, 0x01, 0x03, 0x12};
    const uint8_t long_header[] = {0x72, 0x87, 0x65, 0x43, 0x21, 0x34, 0x12, 2, 4,
                                   3, 4, 0, 0, 0x01, 0x03, 0x12};
    const uint8_t encrypted[] = {0x7a, 1, 0, 0, 5};
    const uint8_t truncated[] = {0x78, 0x84, 0x80};
    const uint8_t modifier[] = {0x78, 0x01, 0x83, 0x00, 1};
    const uint8_t variable[] = {0x78, 0x0d, 0x03, 1, 2};
    const uint8_t compact[] = {0x79, 0, 0, 0, 0};
    uint8_t frame[MBUS_MAX_TELEGRAM];
    mbus_identity link, transport;
    int has_link, has_transport;
    size_t n;
    assert(decode_wireless(short_header, sizeof(short_header)) == MBUS_OK);
    assert(result.access_number == 1 && result.meter_status == 2);
    assert(decode_wireless(long_header, sizeof(long_header)) == MBUS_OK);
    assert(result.access_number == 3 && result.has_link_identity && result.has_transport_identity);
    assert(result.link_identity.manufacturer == 0x2c2d);
    assert(result.transport_identity.manufacturer == 0x1234);
    assert(memcmp(result.link_identity.identification, "\x12\x34\x56\x78", 4) == 0);
    assert(memcmp(result.transport_identity.identification, "\x87\x65\x43\x21", 4) == 0);
    assert(result.link_identity.version == 1 && result.link_identity.medium == MEDIUM_WATER);
    assert(result.transport_identity.version == 2 && result.transport_identity.medium == MEDIUM_HEAT_OUTLET);
    n = wireless(frame, short_header, sizeof(short_header));
    frame[9] = 0xff;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    assert(result.link_identity.medium == 0xff);
    n = wired(frame, long_header, sizeof(long_header));
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRED, &result, &error) == MBUS_OK);
    assert(!result.has_link_identity && result.has_transport_identity);
    assert(result.address == 1 && result.transport_identity.manufacturer == 0x1234);
    assert(mbus_get_identities(frame, n, MBUS_WIRED, &link, &has_link,
                               &transport, &has_transport, &error) == MBUS_OK);
    assert(!has_link && has_transport && transport.manufacturer == 0x1234);
    assert(mbus_get_identities(frame, n - 1, MBUS_WIRED, &link, &has_link,
                               &transport, &has_transport, &error) == MBUS_INVALID);
    assert(decode_wireless(encrypted, sizeof(encrypted)) == MBUS_UNSUPPORTED);
    assert(strstr(error, "security") != NULL);
    assert(decode_wireless(truncated, sizeof(truncated)) == MBUS_INVALID);
    assert(decode_wireless(modifier, sizeof(modifier)) == MBUS_OK);
    assert(result.records[0].unsupported);
    assert(decode_wireless(variable, sizeof(variable)) == MBUS_OK);
    assert(result.records[0].unsupported);
    assert(decode_wireless(compact, sizeof(compact)) == MBUS_UNSUPPORTED);
    n = wireless(frame, short_header, sizeof(short_header));
    frame[0]--;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_INVALID);
    assert(mbus_decode(&ctx, NULL, n, MBUS_WIRELESS, &result, &error) == MBUS_INVALID);
}

static void manufacturer_decoders(void)
{
    const uint8_t vif_data[] = {0x78, 0x01, 0x7f, 0x42, 0x01, 0x03, 0x05};
    const uint8_t vife_data[] = {
        0x78, 0x01, 0x83, 0xff, 0x7f, 0x42, 0x01, 0x03, 0x05
    };
    const uint8_t ci_data[] = {0xa0, 0x11, 0x22, 0x33};
    const uint8_t dif_data[] = {0x78, 0x0f, 0xaa, 0xbb};
    const int accept = 0, decline = 1;
    uint8_t frame[MBUS_MAX_TELEGRAM];
    size_t n, i;

    assert(decode_wireless(vif_data, sizeof(vif_data)) == MBUS_OK);
    assert(result.record_count == 2 && result.records[0].unsupported &&
           result.records[0].manufacturer_specific &&
           result.records[1].raw_value.signed_value == 5);
    assert(mbus_register_manufacturer_decoder(&ctx, 0x2c2d, manufacturer_test_decoder,
                                              (void *)&decline) == MBUS_OK);
    assert(decode_wireless(vif_data, sizeof(vif_data)) == MBUS_OK);
    assert(result.records[0].unsupported && result.records[1].raw_value.signed_value == 5);
    assert(mbus_register_manufacturer_decoder(&ctx, 0x2c2d, manufacturer_test_decoder,
                                              (void *)&accept) == MBUS_OK);
    assert(decode_wireless(vif_data, sizeof(vif_data)) == MBUS_OK);
    assert(!result.records[0].unsupported && result.records[0].manufacturer_specific &&
           strcmp(result.records[0].quantity, "manufacturer_value") == 0 &&
           result.records[0].raw_value.unsigned_value == 0x42 &&
           result.records[0].value == 132.0 &&
           result.records[1].raw_value.signed_value == 5);
    assert(decode_wireless(vife_data, sizeof(vife_data)) == MBUS_OK);
    assert(!result.records[0].unsupported && result.records[0].manufacturer_specific &&
           result.records[0].raw_value.unsigned_value == 0x42 &&
           result.records[0].value == 132.0);
    assert(mbus_unregister_manufacturer_decoder(&ctx, 0x2c2d) == MBUS_OK);
    assert(decode_wireless(vif_data, sizeof(vif_data)) == MBUS_OK);
    assert(result.records[0].unsupported);

    n = wireless(frame, ci_data, sizeof(ci_data));
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    assert(result.has_manufacturer_data && !result.manufacturer_data_decoded &&
           result.manufacturer_data_kind == MBUS_MANUFACTURER_CI &&
           result.manufacturer_data_length == 3);
    assert(mbus_register_manufacturer_decoder(&ctx, 0x2c2d, manufacturer_test_decoder,
                                              (void *)&accept) == MBUS_OK);
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    assert(result.has_manufacturer_data && result.manufacturer_data_decoded);
    assert(decode_wireless(dif_data, sizeof(dif_data)) == MBUS_OK);
    assert(result.has_manufacturer_data && result.manufacturer_data_decoded &&
           result.manufacturer_data_kind == MBUS_MANUFACTURER_DIF);
    assert(mbus_unregister_manufacturer_decoder(&ctx, 0x2c2d) == MBUS_OK);

    for (i = 0; i < MBUS_MAX_MANUFACTURER_DECODERS; i++)
        assert(mbus_register_manufacturer_decoder(&ctx, (uint16_t)(0x1000 + i),
                                                  manufacturer_test_decoder,
                                                  (void *)&accept) == MBUS_OK);
    assert(mbus_register_manufacturer_decoder(&ctx, 0x2000, manufacturer_test_decoder,
                                              (void *)&accept) == MBUS_LIMIT);
    for (i = 0; i < MBUS_MAX_MANUFACTURER_DECODERS; i++)
        assert(mbus_unregister_manufacturer_decoder(&ctx, (uint16_t)(0x1000 + i)) == MBUS_OK);
    assert(mbus_unregister_manufacturer_decoder(&ctx, 0x2c2d) == MBUS_INVALID);

    /* A decoder registered in one context must not affect another. */
    {
        mbus_context other;
        mbus_context_init(&other);
        assert(mbus_register_manufacturer_decoder(&other, 0x2c2d, manufacturer_test_decoder,
                                                  (void *)&accept) == MBUS_OK);
        assert(decode_wireless(vif_data, sizeof(vif_data)) == MBUS_OK);
        assert(result.records[0].unsupported);
        n = wireless(frame, vif_data, sizeof(vif_data));
        assert(mbus_decode(&other, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
        assert(!result.records[0].unsupported);
        assert(mbus_unregister_manufacturer_decoder(&other, 0x2c2d) == MBUS_OK);
        assert(mbus_unregister_manufacturer_decoder(&ctx, 0x2c2d) == MBUS_INVALID);
    }
    assert(mbus_register_manufacturer_decoder(NULL, 0x2c2d, manufacturer_test_decoder,
                                              (void *)&accept) == MBUS_INVALID);
}

static void truncation_and_limits(void)
{
    const uint8_t payload[] = {0x7a, 1, 0, 0, 0, 0xc4, 0x91, 0x22, 0x03, 1, 2, 3, 4};
    uint8_t frame[MBUS_MAX_TELEGRAM], many[220];
    size_t n, i;
    n = wired(frame, payload, sizeof(payload));
    for (i = 0; i < n; i++)
        assert(mbus_decode(&ctx, frame, i, MBUS_WIRED, &result, &error) != MBUS_OK);
    many[0] = 0x78;
    for (i = 0; i < MBUS_MAX_RECORDS + 1; i++) {
        many[1 + i * 3] = 1;
        many[2 + i * 3] = 3;
        many[3 + i * 3] = 1;
    }
    assert(decode_wireless(many, 1 + (MBUS_MAX_RECORDS + 1) * 3) == MBUS_LIMIT);
}

static void extensions_and_large_values(void)
{
    const uint8_t payload[] = {
        0x78, 0x81, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x03, 1,
        0x81, 0x00, 0x03, 2,
        0x07, 0xfd, 0x17, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
    const uint8_t excess[] = {
        0x78, 0x81, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0, 0x03, 1
    };
    assert(decode_wireless(payload, sizeof(payload)) == MBUS_OK);
    assert(result.records[0].storage == (UINT64_C(1) << 41) - 2);
    assert(result.records[0].tariff == (1U << 20) - 1);
    assert(result.records[0].subunit == (1U << 10) - 1);
    assert(result.records[1].storage_is_register);
    assert(result.records[2].value_type == MBUS_VALUE_UNSIGNED && result.records[2].raw_value.unsigned_value == UINT64_MAX);
    assert(decode_wireless(excess, sizeof(excess)) == MBUS_INVALID);
}

static void unsupported_records(void)
{
    const uint8_t payload[] = {
        0x78,
        0x02, 0x6f, 0x12, 0x34,
        0x81, 0x10, 0x83, 0x00, 0x42,
        0x01, 0xfd, 0x97, 0x01, 0x43,
        0x01, 0xfb, 0x00, 0x44,
        0x01, 0x7c, 0x02, 'W', 'k', 0x45,
        0x01, 0xfc, 0xa2, 0x73, 0x01, 'x', 0x46,
        0x0d, 0x03, 0xc2, 0x34, 0x12,
        0x01, 0x03, 0x55
    };
    const uint8_t reserved_lvar[] = {0x78, 0x0d, 0x03, 0xca};
    const uint8_t short_lvar[] = {0x78, 0x0d, 0x03, 0x02, 0x42};
    const uint8_t short_vife[] = {0x78, 0x01, 0x83};
    const uint8_t short_custom[] = {0x78, 0x01, 0x7c, 0x02, 'x'};
    const uint8_t manufacturer[] = {0x78, 0x01, 0x7f, 0x42};
    const uint8_t container_length[] = {0x78, 0x0d, 0xfd, 0x3b, 0xc1, 0x42};
    const uint8_t compact_profile[] = {0x78, 0x0d, 0x93, 0x13, 0x01, 0x00};
    size_t i;
    assert(decode_wireless(payload, sizeof(payload)) == MBUS_OK);
    assert(result.record_count == 8);
    for (i = 0; i < 7; i++)
        assert(result.records[i].unsupported);
    assert(result.records[1].tariff == 1);
    assert(result.records[6].record_length == 5 && result.records[6].value_length == 2);
    assert(memcmp(result.telegram + result.records[6].record_offset, "\x0d\x03\xc2\x34\x12", 5) == 0);
    assert(memcmp(result.telegram + result.records[6].value_offset, "\x34\x12", 2) == 0);
    assert(!result.records[7].unsupported && result.records[7].raw_value.signed_value == 0x55);
    assert(decode_wireless(reserved_lvar, sizeof(reserved_lvar)) == MBUS_UNSUPPORTED);
    assert(decode_wireless(short_lvar, sizeof(short_lvar)) == MBUS_INVALID);
    assert(decode_wireless(short_vife, sizeof(short_vife)) == MBUS_INVALID);
    assert(decode_wireless(short_custom, sizeof(short_custom)) == MBUS_INVALID);
    assert(decode_wireless(manufacturer, sizeof(manufacturer)) == MBUS_OK);
    assert(result.record_count == 1 && result.records[0].manufacturer_specific &&
           result.records[0].unsupported);
    assert(decode_wireless(container_length, sizeof(container_length)) == MBUS_UNSUPPORTED);
    assert(decode_wireless(compact_profile, sizeof(compact_profile)) == MBUS_UNSUPPORTED);
}

static void modifiers(void)
{
    const uint8_t payload[] = {
        0x78, 0x04, 0x93, 0x3a, 0xe8, 0x03, 0, 0,
        0x01, 0x83, 0xf3, 0x3c, 20,
        0x01, 0x83, 0x12, 4,
        0x01, 0x83, 0x20, 1
    };
    const uint8_t zero_vife[] = {0x78, 0x01, 0x83, 0x00, 0x01, 0x01, 0x03, 0x02};
    assert(decode_wireless(zero_vife, sizeof(zero_vife)) == MBUS_OK);
    assert(result.record_count == 2);
    assert(result.records[0].vife_count == 1 && result.records[0].vifes[0] == 0);
    assert(result.records[1].vife_count == 0);
    assert(decode_wireless(payload, sizeof(payload)) == MBUS_OK);
    assert(result.record_count == 4);
    assert(result.records[0].modifier_flags == 2);
    assert(result.records[0].vife_count == 1 && result.records[0].vifes[0] == 0x3a);
    assert(fabs(result.records[0].value - 1.0) < 1e-9);
    assert(result.records[1].vife_count == 2 && result.records[1].modifier_flags == 8);
    assert(result.records[1].vifes[0] == 0xf3 && result.records[1].vifes[1] == 0x3c);
    assert(result.records[1].exponent == -3 && fabs(result.records[1].value - 0.02) < 1e-9);
    assert(result.records[2].modifier_flags == 1);
    assert(result.records[3].unsupported);
}

static void security_mode7(void)
{
    const uint8_t short_payload[] = {
        0x7a, 0x55, 0x00, 0x18, 0x27, 0x10, 1, 2, 3, 4,
        0x1c, 0x4d, 0x6a, 0x81, 0xcb, 0x50, 0x06, 0x83,
        0xd3, 0x9e, 0x11, 0x4e, 0x1a, 0x4f, 0x41, 0x43,
        0x01, 0x03, 0x07
    };
    const uint8_t long_payload[] = {
        0x72, 0x87, 0x65, 0x43, 0x21, 0x34, 0x12, 2, 4,
        3, 4, 0x10, 0x27, 0x50, 2, 1, 2, 3, 4,
        0x92, 0x2c, 0x94, 0x05, 0xe9, 0x89, 0xac, 0x27,
        0x5a, 0x4d, 0x3e, 0xb0, 0x89, 0xb1, 0x63, 0x4a
    };
    const mbus_key keys[] = {
        {{0}},
        {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}}
    };
    uint8_t frame[MBUS_MAX_TELEGRAM];
    size_t n = wireless(frame, short_payload, sizeof(short_payload));
    /* This vector uses wireless identity AE4C/44552233/99/07. */
    frame[2] = 0xae; frame[3] = 0x4c;
    memcpy(frame + 4, "\x44\x55\x22\x33\x99\x07", 6);
    assert(mbus_decode_with_keys(&ctx, frame, n, MBUS_WIRELESS, keys, 2, &result, &error) == MBUS_OK);
    assert(result.decrypted && !result.authentication_verified);
    assert(result.message_counter == 0x04030201 && result.security_key_id == 0);
    assert(result.record_count == 2 && result.records[0].raw_value.signed_value == 123529);
    assert(result.records[1].raw_value.signed_value == 7);
    assert(mbus_decode_with_keys(&ctx, frame, n, MBUS_WIRELESS, keys, 1, &result, &error) == MBUS_INVALID);
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_UNSUPPORTED);
    frame[15] = 0;
    assert(mbus_decode_with_keys(&ctx, frame, n, MBUS_WIRELESS, keys, 2, &result, &error) == MBUS_UNSUPPORTED);
    frame[15] = 0x10;
    frame[14] = 0x07;
    assert(mbus_decode_with_keys(&ctx, frame, n, MBUS_WIRELESS, keys, 2, &result, &error) == MBUS_UNSUPPORTED);

    n = wireless(frame, long_payload, sizeof(long_payload));
    assert(mbus_decode_with_keys(&ctx, frame, n, MBUS_WIRELESS, keys + 1, 1, &result, &error) == MBUS_OK);
    assert(result.has_key_version && result.security_key_version == 2);
    assert(result.record_count == 1 && result.records[0].raw_value.signed_value == 123529);
    n = wired(frame, long_payload, sizeof(long_payload));
    assert(mbus_decode_with_keys(&ctx, frame, n, MBUS_WIRED, keys + 1, 1, &result, &error) == MBUS_OK);
    assert(result.record_count == 1 && result.records[0].raw_value.signed_value == 123529);
}

static void compact_profiles(void)
{
    /* EN 13757-3:2018 Annex F tables F.10, F.12 and F.14. */
    const uint8_t forward[] = {
        0x78, 0x84,0x04,0x6d,0x00,0x20,0x41,0x11,
        0x8b,0x04,0x15,0x00,0x30,0x12,
        0x8d,0x04,0x95,0x1f,0x05,0x69,0x01,0x03,0x02,0x11
    };
    const uint8_t inverse[] = {
        0x78, 0x84,0x04,0x6d,0x00,0x23,0x41,0x11,
        0x8b,0x04,0x15,0x16,0x30,0x12,
        0x8d,0x04,0x95,0x13,0x05,0x69,0x01,0x11,0x02,0x03
    };
    const uint8_t registers[] = {
        0x78, 0x84,0x90,0x81,0x00,0x03,0xf0,0x49,0x02,0x00,
        0x8d,0x90,0x81,0x00,0x83,0x1e,0x0a,0x34,0xfe,
        0xa0,0x86,0x01,0x00,0xd0,0xfb,0x01,0x00
    };
    const uint8_t invalid[] = {
        0x78,0x8b,0x04,0x15,0x00,0x30,0x12,
        0x8d,0x04,0x95,0x1f,0x05,0x69,0x01,0x03,0xff,0x11
    };
    const uint8_t missing_base[] = {0x78,0x8d,0x04,0x95,0x1f,0x03,0x69,0x01,0x03};
    const uint8_t empty_base[] = {0x78,0x40,0x03,0x4d,0x83,0x1f,0x03,0x41,0x01,0x03};
    const uint8_t bad_length[] = {0x78,0x8d,0x04,0x95,0x1f,0x05,0x34,0x01,1,2,3};
    const uint8_t bad_base[] = {
        0x78,0x8b,0x04,0x15,0xff,0xff,0xff,
        0x8d,0x04,0x95,0x1f,0x03,0x69,0x01,0x03
    };
    const uint8_t ambiguous_base[] = {
        0x78,0x8b,0x04,0x15,0x00,0x30,0x12,0x8b,0x04,0x15,0x16,0x30,0x12,
        0x8d,0x04,0x95,0x1f,0x03,0x69,0x01,0x03
    };
    const uint8_t unimplemented_mode[] = {
        0x78,0x8b,0x04,0x15,0x00,0x30,0x12,
        0x8d,0x04,0x95,0x1f,0x03,0xa9,0x01,0x03
    };
    const uint8_t absolute_byte[] = {0x78,0x4d,0x83,0x1f,0x05,0x01,0x01,0xfb,0x80,0x07};
    const uint8_t binary_increment[] = {
        0x78,0x4d,0x83,0x1f,0x0a,0x44,0x01,0x01,0,0,0,0xff,0xff,0xff,0xff,
        0x41,0x03,0x0a
    };
    /* OMS Annex N.13 DR4 and DR5, transported as unencrypted full-frame records. */
    const uint8_t oms_inverse[] = {
        0x78,0xce,0x01,0x03,0x95,0x19,0,0,0,0,
        0xcd,0x01,0x83,0x13,0x30,0x41,0x08,
        1,3,5,7,9,8,6,4,2,0,0,0,0,0,0,0,0,0,0,0,
        1,3,5,7,9,8,6,4,2,0,0,0,0,0,0,0,0,0,0,0,
        1,3,5,7,9,8
    };
    const uint8_t encrypted_profile[] = {
        0x7a,0x55,0x00,0x20,0x05,
        0xc2,0xa0,0x67,0xed,0x64,0xfb,0x48,0xde,0xad,0x61,0x1b,0xec,0xcb,0x91,0x86,0xff,
        0x0d,0x2d,0x3f,0xbe,0xe2,0xfa,0x73,0x20,0x06,0xbf,0xb4,0xe0,0x4d,0xb3,0x08,0x0a
    };
    const mbus_key profile_key = {{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
    uint8_t encrypted_frame[MBUS_MAX_TELEGRAM];
    size_t encrypted_length;
    const mbus_profile_value *v;
    /* Reduced from the monthly 16-bit profile in rtl-wmbus samples.cu8. */
    uint8_t rtl_sample_profile[] = {
        0x78,
        0x83,0x04,0x6e,0,0,0,
        0x8d,0x04,0xee,0x1f,0x1e,0x72,0xfe,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0
    };
    const uint8_t unsigned_modes[] = {
        0x78, 0x41,0x03,10, 0x4d,0x83,0x1f,0x04,0x42,1,3,0
    };
    const uint8_t decrement_mode[] = {
        0x78, 0x41,0x03,10, 0x4d,0x83,0x1f,0x04,0x82,1,3,0
    };
    const uint8_t unsigned_minimum_base[] = {
        0x78, 0x41,0x03,0x80, 0x4d,0x83,0x1f,0x03,0x41,1,1
    };
    const uint8_t unsigned_maximum_base[] = {
        0x78, 0x41,0x03,0xff, 0x4d,0x83,0x1f,0x03,0x41,1,1
    };
    const uint8_t difference_mode[] = {
        0x78, 0x41,0x03,10, 0x4d,0x83,0x1f,0x04,0xc2,1,0xfd,0xff
    };
    const uint8_t wide_unsigned[] = {
        0x78, 0x41,0x03,0, 0x4d,0x83,0x1f,0x0a,0x47,1,
        0,0,0,0,0,0,0,0x80
    };
    const uint8_t inverse_difference[] = {
        0x78, 0x41,0x03,10, 0x4d,0x83,0x13,0x04,0xc2,1,0xfd,0xff
    };
    const uint8_t no_interval[] = {
        0x78, 0x4d,0x83,0x1f,0x03,0x01,0,7
    };
    const uint8_t half_month[] = {
        0x78, 0x4d,0x83,0x1f,0x03,0x31,0xfd,7
    };
    const unsigned profile_types[] = {1, 2, 3, 4, 6, 7};
    uint8_t sized_profile[32];
    size_t i;
    assert(decode_wireless(forward, sizeof(forward)) == MBUS_OK);
    assert(result.record_count == 3 && result.records[2].is_compact_profile);
    assert(result.records[2].value_type == MBUS_VALUE_NONE && !result.records[2].has_value);
    assert(result.records[2].profile_kind == MBUS_PROFILE_FORWARD);
    assert(result.records[2].has_profile_base && result.records[2].profile_base_record == 1);
    assert(result.profile_value_count == 3);
    v = result.profile_values;
    assert(v[0].valid && v[0].integer == 123003 && fabs(v[0].value - 12300.3) < 1e-8);
    assert(v[1].integer == 123005 && v[2].integer == 123016);
    assert(decode_wireless(inverse, sizeof(inverse)) == MBUS_OK);
    assert(result.records[2].profile_kind == MBUS_PROFILE_INVERSE);
    assert(result.profile_values[0].integer == 123005);
    assert(result.profile_values[1].integer == 123003);
    assert(result.profile_values[2].integer == 123000);
    assert(decode_wireless(registers, sizeof(registers)) == MBUS_OK);
    assert(result.records[1].storage_is_register && result.records[1].tariff == 1);
    assert(result.records[1].profile_kind == MBUS_PROFILE_REGISTER);
    assert(result.profile_values[0].integer == 100000);
    assert(result.profile_values[1].integer == 130000);
    assert(decode_wireless(rtl_sample_profile, sizeof(rtl_sample_profile)) == MBUS_OK);
    assert(result.record_count == 2 && result.records[1].is_compact_profile &&
           !result.records[1].unsupported);
    assert(result.records[1].profile_interval == MBUS_PROFILE_MONTHLY_INTERVAL);
    assert(result.records[1].profile_value_count == 14);
    assert(result.profile_value_count == 14 && result.profile_values[0].valid &&
           result.profile_values[0].is_unsigned &&
           result.profile_values[0].unsigned_integer == 0);
    assert(decode_wireless(unsigned_modes, sizeof(unsigned_modes)) == MBUS_OK);
    assert(result.profile_values[0].is_unsigned &&
           result.profile_values[0].unsigned_integer == 13);
    assert(decode_wireless(decrement_mode, sizeof(decrement_mode)) == MBUS_OK);
    assert(result.profile_values[0].is_unsigned &&
           result.profile_values[0].unsigned_integer == 7);
    assert(decode_wireless(unsigned_minimum_base, sizeof(unsigned_minimum_base)) == MBUS_OK);
    assert(result.profile_values[0].valid &&
           result.profile_values[0].unsigned_integer == 129);
    assert(decode_wireless(unsigned_maximum_base, sizeof(unsigned_maximum_base)) == MBUS_OK);
    assert(!result.profile_values[0].valid);
    assert(decode_wireless(difference_mode, sizeof(difference_mode)) == MBUS_OK);
    assert(!result.profile_values[0].is_unsigned && result.profile_values[0].integer == 7);
    assert(decode_wireless(wide_unsigned, sizeof(wide_unsigned)) == MBUS_OK);
    assert(result.profile_values[0].is_unsigned &&
           result.profile_values[0].unsigned_integer == UINT64_C(0x8000000000000000));
    assert(decode_wireless(inverse_difference, sizeof(inverse_difference)) == MBUS_OK);
    assert(!result.profile_values[0].is_unsigned && result.profile_values[0].integer == 13);
    assert(decode_wireless(no_interval, sizeof(no_interval)) == MBUS_OK);
    assert(result.records[0].profile_interval == 0 && result.profile_values[0].integer == 7);
    assert(decode_wireless(half_month, sizeof(half_month)) == MBUS_OK);
    assert(result.records[0].profile_interval == MBUS_PROFILE_SEMIMONTHLY_INTERVAL &&
           result.profile_values[0].integer == 7);
    for (i = 0; i < sizeof(profile_types) / sizeof(profile_types[0]); i++) {
        size_t element_size = profile_types[i] == 6 ? 6 :
                              profile_types[i] == 7 ? 8 : profile_types[i];
        size_t length = 0, j;
        sized_profile[length++] = 0x78;
        sized_profile[length++] = 0x4d;
        sized_profile[length++] = 0x83;
        sized_profile[length++] = 0x1f;
        sized_profile[length++] = (uint8_t)(element_size + 2);
        sized_profile[length++] = (uint8_t)profile_types[i];
        sized_profile[length++] = 1;
        for (j = 0; j < element_size; j++)
            sized_profile[length++] = (uint8_t)(j == 0 ? 1 : 0);
        assert(decode_wireless(sized_profile, length) == MBUS_OK);
        assert(result.profile_value_count == 1 && result.profile_values[0].valid &&
               result.profile_values[0].value_length == element_size &&
               result.profile_values[0].integer == 1);
    }
    assert(decode_wireless(invalid, sizeof(invalid)) == MBUS_OK);
    assert(result.profile_values[0].valid && !result.profile_values[1].valid &&
           !result.profile_values[2].valid);
    assert(decode_wireless(missing_base, sizeof(missing_base)) == MBUS_OK);
    assert(result.records[0].unsupported && result.profile_value_count == 0);
    assert(decode_wireless(empty_base, sizeof(empty_base)) == MBUS_OK);
    assert(result.records[0].value_type == MBUS_VALUE_NONE && !result.records[0].has_value);
    assert(result.records[1].has_profile_base && result.profile_value_count == 1 && !result.profile_values[0].valid);
    assert(decode_wireless(bad_length, sizeof(bad_length)) == MBUS_INVALID);
    assert(decode_wireless(bad_base, sizeof(bad_base)) == MBUS_OK);
    assert(result.records[0].invalid_value && !result.profile_values[0].valid);
    assert(decode_wireless(ambiguous_base, sizeof(ambiguous_base)) == MBUS_OK);
    assert(result.records[2].unsupported && result.profile_value_count == 0);
    assert(decode_wireless(unimplemented_mode, sizeof(unimplemented_mode)) == MBUS_OK);
    assert(result.records[1].unsupported && result.profile_value_count == 0);
    assert(decode_wireless(absolute_byte, sizeof(absolute_byte)) == MBUS_OK);
    assert(!result.records[0].has_profile_base && result.profile_value_count == 3);
    assert(result.profile_values[0].valid && result.profile_values[0].integer == -5);
    assert(!result.profile_values[1].valid);
    assert(result.profile_values[2].valid && result.profile_values[2].integer == 7);
    assert(decode_wireless(binary_increment, sizeof(binary_increment)) == MBUS_OK);
    assert(result.records[0].profile_base_record == 1 && result.profile_value_count == 2);
    assert(result.profile_values[0].integer == 11 && !result.profile_values[1].valid);
    assert(decode_wireless(oms_inverse, sizeof(oms_inverse)) == MBUS_OK);
    assert(result.profile_value_count == 46 && result.records[1].storage == 3);
    assert(result.profile_values[0].integer == 1994);
    assert(result.profile_values[9].integer == 1950);
    assert(result.profile_values[45].integer == 1872);
    encrypted_length = wireless(encrypted_frame, encrypted_profile, sizeof(encrypted_profile));
    memcpy(encrypted_frame + 2, "\xae\x4c\x44\x55\x22\x33\x99\x07", 8);
    assert(mbus_decode_with_keys(&ctx, encrypted_frame, encrypted_length, MBUS_WIRELESS,
                                 &profile_key, 1, &result, &error) == MBUS_OK);
    assert(result.decrypted && result.profile_value_count == 3);
    assert(result.profile_values[0].integer == 123003 && result.profile_values[2].integer == 123016);
    assert(result.telegram[result.profile_values[0].value_offset] == 0x03);
}

static void oms_annex_n(void)
{
    /* OMS Vol. 2 Annex N, sections N.2.1, N.2.2, N.6.3 and N.6.4.
       Wireless examples have link CRC bytes removed. */
    const uint8_t gas_wireless[] = {
        0x2e,0x44,0x93,0x15,0x78,0x56,0x34,0x12,0x33,0x03,
        0x7a,0x2a,0x00,0x20,0x25,
        0x59,0x23,0xc9,0x5a,0xaa,0x26,0xd1,0xb2,0xe7,0x49,0x3b,0x01,0x3e,0xc4,0xa6,0xf6,
        0xd3,0x52,0x9b,0x52,0x0e,0xdf,0xf0,0xea,0x6d,0xef,0xc9,0x9d,0x6d,0x69,0xeb,0xf3
    };
    const uint8_t gas_wired[] = {
        0x68,0x20,0x20,0x68,0x08,0xfd,0x72,0x78,0x56,0x34,0x12,0x93,0x15,0x33,0x03,
        0x2a,0x00,0x00,0x00,0x0c,0x14,0x27,0x04,0x85,0x02,0x04,0x6d,0x32,0x37,
        0x1f,0x15,0x02,0xfd,0x17,0x00,0x00,0x89,0x16
    };
    const uint8_t hca_wireless[] = {
        0x30,0x44,0x93,0x44,0x44,0x33,0x22,0x11,0x55,0x37,0x8c,0x00,0x75,
        0x72,0x88,0x77,0x66,0x55,0x93,0x44,0x55,0x08,0x00,0x04,0x10,0x05,
        0x00,0xdf,0xe2,0xa7,0x82,0x14,0x6d,0x15,0x13,0x58,0x1c,0xd2,0xf8,0x3f,0x39,0x04,
        0x0c,0xfd,0x10,0x78,0x56,0x34,0x12
    };
    const uint8_t hca_wired[] = {
        0x68,0x2c,0x2c,0x68,0x08,0xfd,0x72,0x88,0x77,0x66,0x55,0x93,0x44,0x55,0x08,
        0x00,0x04,0x10,0x05,0x00,0xdf,0xe2,0xa7,0x82,0x14,0x6d,0x15,0x13,0x58,
        0x1c,0xd2,0xf8,0x3f,0x39,0x04,0x0c,0xfd,0x10,0x78,0x56,0x34,0x12,
        0x0c,0x78,0x44,0x33,0x22,0x11,0x26,0x16
    };
    const uint8_t gas_profile_b[] = {
        0x43,0x44,0x93,0x15,0x78,0x56,0x34,0x12,0x33,0x03,0x8c,0x20,0x75,
        0x90,0x0f,0x00,0x2c,0x25,0xb3,0x0a,0x00,0x00,0x21,0x92,0x4d,0x4f,
        0x2f,0xb6,0x6e,0x01,0x7a,0x75,0x00,0x20,0x07,0x10,
        0x90,0x58,0x47,0x5f,0x4b,0xc9,0x1d,0xf8,0x78,0xb8,0x0a,0x1b,0x0f,0x98,0xb6,0x29,
        0x02,0x4a,0xac,0x72,0x79,0x42,0xbf,0xc5,0x49,0x23,0x3c,0x01,0x40,0x82,0x9b,0x93
    };
    const mbus_key gas_key = {{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,17}};
    const mbus_key hca_key = {{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
    const uint8_t truncated_ell[] = {0x8c, 0x00};
    uint8_t transport_counter_frame[55];
    size_t i;
    for (i = 0; i < 2; i++) {
        mbus_status status = i == 0 ?
            mbus_decode_with_keys(&ctx, gas_wireless, sizeof(gas_wireless), MBUS_WIRELESS,
                                  &gas_key, 1, &result, &error) :
            mbus_decode(&ctx, gas_wired, sizeof(gas_wired), MBUS_WIRED, &result, &error);
        assert(status == MBUS_OK);
        assert(result.record_count == 3);
        assert(strcmp(result.records[0].unit, "m3") == 0);
        assert(fabs(result.records[0].value - 28504.27) < 1e-8);
        assert(result.records[1].value_type == MBUS_VALUE_DATETIME);
        assert(strcmp(result.records[1].raw_value.datetime, "2008-05-31T23:50") == 0);
        assert(strcmp(result.records[2].quantity, "error_flags") == 0);
        assert(result.records[2].raw_value.unsigned_value == 0);
        assert(result.decrypted == (i == 0));
    }
    for (i = 0; i < 2; i++) {
        const uint8_t *frame = i == 0 ? hca_wireless : hca_wired;
        size_t length = i == 0 ? sizeof(hca_wireless) : sizeof(hca_wired);
        assert(mbus_decode_with_keys(&ctx, frame, length, i == 0 ? MBUS_WIRELESS : MBUS_WIRED,
                                     &hca_key, 1, &result, &error) == MBUS_OK);
        assert(result.decrypted && !result.authentication_verified);
        assert(result.has_extended_link_header == (i == 0));
        if (i == 0) {
            assert(result.extended_link_control == 0 && result.extended_link_access_number == 0x75);
            assert(memcmp(result.link_identity.identification, "\x44\x33\x22\x11", 4) == 0);
        }
        assert(result.has_transport_identity);
        assert(memcmp(result.transport_identity.identification, "\x88\x77\x66\x55", 4) == 0);
        assert(result.record_count == (i == 0 ? 4 : 5));
        assert(result.records[0].raw_value.signed_value == 1234);
        assert(result.records[1].storage == 1);
        assert(result.records[1].value_type == MBUS_VALUE_DATETIME);
        assert(strcmp(result.records[1].raw_value.datetime, "2007-04-30") == 0);
        assert(result.records[2].storage == 1 && result.records[2].raw_value.signed_value == 23456);
        assert(strcmp(result.records[3].quantity, "customer_location") == 0);
        assert(result.records[3].raw_value.signed_value == 12345678);
        if (i == 1) {
            assert(strcmp(result.records[4].quantity, "fabrication_number") == 0);
            assert(result.records[4].raw_value.signed_value == 11223344);
        }
    }
    assert(decode_wireless(truncated_ell, sizeof(truncated_ell)) == MBUS_INVALID);
    assert(mbus_decode_with_keys(&ctx, gas_profile_b, sizeof(gas_profile_b), MBUS_WIRELESS,
                                 &hca_key, 1, &result, &error) == MBUS_UNSUPPORTED);
    assert(strstr(error, "AFL") != NULL);

    /* Reframe N.2.3's ciphertext with its counter in TPL, without the AFL MAC. */
    memcpy(transport_counter_frame, gas_profile_b, 13);
    transport_counter_frame[0] = sizeof(transport_counter_frame) - 1;
    memcpy(transport_counter_frame + 13, gas_profile_b + 30, 6);
    transport_counter_frame[17] |= 0x20;
    memcpy(transport_counter_frame + 19, gas_profile_b + 18, 4);
    memcpy(transport_counter_frame + 23, gas_profile_b + 36, 32);
    assert(mbus_decode_with_keys(&ctx, transport_counter_frame, sizeof(transport_counter_frame),
                                 MBUS_WIRELESS, &hca_key, 1, &result, &error) == MBUS_OK);
    assert(result.decrypted && !result.authentication_verified && result.message_counter == 2739);
    assert(result.record_count == 3 && fabs(result.records[0].value - 28504.27) < 1e-8);
    assert(strcmp(result.records[1].raw_value.datetime, "2008-05-31T23:50") == 0);
}

static void random_inputs(void)
{
    uint8_t frame[MBUS_MAX_TELEGRAM];
    uint32_t state = 0x12345678;
    size_t i, j;
    for (i = 0; i < 20000; i++) {
        size_t n = i % (sizeof(frame) + 1);
        for (j = 0; j < n; j++) {
            state = state * 1664525U + 1013904223U;
            frame[j] = (uint8_t)(state >> 24);
        }
        if (n >= 11 && n <= 256) {
            frame[0] = (uint8_t)(n - 1);
            frame[10] = 0x78;
        }
        (void)mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error);
        (void)mbus_decode(&ctx, frame, n, MBUS_WIRED, &result, &error);
    }
}

int main(void)
{
    mbus_context_init(&ctx);
    const char *version = mbus_version();
    const char *names[] = {"TCH", "QDS"};
    size_t i = 0;
    assert(MANUF_CODE(names[i++]) == 0x5068 && i == 1);
    assert(MANUF_CODE("QDS") == 0x4493);
    assert(MANUF_CODE("AAA") == 0x0421);
    assert(MANUF_CODE("ZZZ") == 0x6b5a);
    assert(MANUF_CODE(NULL) == MBUS_MANUFACTURER_INVALID);
    assert(MANUF_CODE("") == MBUS_MANUFACTURER_INVALID);
    assert(MANUF_CODE("TC") == MBUS_MANUFACTURER_INVALID);
    assert(MANUF_CODE("TCHX") == MBUS_MANUFACTURER_INVALID);
    assert(MANUF_CODE("Tch") == MBUS_MANUFACTURER_INVALID);
    assert(MANUF_CODE("TC1") == MBUS_MANUFACTURER_INVALID);
    assert(version != NULL && strcmp(version, MBUS_EXPECTED_VERSION) == 0);
    assert(mbus_version() == version);
    example();
    group_and_numbers();
    value_types();
    headers_and_errors();
    manufacturer_decoders();
    truncation_and_limits();
    extensions_and_large_values();
    unsupported_records();
    modifiers();
    security_mode7();
    compact_profiles();
    oms_annex_n();
    random_inputs();
    puts("library tests passed");
    return 0;
}
