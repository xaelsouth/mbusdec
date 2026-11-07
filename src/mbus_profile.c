#include "mbus_internal.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>

enum {
    PROFILE_CONTROL_TYPE_MASK = 0x0f,
    PROFILE_CONTROL_MODE_SHIFT = 6,
    PROFILE_MODE_ABSOLUTE = 0,
    PROFILE_MODE_INCREMENT = 1,
    PROFILE_MODE_DECREMENT = 2,
    PROFILE_MODE_DIFFERENCE = 3,
    PROFILE_BCD_BYTE = 9,
    PROFILE_MIN_LENGTH = 2,
    PROFILE_MAX_INTERVAL = 250,
    PROFILE_DAYS_UNIT = 3
};

static int same_base(const mbus_record *profile, const mbus_record *base)
{
    return !base->is_compact_profile && !base->unsupported &&
           profile->storage == base->storage && profile->tariff == base->tariff &&
           profile->subunit == base->subunit && profile->function == base->function &&
           profile->storage_is_register == base->storage_is_register &&
           (profile->vif & MBUS_VIF_CODE_MASK) == (base->vif & MBUS_VIF_CODE_MASK) &&
           profile->modifier_flags == base->modifier_flags &&
           profile->exponent == base->exponent && base->quantity != NULL &&
           (base->value_type == MBUS_VALUE_SIGNED || base->value_type == MBUS_VALUE_NONE);
}

static int binary_element_size(unsigned type, size_t *size)
{
    if (type >= 1 && type <= 4) {
        *size = type;
        return 1;
    }
    if (type == 6) {
        *size = 6;
        return 1;
    }
    if (type == 7) {
        *size = 8;
        return 1;
    }
    return 0;
}

static uint64_t read_unsigned(const uint8_t *p, size_t size)
{
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < size; i++)
        value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static uint64_t element_mask(size_t size)
{
    return size == sizeof(uint64_t) ? UINT64_MAX :
           (UINT64_C(1) << (size * 8)) - 1;
}

static int64_t signed_element(uint64_t raw, size_t size)
{
    unsigned bits = (unsigned)(size * 8);
    uint64_t sign = UINT64_C(1) << (bits - 1);
    if (!(raw & sign))
        return (int64_t)raw;
    if (size == sizeof(uint64_t))
        return -1 - (int64_t)(UINT64_MAX - raw);
    return (int64_t)raw - (int64_t)(UINT64_C(1) << bits);
}

static int decode_element(const uint8_t *p, unsigned type, unsigned mode,
                          size_t size, int64_t *signed_value,
                          uint64_t *unsigned_value)
{
    if (type == PROFILE_BCD_BYTE) {
        unsigned low = p[0] & 15, high = p[0] >> 4;
        if (low > 9 || high > 9)
            return 0;
        *unsigned_value = high * 10 + low;
        return 1;
    }
    {
        uint64_t raw = read_unsigned(p, size);
        uint64_t mask = element_mask(size);
        if (mode == PROFILE_MODE_INCREMENT || mode == PROFILE_MODE_DECREMENT) {
            if (raw == mask)
                return 0;
            *unsigned_value = raw;
        } else {
            if (raw == (UINT64_C(1) << (size * 8 - 1)))
                return 0;
            *signed_value = signed_element(raw, size);
        }
        return 1;
    }
}

static int base_unsigned_value(const mbus_result *result, const mbus_record *base,
                               uint64_t *value)
{
    unsigned type = base->dif & 15;
    uint64_t raw, mask;
    if (base->unsupported || base->value_type != MBUS_VALUE_SIGNED)
        return 0;
    if ((type >= 9 && type <= 12) || type == 14) {
        if (!base->has_value || base->invalid_value || base->raw_value.signed_value < 0)
            return 0;
        *value = (uint64_t)base->raw_value.signed_value;
        return 1;
    }
    if (base->value_length == 0 || base->value_length > sizeof(uint64_t) ||
        ((type < 1 || type > 4) && type != 6 && type != 7))
        return 0;
    raw = read_unsigned(result->telegram + base->value_offset, base->value_length);
    mask = element_mask(base->value_length);
    if (raw == mask)
        return 0;
    *value = raw;
    return 1;
}

mbus_status mbus_decode_profiles(mbus_result *result, const char **error)
{
    size_t i;
    for (i = 0; i < result->record_count; i++) {
        mbus_record *r = &result->records[i];
        const uint8_t *data;
        size_t j, count, element_size, base_index = 0, matches = 0;
        unsigned type, mode, interval_unit;
        int64_t signed_current = 0;
        uint64_t unsigned_current = 0;
        int valid = 1, inverse;
        if (!r->is_compact_profile || r->unsupported)
            continue;
        if (r->value_length < PROFILE_MIN_LENGTH)
            return mbus_fail(error, MBUS_INVALID, "truncated compact profile control");
        data = result->telegram + r->value_offset;
        r->profile_control = data[0];
        r->profile_interval = data[1];
        type = data[0] & PROFILE_CONTROL_TYPE_MASK;
        mode = data[0] >> PROFILE_CONTROL_MODE_SHIFT;
        interval_unit = (data[0] >> MBUS_PROFILE_INTERVAL_SHIFT) & MBUS_PROFILE_INTERVAL_MASK;
        inverse = r->profile_kind == MBUS_PROFILE_INVERSE;
        if ((!binary_element_size(type, &element_size) && type != PROFILE_BCD_BYTE) ||
            (type == PROFILE_BCD_BYTE && mode != PROFILE_MODE_INCREMENT) ||
            (r->profile_interval > PROFILE_MAX_INTERVAL &&
             !((r->profile_interval == MBUS_PROFILE_SEMIMONTHLY_INTERVAL ||
                r->profile_interval == MBUS_PROFILE_MONTHLY_INTERVAL) &&
               interval_unit == PROFILE_DAYS_UNIT))) {
            r->unsupported = 1;
            continue;
        }
        if (type == PROFILE_BCD_BYTE)
            element_size = 1;
        if ((r->value_length - PROFILE_MIN_LENGTH) % element_size != 0)
            return mbus_fail(error, MBUS_INVALID, "misaligned compact profile length");
        count = (r->value_length - PROFILE_MIN_LENGTH) / element_size;
        if (count > MBUS_MAX_PROFILE_VALUES - result->profile_value_count)
            return mbus_fail(error, MBUS_LIMIT, "too many compact profile values");
        for (j = 0; j < result->record_count; j++) {
            if (same_base(r, &result->records[j])) {
                base_index = j;
                matches++;
            }
        }
        if (matches > 1 || (mode != PROFILE_MODE_ABSOLUTE && matches != 1)) {
            r->unsupported = 1;
            continue;
        }
        if (matches == 1) {
            const mbus_record *base = &result->records[base_index];
            r->has_profile_base = 1;
            r->profile_base_record = base_index;
            if (mode == PROFILE_MODE_INCREMENT || mode == PROFILE_MODE_DECREMENT) {
                valid = base_unsigned_value(result, base, &unsigned_current);
            } else {
                valid = base->has_value && !base->invalid_value &&
                        base->value_type == MBUS_VALUE_SIGNED;
                if (valid)
                    signed_current = base->raw_value.signed_value;
            }
        }
        r->profile_value_index = result->profile_value_count;
        r->profile_value_count = count;
        for (j = 0; j < count; j++) {
            mbus_profile_value *v = &result->profile_values[result->profile_value_count++];
            size_t offset = r->value_offset + PROFILE_MIN_LENGTH + j * element_size;
            int64_t signed_element_value = 0;
            uint64_t unsigned_element_value = 0;
            int unsigned_mode = mode == PROFILE_MODE_INCREMENT || mode == PROFILE_MODE_DECREMENT;
            int element_valid = decode_element(result->telegram + offset, type, mode,
                                               element_size, &signed_element_value,
                                               &unsigned_element_value);
            v->value_offset = offset;
            v->value_length = element_size;
            v->is_unsigned = unsigned_mode;
            if (unsigned_mode) {
                v->unsigned_raw_value = unsigned_element_value;
                if (unsigned_element_value <= INT64_MAX)
                    v->raw_value = (int64_t)unsigned_element_value;
            } else {
                v->raw_value = signed_element_value;
            }
            if (mode == PROFILE_MODE_ABSOLUTE) {
                valid = element_valid;
                signed_current = signed_element_value;
            } else if (valid && element_valid &&
                       mode == PROFILE_MODE_INCREMENT) {
                if (inverse) {
                    if (unsigned_current < unsigned_element_value)
                        valid = 0;
                    else
                        unsigned_current -= unsigned_element_value;
                } else if (unsigned_current > UINT64_MAX - unsigned_element_value) {
                    valid = 0;
                } else {
                    unsigned_current += unsigned_element_value;
                }
            } else if (valid && element_valid &&
                       mode == PROFILE_MODE_DECREMENT) {
                if (inverse) {
                    if (unsigned_current > UINT64_MAX - unsigned_element_value)
                        valid = 0;
                    else
                        unsigned_current += unsigned_element_value;
                } else if (unsigned_current < unsigned_element_value) {
                    valid = 0;
                } else {
                    unsigned_current -= unsigned_element_value;
                }
            } else if (valid && element_valid &&
                       mode == PROFILE_MODE_DIFFERENCE) {
                int64_t delta = inverse ? -signed_element_value : signed_element_value;
                if ((delta > 0 && signed_current > INT64_MAX - delta) ||
                    (delta < 0 && signed_current < INT64_MIN - delta))
                    valid = 0;
                else
                    signed_current += delta;
            } else if (!element_valid || !valid) {
                valid = 0;
            }
            v->valid = valid;
            if (valid) {
                if (unsigned_mode) {
                    v->unsigned_integer = unsigned_current;
                    if (unsigned_current <= INT64_MAX)
                        v->integer = (int64_t)unsigned_current;
                    v->value = (double)unsigned_current * pow(10.0, r->exponent);
                } else {
                    v->integer = signed_current;
                    v->value = (double)signed_current * pow(10.0, r->exponent);
                }
            }
        }
    }
    return MBUS_OK;
}
