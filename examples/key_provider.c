#include "mbus.h"

#include <stdio.h>
#include <string.h>

static mbus_status lookup_key(const mbus_identity *application,
                              const mbus_identity *link,
                              mbus_synchronization synchronization,
                              mbus_key *keys, size_t capacity, size_t *count,
                              void *context, const char **error)
{
    const mbus_key *test_key = context;
    const uint8_t expected_id[] = {0x44, 0x55, 0x22, 0x33};
    *count = 0;
    if (application == NULL && link != NULL &&
        link->manufacturer == MANUF_CODE("SEN") &&
        memcmp(link->identification, expected_id, sizeof(expected_id)) == 0 &&
        link->version == 0x99 && link->medium == MEDIUM_WATER &&
        synchronization == MBUS_ASYNCHRONOUS) {
        if (capacity < 1) {
            if (error != NULL) *error = "key buffer is too small";
            return MBUS_LIMIT;
        }
        keys[0] = *test_key;
        *count = 1;
    }
    return MBUS_OK;
}

int main(void)
{
    const uint8_t telegram[] = {
        46,0x44,0xae,0x4c,0x44,0x55,0x22,0x33,0x99,MEDIUM_WATER,0x7a,0x55,0,0x20,0x05,
        0xc2,0xa0,0x67,0xed,0x64,0xfb,0x48,0xde,0xad,0x61,0x1b,0xec,0xcb,0x91,0x86,0xff,
        0x0d,0x2d,0x3f,0xbe,0xe2,0xfa,0x73,0x20,0x06,0xbf,0xb4,0xe0,0x4d,0xb3,0x08,0x0a
    };
    mbus_key test_key = {{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
    static mbus_result result;
    mbus_context ctx;
    const char *error = NULL;
    mbus_status status;

    mbus_context_init(&ctx);
    if (mbus_set_key_provider(&ctx, lookup_key, &test_key) != MBUS_OK) {
        fprintf(stderr, "failed to install key provider\n");
        return 1;
    }
    status = mbus_decode(&ctx, telegram, sizeof(telegram), MBUS_WIRELESS, &result, &error);
    if (mbus_set_key_provider(&ctx, NULL, NULL) != MBUS_OK) {
        fprintf(stderr, "failed to remove key provider\n");
        return 1;
    }
    if (status != MBUS_OK) {
        fprintf(stderr, "decode failed: %s\n", error);
        return 1;
    }
    if (!result.decrypted || result.profile_value_count != 3) {
        fprintf(stderr, "unexpected decoded result\n");
        return 1;
    }
    printf("Decrypted telegram; profile values: %zu\n", result.profile_value_count);
    return 0;
}
