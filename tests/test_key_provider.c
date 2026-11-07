#include "mbus.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

static mbus_context ctx;

typedef struct {
    int calls;
    int has_application;
    int has_link;
    mbus_synchronization sync;
    mbus_status status;
    size_t count;
    int reenter;
} provider_state;

static mbus_result result, nested_result;
static const char *error;

static mbus_status provider(const mbus_identity *application,
                            const mbus_identity *link,
                            mbus_synchronization sync, mbus_key *keys,
                            size_t capacity, size_t *count, void *context,
                            const char **provider_error)
{
    static const uint8_t nested[] = {10, 0x44, 0x2d, 0x2c, 0x12, 0x34, 0x56, 0x78, 1, 7, 0x78};
    provider_state *state = context;
    size_t i;
    assert(capacity == MBUS_MAX_KEYS && *count == 0);
    state->calls++;
    state->has_application = application != NULL;
    state->has_link = link != NULL;
    state->sync = sync;
    if (application) assert(application->manufacturer == 0x1234);
    if (link) assert(link->manufacturer == 0x2c2d || link->manufacturer == 0x4cae);
    if (state->reenter) {
        const char *nested_error = NULL;
        assert(mbus_set_key_provider(&ctx, NULL, NULL) == MBUS_INVALID);
        assert(mbus_decode(&ctx, nested, sizeof(nested), MBUS_WIRELESS,
                           &nested_result, &nested_error) == MBUS_INVALID);
        assert(nested_error != NULL);
    }
    if (state->status != MBUS_OK) {
        if (state->status == MBUS_LIMIT) *provider_error = "lookup limit";
        return state->status;
    }
    memset(keys, 0, capacity * sizeof(*keys));
    if (state->count >= 2 && state->count <= capacity)
        for (i = 0; i < 16; i++) keys[1].bytes[i] = (uint8_t)i;
    *count = state->count;
    return MBUS_OK;
}

static size_t wireless(uint8_t *frame, const uint8_t *payload, size_t length)
{
    const uint8_t header[] = {0, 0x44, 0x2d, 0x2c, 0x12, 0x34, 0x56, 0x78, 1, 7};
    memcpy(frame, header, sizeof(header));
    memcpy(frame + sizeof(header), payload, length);
    frame[0] = (uint8_t)(sizeof(header) + length - 1);
    return sizeof(header) + length;
}

int main(void)
{
    mbus_context_init(&ctx);
    const uint8_t encrypted[] = {
        0x7a,0x55,0x00,0x20,0x05,
        0xc2,0xa0,0x67,0xed,0x64,0xfb,0x48,0xde,0xad,0x61,0x1b,0xec,0xcb,0x91,0x86,0xff,
        0x0d,0x2d,0x3f,0xbe,0xe2,0xfa,0x73,0x20,0x06,0xbf,0xb4,0xe0,0x4d,0xb3,0x08,0x0a
    };
    const uint8_t long_header[] = {0x72, 0x87,0x65,0x43,0x21,0x34,0x12,2,4, 3,4,0,0};
    const uint8_t ell[] = {0x8c,0x20,1,0x7a,1,0,0,0};
    const uint8_t headerless[] = {0x78};
    const uint8_t manufacturer[] = {0xa0};
    const uint8_t wired[] = {0x68,3,3,0x68,8,1,0x78,0x81,0x16};
    const mbus_key wrong_key = {{0}};
    provider_state state = {0};
    uint8_t frame[MBUS_MAX_TELEGRAM];
    size_t n;
    int calls;

    assert(mbus_set_key_provider(&ctx, provider, &state) == MBUS_OK);
    state.count = 2;
    state.reenter = 1;
    n = wireless(frame, encrypted, sizeof(encrypted));
    memcpy(frame + 2, "\xae\x4c\x44\x55\x22\x33\x99\x07", 8);
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    assert(state.calls == 1 && result.decrypted && result.profile_value_count == 3);
    assert(mbus_decode_with_keys(&ctx, frame, n, MBUS_WIRELESS, &wrong_key, 1,
                                 &result, &error) == MBUS_INVALID);
    assert(state.calls == 1);
    state.reenter = 0;
    state.count = 0;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_UNSUPPORTED);
    assert(state.calls == 2 && state.has_link && !state.has_application &&
           state.sync == MBUS_ASYNCHRONOUS);

    n = wireless(frame, long_header, sizeof(long_header));
    state.reenter = 1;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    assert(state.calls == 3 && state.has_application && state.has_link &&
           state.sync == MBUS_ASYNCHRONOUS);
    state.reenter = 0;
    frame[n - 1] = 0x20;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    assert(state.sync == MBUS_SYNCHRONOUS);
    frame[n - 1] = 0x27;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_INVALID);
    assert(state.sync == MBUS_SYNC_UNKNOWN);
    n = wireless(frame, ell, sizeof(ell));
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    assert(state.sync == MBUS_SYNCHRONOUS);
    frame[11] = 0;
    frame[n - 1] = 0x20;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    assert(state.sync == MBUS_ASYNCHRONOUS);
    n = wireless(frame, headerless, sizeof(headerless));
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    assert(state.sync == MBUS_SYNC_UNKNOWN && state.has_link && !state.has_application);
    n = wireless(frame, manufacturer, sizeof(manufacturer));
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    assert(result.has_manufacturer_data);
    assert(mbus_decode(&ctx, wired, sizeof(wired), MBUS_WIRED, &result, &error) == MBUS_OK);
    assert(state.sync == MBUS_SYNC_UNKNOWN && !state.has_link && !state.has_application);
    {
        uint8_t wired_long[21] = {0x68,15,15,0x68,8,1};
        unsigned checksum = 0;
        size_t i;
        memcpy(wired_long + 6, long_header, sizeof(long_header));
        for (i = 4; i < 19; i++) checksum += wired_long[i];
        wired_long[19] = (uint8_t)checksum;
        wired_long[20] = 0x16;
        assert(mbus_decode(&ctx, wired_long, sizeof(wired_long), MBUS_WIRED,
                           &result, &error) == MBUS_OK);
        assert(state.sync == MBUS_SYNC_UNKNOWN && !state.has_link && state.has_application);
    }
    state.status = MBUS_LIMIT;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_LIMIT);
    assert(strcmp(error, "lookup limit") == 0);
    state.status = MBUS_INVALID;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_INVALID);
    assert(strcmp(error, "key provider failed") == 0);
    state.status = MBUS_OK;
    state.count = MBUS_MAX_KEYS + 1;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_LIMIT);
    assert(strstr(error, "too many") != NULL);
    state.count = MBUS_MAX_KEYS;
    assert(mbus_decode(&ctx, frame, n, MBUS_WIRELESS, &result, &error) == MBUS_OK);
    state.count = 0;
    calls = state.calls;
    assert(mbus_decode(&ctx, frame, 1, MBUS_WIRELESS, &result, &error) == MBUS_INVALID);
    assert(state.calls == calls);
    assert(mbus_set_key_provider(&ctx, NULL, NULL) == MBUS_OK);
    return 0;
}
