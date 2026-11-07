#ifndef MBUS_H
#define MBUS_H

#include <stddef.h>
#include <stdint.h>

#define MBUS_MAX_TELEGRAM 261
#define MBUS_MAX_RAW_WIRELESS 290
#define MBUS_MAX_RECORDS 64
#define MBUS_MAX_KEYS 16
#define MBUS_MAX_PROFILE_VALUES 256
#define MBUS_MAX_MANUFACTURER_DECODERS 32

/*
 * Encode exactly three uppercase ASCII letters as a 15-bit M-Bus manufacturer
 * code. Returns MBUS_MANUFACTURER_INVALID for NULL or invalid input. This
 * runtime expression evaluates its argument once; it is not a C constant
 * expression.
 */
#define MBUS_MANUFACTURER_INVALID 0
#define MANUF_CODE(text) mbus_manufacturer_code(text)

enum {
    MBUS_MODIFIER_AVERAGE = 1U << 0,
    MBUS_MODIFIER_MEASUREMENT_CONDITIONS = 1U << 1,
    MBUS_MODIFIER_FORWARD_FLOW = 1U << 2,
    MBUS_MODIFIER_BACKWARD_FLOW = 1U << 3,
    MBUS_MODIFIER_BASE_CONDITIONS = 1U << 4
};

enum {
    MBUS_PROFILE_INVERSE = 0x13,
    MBUS_PROFILE_REGISTER = 0x1e,
    MBUS_PROFILE_FORWARD = 0x1f,
    MBUS_PROFILE_INTERVAL_SHIFT = 4,
    MBUS_PROFILE_INTERVAL_MASK = 3,
    MBUS_PROFILE_SEMIMONTHLY_INTERVAL = 253,
    MBUS_PROFILE_MONTHLY_INTERVAL = 254
};

typedef struct {
    uint8_t bytes[16];
} mbus_key;

typedef enum {
    MBUS_WIRED,
    MBUS_WIRELESS,
    MBUS_WIRELESS_FORMAT_A,
    MBUS_WIRELESS_FORMAT_B
} mbus_format;

typedef enum {
    MBUS_OK,
    MBUS_INVALID,
    MBUS_UNSUPPORTED,
    MBUS_LIMIT
} mbus_status;

typedef struct {
    uint16_t manufacturer;
    uint8_t identification[4];
    uint8_t version;
    uint8_t medium;
} mbus_identity;

typedef enum {
    MBUS_SYNC_UNKNOWN,
    MBUS_ASYNCHRONOUS,
    MBUS_SYNCHRONOUS
} mbus_synchronization;

/*
 * Fill keys with at most capacity AES-128 keys and set count to the number
 * returned, in trial order. Zero keys is a successful lookup. application and
 * link are NULL when the corresponding identity is unavailable. The pointers
 * and key buffer are valid only for the duration of the call. On failure,
 * return a non-OK status and optionally set error to a static diagnostic.
 */
typedef mbus_status (*mbus_key_provider)(
    const mbus_identity *application, const mbus_identity *link,
    mbus_synchronization synchronization, mbus_key *keys, size_t capacity,
    size_t *count, void *context, const char **error);

typedef struct mbus_record mbus_record;
typedef struct mbus_result mbus_result;

typedef enum {
    MBUS_MANUFACTURER_CI,
    MBUS_MANUFACTURER_DIF,
    MBUS_MANUFACTURER_VIF,
    MBUS_MANUFACTURER_VIFE
} mbus_manufacturer_block_kind;

/*
 * Return MBUS_OK when the block was decoded, MBUS_UNSUPPORTED when this
 * handler declines it, or another status on a decode failure. For VIF/VIFE
 * blocks, record points to the record being decoded and a successful handler
 * must populate it as a supported record without changing its framing fields.
 * For CI/DIF blocks, record is NULL; handlers may append decoded records to
 * result, with offsets into result->telegram. Declining handlers must not
 * modify record or result.
 */
typedef mbus_status (*mbus_manufacturer_decoder)(
    mbus_manufacturer_block_kind kind, uint16_t manufacturer,
    const uint8_t *data, size_t length, size_t offset,
    mbus_record *record, mbus_result *result, void *context,
    const char **error);

typedef struct {
    mbus_identity link;
    mbus_identity transport;
    int has_link;
    int has_transport;
    int has_extended_link_header;
    uint8_t extended_link_control;
    int has_transport_header;
    uint16_t configuration;
} mbus_header_info;

typedef struct {
    /* Encoded element location in mbus_result.telegram. */
    size_t value_offset;
    size_t value_length;
    int valid;
    /* If set, use the unsigned fields; signed aliases are populated when they fit. */
    int is_unsigned;
    int64_t raw_value;
    int64_t integer;
    /* Exact encoded and reconstructed values when is_unsigned is set. */
    uint64_t unsigned_raw_value;
    uint64_t unsigned_integer;
    double value;
} mbus_profile_value;

typedef enum {
    MBUS_VALUE_NONE,
    MBUS_VALUE_SIGNED,
    MBUS_VALUE_UNSIGNED,
    MBUS_VALUE_REAL,
    MBUS_VALUE_DATETIME
} mbus_value_type;

typedef union {
    int64_t signed_value;
    uint64_t unsigned_value;
    float real_value;
    char datetime[20];
} mbus_raw_value;

struct mbus_record {
    uint64_t storage;
    uint32_t tariff;
    uint16_t subunit;
    int storage_is_register;
    uint8_t function;
    uint8_t dif;
    uint8_t vif;
    /* VIFE bytes in transmission order; vife_count == 0 means none. */
    uint8_t vifes[10];
    size_t vife_count;
    unsigned modifier_flags;
    int unsupported;
    int manufacturer_specific;
    size_t record_offset;
    size_t record_length;
    size_t value_offset;
    size_t value_length;
    int has_value;
    int invalid_value;
    /* Only read the tagged union member when has_value is set and unsupported is clear. */
    mbus_value_type value_type;
    mbus_raw_value raw_value;
    /* Scaled numeric value; the union holds the unscaled numeric value. */
    double value;
    int exponent;
    const char *quantity;
    const char *unit;
    int is_compact_profile;
    uint8_t profile_kind;
    uint8_t profile_control;
    uint8_t profile_interval;
    /* Slice in mbus_result.profile_values, and optional record index for the base. */
    size_t profile_value_index;
    size_t profile_value_count;
    int has_profile_base;
    size_t profile_base_record;
};

struct mbus_result {
    mbus_format format;
    uint8_t control;
    uint8_t address;
    mbus_identity link_identity;
    mbus_identity transport_identity;
    int has_link_identity;
    int has_transport_identity;
    int has_extended_link_header;
    uint8_t extended_link_control;
    uint8_t extended_link_access_number;
    uint8_t ci;
    int has_transport_header;
    uint8_t access_number;
    uint8_t meter_status;
    uint16_t configuration;
    int decrypted;
    /* A decryption check is not authentication; currently always false. */
    int authentication_verified;
    /* Mode 7 transport-counter parameters; key version may be absent. */
    int has_security_parameters;
    uint8_t security_key_id;
    int has_key_version;
    uint8_t security_key_version;
    uint32_t message_counter;
    uint8_t telegram[MBUS_MAX_TELEGRAM];
    size_t telegram_length;
    mbus_record records[MBUS_MAX_RECORDS];
    size_t record_count;
    mbus_profile_value profile_values[MBUS_MAX_PROFILE_VALUES];
    size_t profile_value_count;
    uint8_t manufacturer_data[MBUS_MAX_TELEGRAM];
    size_t manufacturer_data_length;
    int has_manufacturer_data;
    int manufacturer_data_decoded;
    uint16_t manufacturer_data_manufacturer;
    mbus_manufacturer_block_kind manufacturer_data_kind;
    int more_records;
};

/*
 * Return the library's project version as a NUL-terminated string.
 * The returned pointer refers to static storage and must not be freed.
 */
const char *mbus_version(void);

uint16_t mbus_manufacturer_code(const char *text);

/*
 * Register one process-global manufacturer decoder. Registrations are
 * single-threaded; at most MBUS_MAX_MANUFACTURER_DECODERS may be installed.
 * Registering an existing manufacturer replaces its decoder and context.
 * Registration changes are not allowed from inside a decoder callback.
 */
mbus_status mbus_register_manufacturer_decoder(
    uint16_t manufacturer, mbus_manufacturer_decoder decoder, void *context);

/* Remove a registered decoder; returns MBUS_INVALID when none was registered. */
mbus_status mbus_unregister_manufacturer_decoder(uint16_t manufacturer);

/*
 * Install or replace the process-global key provider. Passing NULL removes it.
 * The caller owns context and must keep it alive until removal or replacement.
 * Registration and decoding with a provider are single-threaded; registration
 * changes and recursive decoding are not allowed from within the provider.
 */
mbus_status mbus_set_key_provider(mbus_key_provider provider, void *context);

/*
 * Validate and remove link CRCs from a raw wireless Format A or B frame,
 * starting at its L field (no preamble or sync word). format must be
 * MBUS_WIRELESS_FORMAT_A or MBUS_WIRELESS_FORMAT_B. The normalized output
 * starts with an L field excluding CRCs and can be passed as MBUS_WIRELESS.
 * Input and output may overlap; out_length and error storage must not overlap
 * either buffer. On failure output is unchanged and *out_length is zero.
 * All pointers except error are required; error follows the same
 * convention as mbus_decode. A valid link frame need not contain a decodable
 * application telegram. CRC validation is not authentication.
 */
mbus_status mbus_wireless_remove_crc(const uint8_t *input, size_t length,
                                     mbus_format format, uint8_t *output,
                                     size_t capacity, size_t *out_length,
                                     const char **error);

/*
 * Decode a complete wired long frame, CRC-stripped wireless frame, or raw
 * wireless Format A/B frame. Raw wireless results have format MBUS_WIRELESS
 * and offsets refer to the normalized CRC-stripped telegram.
 * The caller owns all buffers; telegram and result must not overlap.
 * error may be NULL. Otherwise it is cleared on entry and, on failure,
 * points to a static diagnostic string that must not be freed.
 * Only inspect result when MBUS_OK is returned.
 */
mbus_status mbus_decode(const uint8_t *telegram, size_t length,
                        mbus_format format, mbus_result *result,
                        const char **error);

/*
 * As above, trying up to MBUS_MAX_KEYS AES-128 keys in order. keys may be
 * NULL only when key_count is zero. If key_count is zero, an installed key
 * provider is called after successful header parsing, even for plaintext
 * telegrams. A nonzero explicit key count suppresses the provider, including
 * when those keys fail. Decrypted payload bytes replace ciphertext in
 * result->telegram; input is unchanged. Decryption is not authentication.
 */
mbus_status mbus_decode_with_keys(const uint8_t *telegram, size_t length,
                                  mbus_format format, const mbus_key *keys,
                                  size_t key_count, mbus_result *result,
                                  const char **error);

/*
 * Extract link and transport identities without decrypting. All output
 * pointers except error are required; presence flags indicate valid identities.
 * Outputs must not be used on failure.
 */
mbus_status mbus_get_identities(const uint8_t *telegram, size_t length,
                                mbus_format format, mbus_identity *link,
                                int *has_link, mbus_identity *transport,
                                int *has_transport, const char **error);

/*
 * Extract identities, extended-link control, and transport configuration
 * without decrypting. Raw wireless formats are CRC-validated first. info is
 * required and valid only on MBUS_OK.
 */
mbus_status mbus_get_header_info(const uint8_t *telegram, size_t length,
                                 mbus_format format, mbus_header_info *info,
                                 const char **error);

#endif
