#include "mbus.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static mbus_result result;
static const char *error;

static mbus_status provider(const mbus_identity *application, const mbus_identity *link,
                            mbus_synchronization sync, mbus_key *keys, size_t capacity,
                            size_t *count, void *context, const char **provider_error)
{
    const uint8_t bytes[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,17};
    int *calls = context;
    (void)provider_error;
    assert(!application && link && link->manufacturer == 0x1593);
    assert(sync == MBUS_SYNCHRONOUS && capacity >= 1);
    memcpy(keys[0].bytes, bytes, sizeof(bytes));
    *count = 1;
    (*calls)++;
    return MBUS_OK;
}

/* Reference generator for synthetic boundary cases, separate from the decoder. */
static uint16_t reference_crc(const uint8_t *data, size_t length)
{
    unsigned crc = 0;
    size_t i;
    for (i = 0; i < length; i++) {
        unsigned mask;
        for (mask = 0x80; mask; mask >>= 1) {
            unsigned top = (crc >> 15) ^ ((data[i] & mask) != 0);
            crc = (crc << 1) & 0xffff;
            if (top) crc ^= 0x3d65;
        }
    }
    return (uint16_t)(crc ^ 0xffff);
}

static size_t append(uint8_t *raw, size_t pos, const uint8_t *data, size_t n)
{
    uint16_t crc = reference_crc(data, n);
    memcpy(raw + pos, data, n);
    raw[pos + n] = (uint8_t)(crc >> 8);
    raw[pos + n + 1] = (uint8_t)crc;
    return pos + n + 2;
}

static size_t make_raw(const uint8_t *data, size_t n, mbus_format format, uint8_t *raw)
{
    uint8_t copy[256];
    size_t pos = 0, offset = 0;
    memcpy(copy, data, n);
    if (format == MBUS_WIRELESS_FORMAT_A) {
        copy[0] = (uint8_t)(n - 1);
        pos = append(raw, pos, copy, 10);
        offset = 10;
        while (offset < n) {
            size_t block = n - offset < 16 ? n - offset : 16;
            pos = append(raw, pos, copy + offset, block);
            offset += block;
        }
    } else {
        copy[0] = (uint8_t)(n + (n > 126 ? 4 : 2) - 1);
        while (offset < n) {
            size_t block = n - offset < 126 ? n - offset : 126;
            pos = append(raw, pos, copy + offset, block);
            offset += block;
        }
    }
    return pos;
}

static size_t from_hex(const char *s, uint8_t *out)
{
    size_t n = 0;
    while (*s) {
        unsigned value;
        assert(sscanf(s, "%2x", &value) == 1);
        out[n++] = (uint8_t)value;
        s += 2;
    }
    return n;
}

static void upstream_vectors(void)
{
    /* wmbusmeters simulation_dll_crcs.txt, second frame, Format A. */
    const char *a =
        "3444EE4D813929271608811D7A51000000046D1912A62B036E000000"
        "5170426CE1F1436E00000002FF2C00000259D6D0D4090265FC0902FD66A00044C4";
    const char *a_plain =
        "3444EE4D8139292716087A51000000046D1912A62B036E000000"
        "426CE1F1436E00000002FF2C00000259D4090265FC0902FD66A000";
    /* wmbusmeters test_frame_b_trim.sh, two-CRC Format B. */
    const char *b =
        "C244C5140443054300048C200E900F002C25895E02003B0877D31DA840B17A0E009007100F"
        "8FAD4C4E61400DA4F3B1F7E273CC4C5C07F93CD20B237345DB39F9A98F8415E1C4B7A25"
        "C86E4C22D05C3352FA7E2E5627031F14643063F1DC8FE2C32459B6F02E4234B447B18E"
        "4BFDA8C4DA9B1EDE6CF29D0B2122A3E8B9DEEBEE3E6DBE6617B4E709EC6B595179CB01"
        "C2C0BDE6723A345B708652880C114A58DA07C73AD9EB8ACB17FA0A88F19BC84CB1FC566"
        "72DE8A9603FD0C05010002FD0B2111F60B";
    /* wmbusmeters generated_tests.xmq, C1 short capture, single CRC. */
    const char *b_short =
        "2F46C5141731425431087A0A0B00002F2F046D28344535036E000000"
        "426C5E34436E000000317F1A346D00335B340186";
    uint8_t raw[MBUS_MAX_RAW_WIRELESS], plain[256], expected[256];
    mbus_header_info info;
    size_t n, out, expected_n;

    n = from_hex(a, raw);
    expected_n = from_hex(a_plain, expected);
    assert(mbus_wireless_remove_crc(raw, n, MBUS_WIRELESS_FORMAT_A,
                                    plain, sizeof(plain), &out, &error) == MBUS_OK);
    assert(out == expected_n && memcmp(plain, expected, out) == 0);
    assert(mbus_decode(raw, n, MBUS_WIRELESS_FORMAT_A, &result, &error) == MBUS_OK);
    assert(result.format == MBUS_WIRELESS && result.telegram_length == out &&
           memcmp(result.telegram, plain, out) == 0);
    assert(mbus_get_header_info(raw, n, MBUS_WIRELESS_FORMAT_A, &info, &error) == MBUS_OK);
    assert(info.has_link && info.link.manufacturer == 0x4dee);

    n = from_hex(b_short, raw);
    assert(mbus_wireless_remove_crc(raw, n, MBUS_WIRELESS_FORMAT_B,
                                    plain, sizeof(plain), &out, &error) == MBUS_OK);
    assert(out == n - 2 && plain[0] == n - 3 && memcmp(plain + 1, raw + 1, out - 1) == 0);
    assert(mbus_get_header_info(raw, n, MBUS_WIRELESS_FORMAT_B, &info, &error) == MBUS_OK);
    assert(info.has_link && info.link.manufacturer == 0x14c5);

    n = from_hex(b, raw);
    assert(n == 195);
    assert(mbus_wireless_remove_crc(raw, n, MBUS_WIRELESS_FORMAT_B,
                                    plain, sizeof(plain), &out, &error) == MBUS_OK);
    assert(out == 191 && plain[0] == 190);
    assert(memcmp(plain + 1, raw + 1, 125) == 0);
    assert(memcmp(plain + 126, raw + 128, 65) == 0);
    assert(mbus_get_header_info(raw, n, MBUS_WIRELESS_FORMAT_B, &info, &error) == MBUS_UNSUPPORTED);
    assert(error != NULL); /* Upstream telegram uses unsupported AFL. */
}

static void boundaries(void)
{
    uint8_t original[256], raw[MBUS_MAX_RAW_WIRELESS], work[MBUS_MAX_RAW_WIRELESS];
    uint8_t out[256], sentinel[256];
    size_t n, raw_len, out_len, i, j;
    mbus_format formats[] = {MBUS_WIRELESS_FORMAT_A, MBUS_WIRELESS_FORMAT_B};
    for (i = 0; i < sizeof(original); i++) original[i] = (uint8_t)(i * 13 + 7);
    memset(sentinel, 0xa5, sizeof(sentinel));
    for (j = 0; j < 2; j++) {
        size_t max = j == 0 ? 256 : 252;
        for (n = 10; n <= max; n++) {
            original[0] = (uint8_t)(n - 1);
            raw_len = make_raw(original, n, formats[j], raw);
            assert(raw_len <= MBUS_MAX_RAW_WIRELESS);
            assert(mbus_wireless_remove_crc(raw, raw_len, formats[j], out, sizeof(out),
                                            &out_len, &error) == MBUS_OK);
            assert(out_len == n && memcmp(out, original, n) == 0);
            memcpy(work, raw, raw_len);
            assert(mbus_wireless_remove_crc(work, raw_len, formats[j], work, sizeof(work),
                                            &out_len, &error) == MBUS_OK);
            assert(out_len == n && memcmp(work, original, n) == 0);
            memcpy(work, raw, raw_len);
            assert(mbus_wireless_remove_crc(work, raw_len, formats[j], work + 1,
                                            sizeof(work) - 1, &out_len, &error) == MBUS_OK);
            assert(out_len == n && memcmp(work + 1, original, n) == 0);
            memcpy(out, sentinel, sizeof(out));
            assert(mbus_wireless_remove_crc(raw, raw_len, formats[j], out, n - 1,
                                            &out_len, &error) == MBUS_LIMIT);
            assert(out_len == 0 && error && memcmp(out, sentinel, sizeof(out)) == 0);
            assert(mbus_wireless_remove_crc(raw, raw_len - 1, formats[j], out, sizeof(out),
                                            &out_len, &error) == MBUS_INVALID);
            assert(out_len == 0 && error && memcmp(out, sentinel, sizeof(out)) == 0);
            for (i = 0; i < raw_len; i++) {
                memcpy(work, raw, raw_len);
                work[i] ^= 1;
                assert(mbus_wireless_remove_crc(work, raw_len, formats[j], out, sizeof(out),
                                                &out_len, &error) == MBUS_INVALID);
                assert(out_len == 0 && error && memcmp(out, sentinel, sizeof(out)) == 0);
            }
        }
    }
    assert(mbus_wireless_remove_crc(raw, 0, MBUS_WIRELESS_FORMAT_A, out, sizeof(out),
                                    &out_len, &error) == MBUS_INVALID);
    assert(mbus_wireless_remove_crc(raw, 291, MBUS_WIRELESS_FORMAT_A, out, sizeof(out),
                                    &out_len, &error) == MBUS_LIMIT);
    assert(mbus_wireless_remove_crc(raw, 12, MBUS_WIRELESS, out, sizeof(out),
                                    &out_len, &error) == MBUS_INVALID);
    assert(mbus_wireless_remove_crc(NULL, 12, MBUS_WIRELESS_FORMAT_A, out, sizeof(out),
                                    &out_len, &error) == MBUS_INVALID);
    assert(mbus_wireless_remove_crc(raw, 12, MBUS_WIRELESS_FORMAT_A, NULL, 0,
                                    &out_len, &error) == MBUS_INVALID);
    assert(mbus_wireless_remove_crc(raw, 12, MBUS_WIRELESS_FORMAT_A, out, sizeof(out),
                                    NULL, &error) == MBUS_INVALID);
    for (n = 129; n <= 130; n++) {
        memset(raw, 0, n);
        raw[0] = (uint8_t)(n - 1);
        assert(mbus_wireless_remove_crc(raw, n, MBUS_WIRELESS_FORMAT_B, out, sizeof(out),
                                        &out_len, &error) == MBUS_INVALID);
    }
}

static void oms_decode(void)
{
    const uint8_t gas[] = {
        0x2e,0x44,0x93,0x15,0x78,0x56,0x34,0x12,0x33,0x03,
        0x7a,0x2a,0x00,0x20,0x25,
        0x59,0x23,0xc9,0x5a,0xaa,0x26,0xd1,0xb2,0xe7,0x49,0x3b,0x01,0x3e,0xc4,0xa6,0xf6,
        0xd3,0x52,0x9b,0x52,0x0e,0xdf,0xf0,0xea,0x6d,0xef,0xc9,0x9d,0x6d,0x69,0xeb,0xf3
    };
    const mbus_key key = {{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,17}};
    uint8_t raw[MBUS_MAX_RAW_WIRELESS];
    mbus_format format;
    int calls = 0;
    assert(mbus_set_key_provider(provider, &calls) == MBUS_OK);
    for (format = MBUS_WIRELESS_FORMAT_A; format <= MBUS_WIRELESS_FORMAT_B; format++) {
        size_t n = make_raw(gas, sizeof(gas), format, raw);
        assert(mbus_decode_with_keys(raw, n, format, &key, 1, &result, &error) == MBUS_OK);
        assert(calls == (int)(format - MBUS_WIRELESS_FORMAT_A));
        assert(result.decrypted && result.format == MBUS_WIRELESS);
        assert(result.telegram_length == sizeof(gas) && result.records[0].has_value);
        assert(fabs(result.records[0].value - 28504.27) < 1e-8);
        assert(mbus_decode(raw, n, format, &result, &error) == MBUS_OK);
        assert(calls == (int)(format - MBUS_WIRELESS_FORMAT_A) + 1);
        assert(result.decrypted && fabs(result.records[0].value - 28504.27) < 1e-8);
        raw[n - 1] ^= 1;
        assert(mbus_decode(raw, n, format, &result, &error) == MBUS_INVALID);
        assert(error && strstr(error, "CRC"));
        assert(calls == (int)(format - MBUS_WIRELESS_FORMAT_A) + 1);
    }
    assert(mbus_set_key_provider(NULL, NULL) == MBUS_OK);
}

int main(void)
{
    upstream_vectors();
    boundaries();
    oms_decode();
    return 0;
}
