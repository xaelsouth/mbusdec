#ifndef MBUS_INTERNAL_H
#define MBUS_INTERNAL_H

#include "mbus.h"

enum {
    MBUS_WIRED_START = 0x68,
    MBUS_WIRED_STOP = 0x16,
    MBUS_CI_LONG_HEADER = 0x72,
    MBUS_CI_NO_HEADER = 0x78,
    MBUS_CI_SHORT_HEADER = 0x7a,
    MBUS_CI_ELL_SHORT = 0x8c,
    MBUS_CI_AFL = 0x90,

    MBUS_DIF_FILLER = 0x2f,
    MBUS_DIF_MANUFACTURER = 0x0f,
    MBUS_DIF_MORE_RECORDS = 0x1f,
    MBUS_DIF_TYPE_MASK = 0x0f,
    MBUS_DIF_TYPE_SPECIAL = 0x0f,
    MBUS_DIF_TYPE_VARIABLE = 0x0d,
    MBUS_DIF_TYPE_REAL = 0x05,
    MBUS_DIF_TYPE_FIRST_BCD = 0x09,
    MBUS_DIF_TYPE_NO_DATA = 0x00,
    MBUS_DIF_TYPE_SELECTION = 0x08,
    MBUS_EXTENSION_BIT = 0x80,
    MBUS_VIF_CODE_MASK = 0x7f,
    MBUS_VIF_MANUFACTURER = 0x7f,
    MBUS_VIF_CUSTOM = 0x7c,
    MBUS_VIF_DATE = 0x6c,
    MBUS_VIF_DATETIME = 0x6d,
    MBUS_VIF_EXT_FB = 0xfb,
    MBUS_VIF_EXT_FD = 0xfd,
    MBUS_VIFE_CUSTOMER_LOCATION = 0x10,
    MBUS_VIFE_ERROR_FLAGS = 0x17,
    MBUS_VIFE_DIMENSIONLESS = 0x3a,
    MBUS_VIFE_AVERAGE = 0x12,
    MBUS_VIFE_PROFILE = 0x13,
    MBUS_VIFE_LIMIT_FIRST = 0x1e,
    MBUS_VIFE_LIMIT_SECOND = 0x1f,
    MBUS_VIFE_MEASUREMENT_CONDITIONS = 0x3a,
    MBUS_VIFE_FORWARD_FLOW = 0x3b,
    MBUS_VIFE_BACKWARD_FLOW = 0x3c,
    MBUS_VIFE_BASE_CONDITIONS = 0x3e,
    MBUS_VIFE_FACTOR_FIRST = 0x70,
    MBUS_VIFE_FACTOR_LAST = 0x77,
    MBUS_VIFE_FACTOR_THOUSAND = 0x7d,
    MBUS_VIFE_COMPACT_PROFILE = 0x7c,
    MBUS_VIFE_MANUFACTURER = 0x7f,
    MBUS_VIFE_DATA_CONTAINER = 0x3b,
    MBUS_VIFE_SPECIAL_CONTAINER = 0x76,

    MBUS_VIF_ENERGY_WH = 0x00,
    MBUS_VIF_ENERGY_J = 0x08,
    MBUS_VIF_VOLUME = 0x10,
    MBUS_VIF_MASS = 0x18,
    MBUS_VIF_ON_TIME = 0x20,
    MBUS_VIF_OPERATING_TIME = 0x24,
    MBUS_VIF_POWER_W = 0x28,
    MBUS_VIF_POWER_J_PER_HOUR = 0x30,
    MBUS_VIF_VOLUME_FLOW_HOUR = 0x38,
    MBUS_VIF_VOLUME_FLOW_MINUTE = 0x40,
    MBUS_VIF_VOLUME_FLOW_SECOND = 0x48,
    MBUS_VIF_MASS_FLOW = 0x50,
    MBUS_VIF_TEMPERATURE = 0x58,
    MBUS_VIF_HEAT_COST = 0x6e,
    MBUS_VIF_AVERAGING_DURATION = 0x70,
    MBUS_VIF_ACTUALITY_DURATION = 0x74,
    MBUS_VIF_FABRICATION_NUMBER = 0x78,

    MBUS_LVAR_TEXT_MAX = 0xbf,
    MBUS_LVAR_POSITIVE_BCD = 0xc0,
    MBUS_LVAR_POSITIVE_BCD_MAX = 0xc9,
    MBUS_LVAR_NEGATIVE_BCD = 0xd0,
    MBUS_LVAR_NEGATIVE_BCD_MAX = 0xd9,
    MBUS_LVAR_BINARY = 0xe0,
    MBUS_LVAR_BINARY_MAX = 0xef,
    MBUS_LVAR_FLOAT = 0xf0,
    MBUS_LVAR_FLOAT_MAX = 0xf4,
    MBUS_LVAR_FLOAT_BASE = 0xec,
    MBUS_LVAR_FLOAT_48 = 0xf5,
    MBUS_LVAR_FLOAT_64 = 0xf6,

    MBUS_SECURITY_MODE_MASK = 0x1f00,
    MBUS_SECURITY_MODE_5 = 0x0500,
    MBUS_SECURITY_MODE_7 = 0x0700,
    MBUS_CONFIG_SYNCHRONIZED = 0x2000,
    MBUS_ELL_SYNCHRONIZED = 0x20,
    MBUS_SECURITY_BLOCK_COUNT_MASK = 0x00f0,
    MBUS_SECURITY_BLOCK_COUNT_SHIFT = 4,
    MBUS_SECURITY_TPL_COUNTER = 0x2000,
    MBUS_SECURITY_PADDING = 0x0008,
    MBUS_SECURITY_EXTENSION_RESERVED = 0x80,
    MBUS_SECURITY_EXTENSION_KEY_VERSION = 0x40,
    MBUS_SECURITY_EXTENSION_KDF_MASK = 0x30,
    MBUS_SECURITY_EXTENSION_KDF_A = 0x10,
    MBUS_SECURITY_EXTENSION_KEY_ID_MASK = 0x0f,
    MBUS_AES_BLOCK_SIZE = 16,
    MBUS_ID_SIZE = 4
};

static inline mbus_status mbus_fail(const char **error, mbus_status status,
                                    const char *message)
{
    if (error != NULL)
        *error = message;
    return status;
}

static inline mbus_synchronization mbus_header_sync(
    mbus_format format, int has_ell, uint8_t ell_control,
    int has_transport, uint16_t configuration)
{
    if (format != MBUS_WIRELESS && format != MBUS_WIRELESS_FORMAT_A &&
        format != MBUS_WIRELESS_FORMAT_B)
        return MBUS_SYNC_UNKNOWN;
    if (has_ell)
        return (ell_control & MBUS_ELL_SYNCHRONIZED) ?
               MBUS_SYNCHRONOUS : MBUS_ASYNCHRONOUS;
    if (has_transport &&
        ((configuration & MBUS_SECURITY_MODE_MASK) == 0 ||
         (configuration & MBUS_SECURITY_MODE_MASK) == MBUS_SECURITY_MODE_5))
        return (configuration & MBUS_CONFIG_SYNCHRONIZED) ?
               MBUS_SYNCHRONOUS : MBUS_ASYNCHRONOUS;
    return MBUS_SYNC_UNKNOWN;
}

mbus_status mbus_decode_records(mbus_context *ctx, const uint8_t *data, size_t length,
                                size_t base, mbus_result *result, const char **error);
mbus_status mbus_decode_security(mbus_context *ctx, const uint8_t *telegram, size_t *position,
                                 size_t end, const mbus_key *keys, size_t key_count,
                                 mbus_result *result, const char **error);
mbus_status mbus_decode_profiles(mbus_result *result, const char **error);
mbus_status mbus_dispatch_manufacturer_decoder(
    mbus_context *ctx, mbus_manufacturer_block_kind kind, uint16_t manufacturer,
    const uint8_t *data, size_t length, size_t offset,
    mbus_record *record, mbus_result *result, int *handled,
    const char **error);
mbus_status mbus_validate_manufacturer_records(const mbus_result *result,
                                               size_t first,
                                               const char **error);

#endif
