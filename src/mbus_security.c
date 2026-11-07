#include "mbus_internal.h"

#include <string.h>

#if defined(MBUS_AES_OPENSSL)
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/aes.h>
#elif defined(MBUS_AES_BUNDLED)
#include "aes.h"
#else
#error "Select an AES backend"
#endif

static void clear_sensitive(void *buffer, size_t length)
{
    volatile unsigned char *p = buffer;
    while (length--)
        *p++ = 0;
}

static int decrypt_cbc(const uint8_t *input, uint8_t *output, size_t length,
                       const uint8_t key[MBUS_AES_BLOCK_SIZE], uint8_t iv[MBUS_AES_BLOCK_SIZE])
{
#if defined(MBUS_AES_OPENSSL)
    AES_KEY context;
    int ok = AES_set_decrypt_key(key, 128, &context) == 0;
    if (ok)
        AES_cbc_encrypt(input, output, length, &context, iv, AES_DECRYPT);
#else
    struct AES_ctx context;
    int ok = 1;
    memcpy(output, input, length);
    AES_init_ctx_iv(&context, key, iv);
    AES_CBC_decrypt_buffer(&context, output, length);
#endif
    clear_sensitive(&context, sizeof(context));
    return ok;
}

static int encrypt_block(const uint8_t key[MBUS_AES_BLOCK_SIZE], uint8_t block[MBUS_AES_BLOCK_SIZE])
{
#if defined(MBUS_AES_OPENSSL)
    AES_KEY context;
    uint8_t result[MBUS_AES_BLOCK_SIZE];
    int ok = AES_set_encrypt_key(key, 128, &context) == 0;
    if (ok) {
        AES_encrypt(block, result, &context);
        memcpy(block, result, sizeof(result));
    }
    clear_sensitive(result, sizeof(result));
#else
    struct AES_ctx context;
    int ok = 1;
    AES_init_ctx(&context, key);
    AES_ECB_encrypt(&context, block);
#endif
    clear_sensitive(&context, sizeof(context));
    return ok;
}

static int derive_mode7_key(const uint8_t key[MBUS_AES_BLOCK_SIZE], const uint8_t counter[4],
                            const uint8_t identification[4], uint8_t derived[MBUS_AES_BLOCK_SIZE])
{
    uint8_t subkey[MBUS_AES_BLOCK_SIZE] = {0}, carry = 0;
    int i, ok = 0;
    if (!encrypt_block(key, subkey))
        goto done;
    for (i = 15; i >= 0; i--) {
        uint8_t next = (uint8_t)(subkey[i] >> 7);
        subkey[i] = (uint8_t)((subkey[i] << 1) | carry);
        carry = next;
    }
    if (carry)
        subkey[15] ^= 0x87;
    derived[0] = 0;
    memcpy(derived + 1, counter, 4);
    memcpy(derived + 5, identification, 4);
    memset(derived + 9, 7, 7);
    for (i = 0; i < 16; i++)
        derived[i] ^= subkey[i];
    ok = encrypt_block(key, derived);
done:
    clear_sensitive(subkey, sizeof(subkey));
    return ok;
}

mbus_status mbus_decode_security(const uint8_t *telegram, size_t *position,
                                 size_t end, const mbus_key *keys, size_t key_count,
                                 mbus_result *result, const char **error)
{
    size_t pos = *position;
    size_t mode7_encrypted_end = 0;
    if ((result->configuration & MBUS_SECURITY_MODE_MASK) == MBUS_SECURITY_MODE_5) {
        const mbus_identity *id = result->has_transport_identity ?
                                  &result->transport_identity : &result->link_identity;
        size_t encrypted_length =
            ((result->configuration & MBUS_SECURITY_BLOCK_COUNT_MASK) >>
             MBUS_SECURITY_BLOCK_COUNT_SHIFT) * MBUS_AES_BLOCK_SIZE;
        uint8_t iv[MBUS_AES_BLOCK_SIZE], trial[MBUS_MAX_TELEGRAM];
        size_t k;
        int matched = 0;
        if (!result->has_transport_identity && !result->has_link_identity)
            return mbus_fail(error, MBUS_UNSUPPORTED, "security mode 5 requires a meter identity");
        if (!key_count)
            return mbus_fail(error, MBUS_UNSUPPORTED, "security mode 5 requires an AES key");
        if (encrypted_length < MBUS_AES_BLOCK_SIZE || encrypted_length > end - pos)
            return mbus_fail(error, MBUS_INVALID, "invalid encrypted block count");
        for (k = 0; k < key_count; k++) {
            iv[0] = (uint8_t)id->manufacturer;
            iv[1] = (uint8_t)(id->manufacturer >> 8);
            memcpy(iv + 2, id->identification, 4);
            iv[6] = id->version;
            iv[7] = id->medium;
            memset(iv + 8, result->access_number, 8);
            if (!decrypt_cbc(telegram + pos, trial, encrypted_length, keys[k].bytes, iv)) {
                clear_sensitive(trial, sizeof(trial));
                return mbus_fail(error, MBUS_INVALID, "AES key initialization failed");
            }
            if (trial[0] == MBUS_DIF_FILLER && trial[1] == MBUS_DIF_FILLER) {
                memcpy(result->telegram + pos, trial, encrypted_length);
                matched = 1;
                break;
            }
        }
        clear_sensitive(trial, sizeof(trial));
        if (!matched)
            return mbus_fail(error, MBUS_INVALID, "AES verification failed for all keys");
        result->decrypted = 1;
    } else if ((result->configuration & MBUS_SECURITY_MODE_MASK) == MBUS_SECURITY_MODE_7) {
        const mbus_identity *id = result->has_transport_identity ?
                                  &result->transport_identity : &result->link_identity;
        size_t encrypted_length =
            ((result->configuration & MBUS_SECURITY_BLOCK_COUNT_MASK) >>
             MBUS_SECURITY_BLOCK_COUNT_SHIFT) * MBUS_AES_BLOCK_SIZE;
        uint8_t extension, iv[MBUS_AES_BLOCK_SIZE], trial[MBUS_MAX_TELEGRAM], derived[MBUS_AES_BLOCK_SIZE];
        uint8_t counter[4];
        size_t k, padding = 0;
        int matched = 0;
        if (!result->has_transport_identity && !result->has_link_identity)
            return mbus_fail(error, MBUS_UNSUPPORTED, "security mode 7 requires a meter identity");
        if (pos == end)
            return mbus_fail(error, MBUS_INVALID, "missing security mode 7 configuration extension");
        extension = telegram[pos++];
        if (extension & MBUS_SECURITY_EXTENSION_RESERVED)
            return mbus_fail(error, MBUS_INVALID, "reserved security mode 7 extension bit");
        if ((extension & MBUS_SECURITY_EXTENSION_KDF_MASK) != MBUS_SECURITY_EXTENSION_KDF_A)
            return mbus_fail(error, MBUS_UNSUPPORTED, "security mode 7 requires KDF A");
        result->has_security_parameters = 1;
        result->security_key_id = extension & MBUS_SECURITY_EXTENSION_KEY_ID_MASK;
        if (extension & MBUS_SECURITY_EXTENSION_KEY_VERSION) {
            if (pos == end)
                return mbus_fail(error, MBUS_INVALID, "missing security key version");
            result->has_key_version = 1;
            result->security_key_version = telegram[pos++];
        }
        if (!(result->configuration & MBUS_SECURITY_TPL_COUNTER))
            return mbus_fail(error, MBUS_UNSUPPORTED, "security mode 7 requires an AFL message counter");
        if (end - pos < 4)
            return mbus_fail(error, MBUS_INVALID, "missing security mode 7 message counter");
        memcpy(counter, telegram + pos, sizeof(counter));
        result->message_counter = (uint32_t)counter[0] | ((uint32_t)counter[1] << 8) |
                                  ((uint32_t)counter[2] << 16) | ((uint32_t)counter[3] << 24);
        pos += 4;
        if (!key_count)
            return mbus_fail(error, MBUS_UNSUPPORTED, "security mode 7 requires an AES key");
        if (encrypted_length < MBUS_AES_BLOCK_SIZE || encrypted_length > end - pos)
            return mbus_fail(error, MBUS_INVALID, "invalid encrypted block count");
        for (k = 0; k < key_count; k++) {
            memset(iv, 0, sizeof(iv));
            if (!derive_mode7_key(keys[k].bytes, counter, id->identification, derived) ||
                !decrypt_cbc(telegram + pos, trial, encrypted_length, derived, iv)) {
                clear_sensitive(derived, sizeof(derived));
                clear_sensitive(trial, sizeof(trial));
                return mbus_fail(error, MBUS_INVALID, "AES key initialization failed");
            }
            clear_sensitive(derived, sizeof(derived));
            if (trial[0] != MBUS_DIF_FILLER || trial[1] != MBUS_DIF_FILLER)
                continue;
            if (result->configuration & MBUS_SECURITY_PADDING) {
                size_t j;
                padding = trial[encrypted_length - 1];
                if (padding == 0 || padding > MBUS_AES_BLOCK_SIZE || padding > encrypted_length - 2)
                    continue;
                for (j = 0; j < padding; j++)
                    if (trial[encrypted_length - 1 - j] != padding)
                        break;
                if (j != padding)
                    continue;
            }
            memcpy(result->telegram + pos, trial, encrypted_length);
            matched = 1;
            break;
        }
        clear_sensitive(trial, sizeof(trial));
        if (!matched)
            return mbus_fail(error, MBUS_INVALID, "AES verification or padding failed for all keys");
        result->decrypted = 1;
        mode7_encrypted_end = pos + encrypted_length - padding;
        if (padding) {
            mbus_status status = mbus_decode_records(result->telegram + pos,
                                                      mode7_encrypted_end - pos, pos, result, error);
            if (status != MBUS_OK)
                return status;
            pos += encrypted_length;
            if (result->has_manufacturer_data && pos < end)
                return mbus_fail(error, MBUS_UNSUPPORTED, "manufacturer data crosses encrypted boundary");
        }
    } else if ((result->configuration & MBUS_SECURITY_MODE_MASK) != 0) {
        return mbus_fail(error, MBUS_UNSUPPORTED, "encrypted or unsupported security mode");
    } else if ((result->configuration & MBUS_SECURITY_BLOCK_COUNT_MASK) != 0) {
        return mbus_fail(error, MBUS_INVALID, "reserved security mode 0 configuration bits are set");
    }
    *position = pos;
    return MBUS_OK;
}
