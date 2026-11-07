#include "mbus_internal.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int describe(mbus_record *r, unsigned code)
{
    static const char *const times[] = {"s", "min", "h", "d"};
    unsigned c = code & MBUS_VIF_CODE_MASK;
    r->exponent = 0;
    if (c < MBUS_VIF_ENERGY_J) {
        r->quantity = "energy"; r->unit = "Wh"; r->exponent = (int)c - 3;
    } else if (c < MBUS_VIF_VOLUME) {
        r->quantity = "energy"; r->unit = "J"; r->exponent = (int)(c & 7);
    } else if (c < MBUS_VIF_MASS) {
        r->quantity = "volume"; r->unit = "m3"; r->exponent = (int)(c & 7) - 6;
    } else if (c < MBUS_VIF_ON_TIME) {
        r->quantity = "mass"; r->unit = "kg"; r->exponent = (int)(c & 7) - 3;
    } else if (c < MBUS_VIF_POWER_W) {
        r->quantity = c < MBUS_VIF_OPERATING_TIME ? "on_time" : "operating_time";
        r->unit = times[c & 3];
    } else if (c < MBUS_VIF_POWER_J_PER_HOUR) {
        r->quantity = "power"; r->unit = "W"; r->exponent = (int)(c & 7) - 3;
    } else if (c < MBUS_VIF_VOLUME_FLOW_HOUR) {
        r->quantity = "power"; r->unit = "J/h"; r->exponent = (int)(c & 7);
    } else if (c < MBUS_VIF_VOLUME_FLOW_MINUTE) {
        r->quantity = "volume_flow"; r->unit = "m3/h"; r->exponent = (int)(c & 7) - 6;
    } else if (c < MBUS_VIF_VOLUME_FLOW_SECOND) {
        r->quantity = "volume_flow"; r->unit = "m3/min"; r->exponent = (int)(c & 7) - 7;
    } else if (c < MBUS_VIF_MASS_FLOW) {
        r->quantity = "volume_flow"; r->unit = "m3/s"; r->exponent = (int)(c & 7) - 9;
    } else if (c < MBUS_VIF_TEMPERATURE) {
        r->quantity = "mass_flow"; r->unit = "kg/h"; r->exponent = (int)(c & 7) - 3;
    } else if (c < MBUS_VIF_DATE) {
        static const char *const names[] = {
            "flow_temperature", "return_temperature", "temperature_difference",
            "external_temperature", "pressure"
        };
        unsigned i = (c - MBUS_VIF_TEMPERATURE) / 4;
        r->quantity = names[i];
        r->unit = i == 2 ? "K" : i == 4 ? "bar" : "degC";
        r->exponent = (int)(c & 3) - 3;
    } else if (c == MBUS_VIF_HEAT_COST) {
        r->quantity = "heat_cost_allocation"; r->unit = "1";
    } else if (c >= MBUS_VIF_AVERAGING_DURATION && c < MBUS_VIF_FABRICATION_NUMBER) {
        r->quantity = c < MBUS_VIF_ACTUALITY_DURATION ? "averaging_duration" : "actuality_duration";
        r->unit = times[c & 3];
    } else if (c == MBUS_VIF_FABRICATION_NUMBER) {
        r->quantity = "fabrication_number"; r->unit = "1";
    } else {
        return 0;
    }
    return 1;
}

static int decode_date(mbus_record *r, const uint8_t *data, unsigned type)
{
    unsigned day, month, year, hour = 0, minute = 0, century = 0;
    unsigned max_day;
    const uint8_t *date;
    static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int datetime = r->vif == MBUS_VIF_DATETIME;
    if ((!datetime && type != 2) || (datetime && type != 4))
        return 0;
    r->value_type = MBUS_VALUE_DATETIME;
    date = data + (datetime ? 2 : 0);
    if ((date[0] == 0xff && date[1] == 0xff) ||
        (datetime && (data[0] & 0x80))) {
        r->invalid_value = 1;
        return 1;
    }
    day = date[0] & 31;
    month = date[1] & 15;
    year = (date[0] >> 5) | ((date[1] >> 4) << 3);
    if (day == 0 || month == 15 || year == 127)
        return 0;
    if (year > 99 || month == 0 || month > 12)
        return 0;
    if (datetime) {
        minute = data[0] & 63;
        hour = data[1] & 31;
        century = (data[1] >> 5) & 3;
        if (minute > 59 || hour > 23)
            return 0;
    }
    year += century ? 1900 + 100 * century : (year <= 80 ? 2000 : 1900);
    max_day = days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
        max_day++;
    if (day > max_day)
        return 0;
    r->has_value = 1;
    r->quantity = datetime ? "date_time" : "date";
    r->unit = "1";
    if (datetime)
        snprintf(r->raw_value.datetime, sizeof(r->raw_value.datetime), "%04u-%02u-%02uT%02u:%02u",
                 year, month, day, hour, minute);
    else
        snprintf(r->raw_value.datetime, sizeof(r->raw_value.datetime), "%04u-%02u-%02u", year, month, day);
    return 1;
}

mbus_status mbus_decode_records(mbus_context *ctx, const uint8_t *p, size_t n, size_t base,
                                mbus_result *r, const char **error)
{
    static const uint8_t lengths[16] = {0, 1, 2, 3, 4, 4, 6, 8, 0, 1, 2, 3, 4, 0, 6, 0};
    size_t pos = 0;
    while (pos < n) {
        size_t start = pos, data_start;
        uint8_t dif = p[pos++], ext;
        unsigned count = 0, type = dif & MBUS_DIF_TYPE_MASK, len, i;
        uint64_t raw = 0, bcd = 0, multiplier = 1;
        mbus_record *rec;
        if (dif == MBUS_DIF_FILLER)
            continue;
        if (dif == MBUS_DIF_MANUFACTURER || dif == MBUS_DIF_MORE_RECORDS) {
            int handled = 0;
            size_t original_record_count = r->record_count;
            r->more_records = dif == MBUS_DIF_MORE_RECORDS;
            r->has_manufacturer_data = 1;
            r->manufacturer_data_length = n - pos;
            memcpy(r->manufacturer_data, p + pos, n - pos);
            r->manufacturer_data_kind = MBUS_MANUFACTURER_DIF;
            r->manufacturer_data_manufacturer = r->has_transport_identity ?
                r->transport_identity.manufacturer : r->link_identity.manufacturer;
            if (r->has_transport_identity || r->has_link_identity) {
                mbus_status status = mbus_dispatch_manufacturer_decoder(
                    ctx, MBUS_MANUFACTURER_DIF, r->manufacturer_data_manufacturer,
                    r->telegram + base + pos, n - pos, base + pos, NULL, r,
                    &handled, error);
                if (status != MBUS_OK)
                    return status;
            }
            {
                mbus_status status = mbus_validate_manufacturer_records(
                    r, original_record_count, error);
                if (status != MBUS_OK)
                    return status;
            }
            r->manufacturer_data_decoded = handled;
            return MBUS_OK;
        }
        if (type == MBUS_DIF_TYPE_SPECIAL)
            return mbus_fail(error, MBUS_UNSUPPORTED, "unsupported special DIF");
        if (r->record_count == MBUS_MAX_RECORDS)
            return mbus_fail(error, MBUS_LIMIT, "too many records");
        rec = &r->records[r->record_count];
        memset(rec, 0, sizeof(*rec));
        rec->dif = dif;
        rec->function = (dif >> 4) & 3;
        rec->storage = (dif >> 6) & 1;
        ext = dif;
        while (ext & MBUS_EXTENSION_BIT) {
            if (count == 10)
                return mbus_fail(error, MBUS_INVALID, "too many DIF extensions");
            if (pos == n)
                return mbus_fail(error, MBUS_INVALID, "truncated DIF extension");
            ext = p[pos++];
            rec->storage |= (uint64_t)(ext & 15) << (1 + 4 * count);
            rec->tariff |= (uint32_t)((ext >> 4) & 3) << (2 * count);
            rec->subunit |= (uint16_t)(((ext >> 6) & 1) << count);
            count++;
        }
        rec->storage_is_register = count > 0 && ext == 0;
        if (pos == n)
            return mbus_fail(error, MBUS_INVALID, "missing VIF");
        rec->vif = p[pos++];
        if ((rec->vif & MBUS_VIF_CODE_MASK) == MBUS_VIF_MANUFACTURER) {
            rec->manufacturer_specific = 1;
            rec->unsupported = 1;
        }
        ext = rec->vif;
        count = 0;
        while (ext & MBUS_EXTENSION_BIT) {
            if (count == 10)
                return mbus_fail(error, MBUS_INVALID, "too many VIF extensions");
            if (pos == n)
                return mbus_fail(error, MBUS_INVALID, "truncated VIF extension");
            ext = p[pos++];
            rec->vifes[count++] = ext;
        }
        rec->vife_count = count;
        for (i = rec->vif == MBUS_VIF_EXT_FB || rec->vif == MBUS_VIF_EXT_FD ? 1U : 0U; i < count; i++) {
            unsigned modifier = rec->vifes[i] & MBUS_VIF_CODE_MASK;
            if (modifier == MBUS_PROFILE_INVERSE || modifier == MBUS_PROFILE_REGISTER ||
                modifier == MBUS_PROFILE_FORWARD) {
                if (type != MBUS_DIF_TYPE_VARIABLE || rec->is_compact_profile || rec->storage == 0)
                    return mbus_fail(error, MBUS_UNSUPPORTED, "unsupported compact profile header");
                rec->is_compact_profile = 1;
                rec->profile_kind = (uint8_t)modifier;
            } else if (modifier == MBUS_VIFE_MANUFACTURER) {
                rec->manufacturer_specific = 1;
            } else if (modifier == MBUS_VIFE_COMPACT_PROFILE) {
                return mbus_fail(error, MBUS_UNSUPPORTED, "unsupported VIFE may change record boundaries");
            }
        }
        if ((rec->vif & MBUS_VIF_CODE_MASK) == MBUS_VIF_CUSTOM) {
            size_t text_length;
            if (pos == n)
                return mbus_fail(error, MBUS_INVALID, "missing custom VIF length");
            text_length = p[pos++];
            if (n - pos < text_length)
                return mbus_fail(error, MBUS_INVALID, "truncated custom VIF");
            pos += text_length;
            rec->unsupported = 1;
        } else if (rec->manufacturer_specific) {
            rec->unsupported = 1;
        } else if (rec->vif == MBUS_VIF_EXT_FD && count == 1) {
            if (rec->vifes[0] == MBUS_VIFE_CUSTOMER_LOCATION) {
                rec->quantity = "customer_location"; rec->unit = "1";
            } else if (rec->vifes[0] == MBUS_VIFE_ERROR_FLAGS) {
                rec->quantity = "error_flags"; rec->unit = "1";
            } else if (rec->vifes[0] == MBUS_VIFE_DIMENSIONLESS) {
                rec->quantity = "dimensionless"; rec->unit = "1";
            } else {
                rec->unsupported = 1;
            }
        } else if (rec->vif != MBUS_VIF_EXT_FB && rec->vif != MBUS_VIF_EXT_FD) {
            unsigned base_vif = rec->vif & MBUS_VIF_CODE_MASK;
            if (rec->is_compact_profile &&
                (base_vif == MBUS_VIF_DATE || base_vif == MBUS_VIF_DATETIME))
                rec->unsupported = 1;
            if (base_vif != MBUS_VIF_DATE && base_vif != MBUS_VIF_DATETIME && !describe(rec, base_vif))
                rec->unsupported = 1;
            for (i = 0; i < count; i++) {
                unsigned modifier = rec->vifes[i] & MBUS_VIF_CODE_MASK;
                if (modifier == MBUS_PROFILE_INVERSE || modifier == MBUS_PROFILE_REGISTER ||
                    modifier == MBUS_PROFILE_FORWARD) {
                    continue;
                } else if (base_vif == MBUS_VIF_DATE || base_vif == MBUS_VIF_DATETIME) {
                    rec->unsupported = 1;
                } else if (modifier >= MBUS_VIFE_FACTOR_FIRST && modifier <= MBUS_VIFE_FACTOR_LAST) {
                    rec->exponent += (int)(modifier & 7) - 6;
                } else if (modifier == MBUS_VIFE_FACTOR_THOUSAND) {
                    rec->exponent += 3;
                } else if (modifier == MBUS_VIFE_AVERAGE) {
                    rec->modifier_flags |= MBUS_MODIFIER_AVERAGE;
                } else if (modifier >= MBUS_VIFE_MEASUREMENT_CONDITIONS && modifier <= MBUS_VIFE_BACKWARD_FLOW) {
                    rec->modifier_flags |= 1U << (modifier - MBUS_VIFE_MEASUREMENT_CONDITIONS + 1);
                } else if (modifier == MBUS_VIFE_BASE_CONDITIONS) {
                    rec->modifier_flags |= MBUS_MODIFIER_BASE_CONDITIONS;
                } else {
                    rec->unsupported = 1;
                }
            }
        } else {
            rec->unsupported = 1;
        }
        if (type == MBUS_DIF_TYPE_VARIABLE) {
            unsigned lvar;
            if (pos == n)
                return mbus_fail(error, MBUS_INVALID, "missing variable data length");
            lvar = p[pos++];
            if (rec->vif == MBUS_VIF_EXT_FD && rec->vife_count &&
                ((rec->vifes[0] & MBUS_VIF_CODE_MASK) == MBUS_VIFE_DATA_CONTAINER || (rec->vifes[0] & MBUS_VIF_CODE_MASK) == MBUS_VIFE_SPECIAL_CONTAINER) &&
                lvar > MBUS_LVAR_TEXT_MAX)
                return mbus_fail(error, MBUS_UNSUPPORTED, "unsupported data container length");
            if (lvar <= MBUS_LVAR_TEXT_MAX) len = lvar;
            else if (lvar >= MBUS_LVAR_POSITIVE_BCD && lvar <= MBUS_LVAR_POSITIVE_BCD_MAX) len = lvar - MBUS_LVAR_POSITIVE_BCD;
            else if (lvar >= MBUS_LVAR_NEGATIVE_BCD && lvar <= MBUS_LVAR_NEGATIVE_BCD_MAX) len = lvar - MBUS_LVAR_NEGATIVE_BCD;
            else if (lvar >= MBUS_LVAR_BINARY && lvar <= MBUS_LVAR_BINARY_MAX) len = lvar - MBUS_LVAR_BINARY;
            else if (lvar >= MBUS_LVAR_FLOAT && lvar <= MBUS_LVAR_FLOAT_MAX) len = 4 * (lvar - MBUS_LVAR_FLOAT_BASE);
            else if (lvar == MBUS_LVAR_FLOAT_48) len = 48;
            else if (lvar == MBUS_LVAR_FLOAT_64) len = 64;
            else return mbus_fail(error, MBUS_UNSUPPORTED, "unknown variable data length");
            if (!rec->is_compact_profile || lvar > MBUS_LVAR_TEXT_MAX)
                rec->unsupported = 1;
        } else {
            len = lengths[type];
        }
        if (n - pos < len)
            return mbus_fail(error, MBUS_INVALID, "truncated record value");
        data_start = pos;
        rec->record_offset = base + start;
        rec->record_length = pos + len - start;
        rec->value_offset = base + data_start;
        rec->value_length = len;
        if (rec->vife_count && rec->vif == MBUS_VIF_EXT_FD && rec->vifes[0] == MBUS_VIFE_ERROR_FLAGS &&
            type != MBUS_DIF_TYPE_NO_DATA && type != MBUS_DIF_TYPE_SELECTION && (type > 7 || type == MBUS_DIF_TYPE_REAL))
            rec->unsupported = 1;
        if (rec->manufacturer_specific) {
            int handled = 0;
            mbus_record original = *rec;
            mbus_manufacturer_block_kind kind =
                (rec->vif & MBUS_VIF_CODE_MASK) == MBUS_VIF_MANUFACTURER ?
                MBUS_MANUFACTURER_VIF : MBUS_MANUFACTURER_VIFE;
            mbus_identity *identity = r->has_transport_identity ?
                &r->transport_identity : &r->link_identity;
            size_t original_count = r->record_count;
            mbus_status status = MBUS_OK;
            if (r->has_transport_identity || r->has_link_identity)
                status = mbus_dispatch_manufacturer_decoder(
                    ctx, kind, identity->manufacturer, r->telegram + base + data_start, len,
                    base + data_start, rec, r, &handled, error);
            if (status != MBUS_OK)
                return status;
            if (r->record_count != original_count || r->record_count > MBUS_MAX_RECORDS)
                return mbus_fail(error, MBUS_INVALID,
                                 "manufacturer record decoder changed the record list");
            if (handled) {
                rec->manufacturer_specific = 1;
                if (rec->record_offset != original.record_offset ||
                    rec->record_length != original.record_length ||
                    rec->value_offset != original.value_offset ||
                    rec->value_length != original.value_length ||
                    rec->dif != original.dif || rec->vif != original.vif ||
                    rec->vife_count != original.vife_count ||
                    memcmp(rec->vifes, original.vifes, sizeof(rec->vifes)) != 0 ||
                    rec->storage != original.storage || rec->tariff != original.tariff ||
                    rec->subunit != original.subunit || rec->function != original.function)
                    return mbus_fail(error, MBUS_INVALID,
                                     "manufacturer decoder changed record framing");
                if (rec->unsupported || rec->quantity == NULL || rec->unit == NULL)
                    return mbus_fail(error, MBUS_INVALID,
                                     "manufacturer decoder returned an incomplete record");
                pos += len;
                r->record_count++;
                continue;
            } else {
                *rec = original;
                rec->unsupported = 1;
            }
        }
        if (rec->unsupported) {
            pos += len;
            r->record_count++;
            continue;
        }
        if (rec->is_compact_profile) {
            pos += len;
            r->record_count++;
            continue;
        }
        if (rec->vif == MBUS_VIF_DATE || rec->vif == MBUS_VIF_DATETIME) {
            if (!decode_date(rec, p + pos, type))
                rec->unsupported = 1;
            else if (rec->invalid_value) {
                rec->quantity = rec->vif == MBUS_VIF_DATE ? "date" : "date_time";
                rec->unit = "1";
            }
            pos += len;
            r->record_count++;
            continue;
        }
        if (type != MBUS_DIF_TYPE_NO_DATA && type != MBUS_DIF_TYPE_SELECTION) {
            rec->has_value = 1;
            for (i = 0; i < len; i++)
                raw |= (uint64_t)p[pos + i] << (8 * i);
            if (rec->vif == MBUS_VIF_EXT_FD && rec->vife_count && rec->vifes[0] == MBUS_VIFE_ERROR_FLAGS) {
                rec->value_type = MBUS_VALUE_UNSIGNED;
                rec->raw_value.unsigned_value = raw;
                rec->value = (double)raw;
            } else if (type >= MBUS_DIF_TYPE_FIRST_BCD) {
                int negative = ((raw >> (len * 8 - 4)) & 15) == 15;
                rec->value_type = MBUS_VALUE_SIGNED;
                for (i = 0; i < len * 2; i++) {
                    unsigned digit = (unsigned)((raw >> (i * 4)) & 15);
                    if (negative && i == len * 2 - 1)
                        break;
                    if (digit > 9) {
                        rec->invalid_value = 1;
                        break;
                    }
                    bcd += digit * multiplier;
                    multiplier *= 10;
                }
                rec->raw_value.signed_value = negative ? -(int64_t)bcd : (int64_t)bcd;
            } else if (type == MBUS_DIF_TYPE_REAL) {
#if FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128
                uint32_t bits = (uint32_t)raw;
                float value;
                if (sizeof(value) != sizeof(bits)) {
                    rec->unsupported = 1;
                    rec->has_value = 0;
                    pos += len;
                    r->record_count++;
                    continue;
                }
                memcpy(&value, &bits, sizeof(value));
                rec->value_type = MBUS_VALUE_REAL;
                rec->raw_value.real_value = value;
                if (isfinite(value))
                    rec->value = value;
                else
                    rec->invalid_value = 1;
#else
                rec->unsupported = 1;
                rec->has_value = 0;
                pos += len;
                r->record_count++;
                continue;
#endif
            } else {
                rec->value_type = MBUS_VALUE_SIGNED;
                if (raw == (UINT64_C(1) << (len * 8 - 1)))
                    rec->invalid_value = 1;
                if (len < 8 && (raw & (UINT64_C(1) << (len * 8 - 1))))
                    raw |= UINT64_MAX << (len * 8);
                if (raw <= INT64_MAX)
                    rec->raw_value.signed_value = (int64_t)raw;
                else if (raw == (UINT64_C(1) << 63))
                    rec->raw_value.signed_value = INT64_MIN;
                else
                    rec->raw_value.signed_value = -(int64_t)(~raw + 1);
            }
            if (rec->value_type == MBUS_VALUE_SIGNED)
                rec->value = (double)rec->raw_value.signed_value;
            rec->value *= pow(10.0, rec->exponent);
            if (rec->invalid_value)
                rec->has_value = 0;
        }
        pos += len;
        r->record_count++;
    }
    return MBUS_OK;
}
