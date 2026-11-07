#include "mbus_internal.h"

#include <string.h>

static mbus_key_provider key_provider;
static void *key_provider_context;
static int key_provider_active;

mbus_status mbus_set_key_provider(mbus_key_provider provider, void *context)
{
    if (key_provider_active)
        return MBUS_INVALID;
    key_provider = provider;
    key_provider_context = provider ? context : NULL;
    return MBUS_OK;
}

static void clear_keys(mbus_key *keys, size_t count)
{
    volatile unsigned char *p = (volatile unsigned char *)keys;
    size_t bytes = count * sizeof(*keys);
    while (bytes--) *p++ = 0;
}

const char *mbus_version(void)
{
    return MBUS_BUILD_VERSION;
}

static void identity(mbus_identity *id, const uint8_t *p)
{
    memcpy(id->identification, p, 4);
    id->manufacturer = (uint16_t)(p[4] | ((uint16_t)p[5] << 8));
    id->version = p[6];
    id->medium = p[7];
}

static uint16_t manufacturer_code(const mbus_result *result)
{
    if (result->has_transport_identity)
        return result->transport_identity.manufacturer;
    if (result->has_link_identity)
        return result->link_identity.manufacturer;
    return 0;
}

static int manufacturer_ci(uint8_t ci)
{
    return ci >= 0xa0 && ci <= 0xbf;
}

static mbus_status read_headers(const uint8_t *telegram, size_t length,
                                mbus_format format, mbus_result *result,
                                size_t *payload, size_t *payload_end, const char **error)
{
    size_t pos, end, i;
    unsigned checksum = 0;
    uint8_t ci;
    if (format == MBUS_WIRED) {
        if (length < 9 || telegram[0] != MBUS_WIRED_START || telegram[3] != MBUS_WIRED_START ||
            telegram[1] != telegram[2] || (size_t)telegram[1] + 6 != length ||
            telegram[length - 1] != MBUS_WIRED_STOP)
            return mbus_fail(error, MBUS_INVALID, "invalid wired long frame");
        for (i = 4; i < length - 2; i++)
            checksum += telegram[i];
        if ((checksum & 255) != telegram[length - 2])
            return mbus_fail(error, MBUS_INVALID, "wired checksum mismatch");
        result->control = telegram[4];
        result->address = telegram[5];
        pos = 6;
        end = length - 2;
    } else if (format == MBUS_WIRELESS) {
        if (length < 11 || (size_t)telegram[0] + 1 != length)
            return mbus_fail(error, MBUS_INVALID, "invalid CRC-stripped wireless frame length");
        result->control = telegram[1];
        result->link_identity.manufacturer = (uint16_t)(telegram[2] | ((uint16_t)telegram[3] << 8));
        memcpy(result->link_identity.identification, telegram + 4, 4);
        result->link_identity.version = telegram[8];
        result->link_identity.medium = telegram[9];
        result->has_link_identity = 1;
        pos = 10;
        end = length;
    } else {
        return mbus_fail(error, MBUS_INVALID, "invalid telegram format");
    }
    if (pos >= end)
        return mbus_fail(error, MBUS_INVALID, "missing CI");
    ci = telegram[pos++];
    if (format == MBUS_WIRELESS && ci == MBUS_CI_ELL_SHORT) {
        if (end - pos < 3)
            return mbus_fail(error, MBUS_INVALID, "truncated extended link header or missing CI");
        result->has_extended_link_header = 1;
        result->extended_link_control = telegram[pos++];
        result->extended_link_access_number = telegram[pos++];
        ci = telegram[pos++];
    }
    result->ci = ci;
    if (ci == MBUS_CI_AFL)
        return mbus_fail(error, MBUS_UNSUPPORTED, "AFL authentication and fragmentation are not supported");
    if (ci == MBUS_CI_LONG_HEADER || ci == MBUS_CI_SHORT_HEADER) {
        size_t header = ci == MBUS_CI_LONG_HEADER ? 12 : 4;
        if (end - pos < header)
            return mbus_fail(error, MBUS_INVALID, "truncated transport header");
        if (ci == MBUS_CI_LONG_HEADER) {
            identity(&result->transport_identity, telegram + pos);
            result->has_transport_identity = 1;
            pos += 8;
        }
        result->has_transport_header = 1;
        result->access_number = telegram[pos++];
        result->meter_status = telegram[pos++];
        result->configuration = (uint16_t)(telegram[pos] | ((uint16_t)telegram[pos + 1] << 8));
        pos += 2;
    } else if (ci != MBUS_CI_NO_HEADER && !manufacturer_ci(ci)) {
        return mbus_fail(error, MBUS_UNSUPPORTED, "unsupported CI field");
    }
    *payload = pos;
    *payload_end = end;
    return MBUS_OK;
}

mbus_status mbus_get_header_info(const uint8_t *telegram, size_t length,
                                 mbus_format format, mbus_header_info *info,
                                 const char **error)
{
    mbus_result headers;
    size_t pos, end;
    mbus_status status;
    if (error != NULL) *error = NULL;
    if (!telegram || !info)
        return mbus_fail(error, MBUS_INVALID, "null argument");
    if (length > MBUS_MAX_TELEGRAM)
        return mbus_fail(error, MBUS_LIMIT, "telegram exceeds maximum length");
    memset(&headers, 0, sizeof(headers));
    status = read_headers(telegram, length, format, &headers, &pos, &end, error);
    if (status != MBUS_OK) return status;
    info->link = headers.link_identity;
    info->has_link = headers.has_link_identity;
    info->transport = headers.transport_identity;
    info->has_transport = headers.has_transport_identity;
    info->has_extended_link_header = headers.has_extended_link_header;
    info->extended_link_control = headers.extended_link_control;
    info->has_transport_header = headers.has_transport_header;
    info->configuration = headers.configuration;
    return MBUS_OK;
}

mbus_status mbus_get_identities(const uint8_t *telegram, size_t length,
                                mbus_format format, mbus_identity *link,
                                int *has_link, mbus_identity *transport,
                                int *has_transport, const char **error)
{
    mbus_header_info info;
    mbus_status status;
    if (error != NULL) *error = NULL;
    if (!link || !has_link || !transport || !has_transport)
        return mbus_fail(error, MBUS_INVALID, "null argument");
    status = mbus_get_header_info(telegram, length, format, &info, error);
    if (status != MBUS_OK) return status;
    *link = info.link;
    *has_link = info.has_link;
    *transport = info.transport;
    *has_transport = info.has_transport;
    return MBUS_OK;
}

mbus_status mbus_decode_with_keys(const uint8_t *telegram, size_t length,
                                  mbus_format format, const mbus_key *keys,
                                  size_t key_count, mbus_result *result,
                                  const char **error)
{
    size_t pos, end;
    mbus_status status;
    mbus_key provided_keys[MBUS_MAX_KEYS];
    if (error != NULL) *error = NULL;
    if (telegram == NULL || result == NULL)
        return mbus_fail(error, MBUS_INVALID, "null argument");
    if (key_count > MBUS_MAX_KEYS || (key_count && keys == NULL))
        return mbus_fail(error, MBUS_INVALID, "invalid key list");
    if (length > MBUS_MAX_TELEGRAM)
        return mbus_fail(error, MBUS_LIMIT, "telegram exceeds maximum length");
    if (key_provider_active)
        return mbus_fail(error, MBUS_INVALID, "recursive decode from key provider");
    memset(result, 0, sizeof(*result));
    memcpy(result->telegram, telegram, length);
    result->telegram_length = length;
    result->format = format;
    status = read_headers(telegram, length, format, result, &pos, &end, error);
    if (status != MBUS_OK) return status;
    if (key_count == 0 && key_provider != NULL) {
        size_t count = 0;
        key_provider_active = 1;
        status = key_provider(
            result->has_transport_identity ? &result->transport_identity : NULL,
            result->has_link_identity ? &result->link_identity : NULL,
            mbus_header_sync(format, result->has_extended_link_header,
                             result->extended_link_control, result->has_transport_header,
                             result->configuration),
            provided_keys, MBUS_MAX_KEYS, &count, key_provider_context, error);
        key_provider_active = 0;
        if (status != MBUS_OK) {
            clear_keys(provided_keys, MBUS_MAX_KEYS);
            if (error != NULL && *error == NULL) *error = "key provider failed";
            return status;
        }
        if (error != NULL) *error = NULL;
        if (count > MBUS_MAX_KEYS) {
            clear_keys(provided_keys, MBUS_MAX_KEYS);
            return mbus_fail(error, MBUS_LIMIT, "key provider returned too many keys");
        }
        keys = provided_keys;
        key_count = count;
    }
    if (manufacturer_ci(result->ci)) {
        int handled = 0;
        size_t original_record_count = result->record_count;
        result->has_manufacturer_data = 1;
        result->manufacturer_data_kind = MBUS_MANUFACTURER_CI;
        result->manufacturer_data_manufacturer = manufacturer_code(result);
        result->manufacturer_data_length = end - pos;
        memcpy(result->manufacturer_data, telegram + pos, end - pos);
        clear_keys(provided_keys, MBUS_MAX_KEYS);
        if (result->has_transport_identity || result->has_link_identity) {
            status = mbus_dispatch_manufacturer_decoder(
                MBUS_MANUFACTURER_CI, result->manufacturer_data_manufacturer,
                result->telegram + pos, end - pos, pos, NULL, result,
                &handled, error);
            if (status != MBUS_OK)
                return status;
        }
        status = mbus_validate_manufacturer_records(result, original_record_count, error);
        if (status != MBUS_OK)
            return status;
        result->manufacturer_data_decoded = handled;
        return mbus_decode_profiles(result, error);
    }
    if (result->has_transport_header) {
        status = mbus_decode_security(telegram, &pos, end, keys, key_count, result, error);
        clear_keys(provided_keys, MBUS_MAX_KEYS);
        if (status != MBUS_OK) return status;
    } else clear_keys(provided_keys, MBUS_MAX_KEYS);
    {
        mbus_status status = mbus_decode_records(result->telegram + pos, end - pos,
                                                 pos, result, error);
        if (status != MBUS_OK)
            return status;
    }
    return mbus_decode_profiles(result, error);
}

mbus_status mbus_decode(const uint8_t *telegram, size_t length,
                        mbus_format format, mbus_result *result,
                        const char **error)
{
    return mbus_decode_with_keys(telegram, length, format, NULL, 0, result, error);
}
