#include "cli_keys.h"

#include <assert.h>
#include <string.h>

static mbus_context ctx;

static mbus_status fallback_provider(const mbus_identity *application,
                                     const mbus_identity *link,
                                     mbus_synchronization sync, mbus_key *keys,
                                     size_t capacity, size_t *count, void *context,
                                     const char **error)
{
    int *calls = context;
    size_t i;
    (void)error;
    assert(!application && link && link->manufacturer == 0x4cae);
    assert(sync == MBUS_ASYNCHRONOUS && capacity >= 1);
    (*calls)++;
    for (i = 0; i < 16; i++) keys[0].bytes[i] = (uint8_t)i;
    *count = 1;
    return MBUS_OK;
}

static void check(const cli_key_store *store, const uint8_t *frame, size_t length,
                  mbus_format format, const unsigned char *expected, size_t count)
{
    mbus_key keys[MBUS_MAX_KEYS];
    size_t n = 0, i;
    const char *error = NULL;
    assert(cli_keys_match(store, frame, length, format, keys, &n, &error) == MBUS_OK);
    assert(n == count);
    for (i = 0; i < count; i++) assert(keys[i].bytes[0] == expected[i]);
}

int main(void)
{
    mbus_context_init(&ctx);
    cli_key_store store = {0};
    uint8_t frame[] = {14, 0x44, 0xae, 0x4c, 0x44, 0x55, 0x22, 0x33,
                       0x99, 0x07, 0x7a, 0x55, 0, 0x10, 0x05};
    const uint8_t headerless[] = {10, 0x44, 0xae, 0x4c, 0x44, 0x55, 0x22, 0x33,
                                  0x99, 0x07, 0x78};
    uint8_t ell[] = {17, 0x44, 0xae, 0x4c, 0x44, 0x55, 0x22, 0x33,
                     0x99, 0x07, 0x8c, 0x20, 0x55, 0x7a, 0x55, 0, 0x10, 0x05};
    const unsigned char async[] = {2, 4, 1};
    const unsigned char sync[] = {3, 1};
    const unsigned char all[] = {1, 2, 3, 4};
    size_t i;
    const uint8_t address[] = {0xae, 0x4c, 0x44, 0x55, 0x22, 0x33, 0x99, 0x07};
    store.count = 4;
    for (i = 0; i < store.count; i++) {
        memcpy(store.entries[i].address, address, sizeof(address));
        store.entries[i].key.bytes[0] = (uint8_t)(i + 1);
    }
    store.entries[1].type = CLI_KEY_ASYNCHRONOUS;
    store.entries[2].type = CLI_KEY_SYNCHRONOUS;
    store.entries[3].type = CLI_KEY_ASYNCHRONOUS;
    check(&store, frame, sizeof(frame), MBUS_WIRELESS, async, sizeof(async));
    frame[14] = 0x25;
    check(&store, frame, sizeof(frame), MBUS_WIRELESS, sync, sizeof(sync));
    frame[14] = 0x27;
    check(&store, frame, sizeof(frame), MBUS_WIRELESS, all, sizeof(all));
    check(&store, headerless, sizeof(headerless), MBUS_WIRELESS, all, sizeof(all));
    check(&store, ell, sizeof(ell), MBUS_WIRELESS, sync, sizeof(sync));
    ell[11] = 0;
    ell[17] = 0x25;
    check(&store, ell, sizeof(ell), MBUS_WIRELESS, async, sizeof(async));
    ell[11] = 0x20;
    ell[17] = 0x27;
    check(&store, ell, sizeof(ell), MBUS_WIRELESS, sync, sizeof(sync));
    {
        mbus_key keys[MBUS_MAX_KEYS] = {{{0}}};
        const char *error = NULL;
        size_t count = 1;
        keys[0].bytes[0] = 99;
        assert(cli_keys_match(&store, ell, sizeof(ell), MBUS_WIRELESS,
                              keys, &count, &error) == MBUS_OK);
        assert(count == 3 && keys[0].bytes[0] == 99 &&
               keys[1].bytes[0] == 3 && keys[2].bytes[0] == 1);
        count = MBUS_MAX_KEYS;
        assert(cli_keys_match(&store, ell, sizeof(ell), MBUS_WIRELESS,
                              keys, &count, &error) == MBUS_LIMIT);
        assert(error != NULL);
    }

    /* ALA selection precedes filtering by key type and does not fall back. */
    {
        const uint8_t long_frame[] = {22, 0x44, 0xae, 0x4c, 0x44, 0x55, 0x22, 0x33,
                                      0x99, 0x07, 0x72, 0x44, 0x55, 0x22, 0x33,
                                      0xae, 0x4c, 0x99, 0x07, 0x55, 0, 0x10, 0x05};
        cli_key_store other = {0};
        other.count = 2;
        memcpy(other.entries[0].address, address, sizeof(address));
        other.entries[0].application = 1;
        other.entries[0].type = CLI_KEY_SYNCHRONOUS;
        memcpy(other.entries[1].address, address, sizeof(address));
        other.entries[1].type = CLI_KEY_ASYNCHRONOUS;
        check(&other, long_frame, sizeof(long_frame), MBUS_WIRELESS, NULL, 0);
        memcpy(other.entries[0].link_address, address, sizeof(address));
        other.entries[0].has_link_constraint = 1;
        other.entries[0].type = CLI_KEY_ASYNCHRONOUS;
        other.entries[0].key.bytes[0] = 9;
        {
            const unsigned char expected[] = {9};
            check(&other, long_frame, sizeof(long_frame), MBUS_WIRELESS, expected, sizeof(expected));
        }
        other.entries[0].link_address[0] ^= 1;
        other.entries[1].key.bytes[0] = 8;
        {
            const unsigned char expected[] = {8};
            check(&other, long_frame, sizeof(long_frame), MBUS_WIRELESS, expected, sizeof(expected));
        }
    }

    /* Wired frames have no link identity; use an application identity instead. */
    {
        const uint8_t wired_long[] = {0x68, 15, 15, 0x68, 0x08, 1, 0x72,
                                      0x44, 0x55, 0x22, 0x33, 0xae, 0x4c, 0x99, 0x07,
                                      0x55, 0, 0x10, 0x25, 0x8d, 0x16};
        for (i = 0; i < store.count; i++) store.entries[i].application = 1;
        check(&store, wired_long, sizeof(wired_long), MBUS_WIRED, all, sizeof(all));
        for (i = 0; i < store.count; i++) store.entries[i].has_link_constraint = 1;
        check(&store, wired_long, sizeof(wired_long), MBUS_WIRED, NULL, 0);
    }
    {
        const uint8_t encrypted[] = {
            46,0x44,0xae,0x4c,0x44,0x55,0x22,0x33,0x99,0x07,0x7a,0x55,0,0x20,0x05,
            0xc2,0xa0,0x67,0xed,0x64,0xfb,0x48,0xde,0xad,0x61,0x1b,0xec,0xcb,0x91,0x86,0xff,
            0x0d,0x2d,0x3f,0xbe,0xe2,0xfa,0x73,0x20,0x06,0xbf,0xb4,0xe0,0x4d,0xb3,0x08,0x0a
        };
        cli_key_store selected = {0};
        mbus_key keys[MBUS_MAX_KEYS];
        static mbus_result decoded;
        const char *error = NULL;
        size_t count = 0;
        int calls = 0;
        assert(mbus_set_key_provider(&ctx, fallback_provider, &calls) == MBUS_OK);
        assert(cli_keys_match(&selected, encrypted, sizeof(encrypted), MBUS_WIRELESS,
                              keys, &count, &error) == MBUS_OK && count == 0);
        assert(mbus_decode_with_keys(&ctx, encrypted, sizeof(encrypted), MBUS_WIRELESS,
                                     keys, count, &decoded, &error) == MBUS_OK);
        assert(calls == 1 && decoded.decrypted);
        selected.count = 1;
        memcpy(selected.entries[0].address, address, sizeof(address));
        count = 0;
        assert(cli_keys_match(&selected, encrypted, sizeof(encrypted), MBUS_WIRELESS,
                              keys, &count, &error) == MBUS_OK && count == 1);
        assert(mbus_decode_with_keys(&ctx, encrypted, sizeof(encrypted), MBUS_WIRELESS,
                                     keys, count, &decoded, &error) == MBUS_INVALID);
        assert(calls == 1);
        assert(mbus_set_key_provider(&ctx, NULL, NULL) == MBUS_OK);
    }
    return 0;
}
