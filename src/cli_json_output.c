#include "cli_json.h"
#include "mjson.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned depth;
    unsigned count[8];
} writer;

static int output(const char *buffer, int length, void *data)
{
    FILE *stream = data;
    return (int)fwrite(buffer, 1, (size_t)length, stream);
}

static void quoted(const char *s)
{
    mjson_print_str(output, stdout, s, (int)strlen(s));
}

static void separator(writer *w)
{
    if (w->count[w->depth - 1]++)
        putchar(',');
}

static void key(writer *w, const char *name)
{
    separator(w);
    quoted(name);
    putchar(':');
}

static void begin(writer *w, char c)
{
    putchar(c);
    w->count[w->depth++] = 0;
}

static void end(writer *w, char c)
{
    --w->depth;
    putchar(c);
}

static void string_field(writer *w, const char *name, const char *value)
{
    key(w, name);
    quoted(value);
}

static void unsigned_field(writer *w, const char *name, uint64_t value)
{
    key(w, name);
    printf("%" PRIu64, value);
}

static void decode_status(writer *w, cli_status status, const char *text)
{
    key(w, "decode_status");
    begin(w, '{');
    unsigned_field(w, "code", (unsigned)status);
    string_field(w, "text", text);
    end(w, '}');
}

static void receiver_field(writer *w, const cli_receiver_metadata *receiver)
{
    if (receiver == NULL || !receiver->has_rssi) return;
    key(w, "receiver");
    begin(w, '{');
    unsigned_field(w, "packet_rssi", receiver->packet_rssi);
    unsigned_field(w, "current_rssi", receiver->current_rssi);
    end(w, '}');
}

static void input_string_field(writer *w, const char *name, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    key(w, name);
    putchar('"');
    for (; *p; p++) {
        if (*p == '"' || *p == '\\') {
            putchar('\\');
            putchar(*p);
        } else if (*p < 0x20 || *p >= 0x7f) {
            printf("\\u%04x", (unsigned)*p);
        } else {
            putchar(*p);
        }
    }
    putchar('"');
}

static void timestamp_field(writer *w, const cli_receiver_metadata *receiver)
{
    if (receiver != NULL && receiver->timestamp != NULL)
        input_string_field(w, "timestamp", receiver->timestamp);
}

static void signed_field(writer *w, const char *name, int64_t value)
{
    key(w, name);
    printf("%" PRId64, value);
}

static void bool_field(writer *w, const char *name, int value)
{
    key(w, name);
    fputs(value ? "true" : "false", stdout);
}

static void null_field(writer *w, const char *name)
{
    key(w, name);
    fputs("null", stdout);
}

static void real_field(writer *w, const char *name, double value)
{
    key(w, name);
    printf("%.17g", value);
}

static void hex_field(writer *w, const char *name, const uint8_t *bytes, size_t length)
{
    static const char digits[] = "0123456789ABCDEF";
    char hex[MBUS_MAX_TELEGRAM * 2 + 1];
    size_t i;
    for (i = 0; i < length; i++) {
        hex[i * 2] = digits[bytes[i] >> 4];
        hex[i * 2 + 1] = digits[bytes[i] & 15];
    }
    hex[length * 2] = '\0';
    string_field(w, name, hex);
}

static int valid_manufacturer(uint16_t code)
{
    unsigned a = (code >> 10) & 31, b = (code >> 5) & 31, c = code & 31;
    return !(code & 0x8000) && a >= 1 && a <= 26 &&
           b >= 1 && b <= 26 && c >= 1 && c <= 26;
}

static int valid_serial(const mbus_identity *id)
{
    size_t i;
    for (i = 0; i < sizeof(id->identification); i++) {
        if ((id->identification[i] & 15) > 9 || (id->identification[i] >> 4) > 9)
            return 0;
    }
    return 1;
}

const char *cli_json_identity_error(const mbus_result *result)
{
    if (result->has_link_identity) {
        if (!valid_manufacturer(result->link_identity.manufacturer))
            return "invalid link manufacturer code";
        if (!valid_serial(&result->link_identity))
            return "invalid link serial number BCD";
    }
    if (result->has_transport_identity) {
        if (!valid_manufacturer(result->transport_identity.manufacturer))
            return "invalid transport manufacturer code";
        if (!valid_serial(&result->transport_identity))
            return "invalid transport serial number BCD";
    }
    return NULL;
}

static void manufacturer_field(writer *w, const char *name, uint16_t code)
{
    char manufacturer[4];
    manufacturer[0] = (char)('A' - 1 + ((code >> 10) & 31));
    manufacturer[1] = (char)('A' - 1 + ((code >> 5) & 31));
    manufacturer[2] = (char)('A' - 1 + (code & 31));
    manufacturer[3] = '\0';
    string_field(w, name, manufacturer);
}

static const char *device_type_name(uint8_t medium)
{
#define MEDIUM_NAME(name) [name] = #name
    static const char *const names[] = {
        MEDIUM_NAME(MEDIUM_OTHER),
        MEDIUM_NAME(MEDIUM_OIL),
        MEDIUM_NAME(MEDIUM_ELECTRICITY),
        MEDIUM_NAME(MEDIUM_GAS),
        MEDIUM_NAME(MEDIUM_HEAT_OUTLET),
        MEDIUM_NAME(MEDIUM_STEAM),
        MEDIUM_NAME(MEDIUM_WARM_WATER),
        MEDIUM_NAME(MEDIUM_WATER),
        MEDIUM_NAME(MEDIUM_HEAT_COST_ALLOCATOR),
        MEDIUM_NAME(MEDIUM_COMPRESSED_AIR),
        MEDIUM_NAME(MEDIUM_COOLING_OUTLET),
        MEDIUM_NAME(MEDIUM_COOLING_INLET),
        MEDIUM_NAME(MEDIUM_HEAT_INLET),
        MEDIUM_NAME(MEDIUM_HEAT_AND_COOLING),
        MEDIUM_NAME(MEDIUM_BUS_SYSTEM),
        MEDIUM_NAME(MEDIUM_UNKNOWN),
        MEDIUM_NAME(MEDIUM_IRRIGATION_WATER),
        MEDIUM_NAME(MEDIUM_WATER_LOGGER),
        MEDIUM_NAME(MEDIUM_GAS_LOGGER),
        MEDIUM_NAME(MEDIUM_GAS_CONVERTER),
        MEDIUM_NAME(MEDIUM_CALORIFIC_VALUE),
        MEDIUM_NAME(MEDIUM_HOT_WATER),
        MEDIUM_NAME(MEDIUM_COLD_WATER),
        MEDIUM_NAME(MEDIUM_DUAL_WATER),
        MEDIUM_NAME(MEDIUM_PRESSURE),
        MEDIUM_NAME(MEDIUM_ADC),
        MEDIUM_NAME(MEDIUM_SMOKE_DETECTOR),
        MEDIUM_NAME(MEDIUM_AMBIENT_SENSOR),
        MEDIUM_NAME(MEDIUM_GAS_DETECTOR),
        MEDIUM_NAME(MEDIUM_ELECTRICITY_BREAKER),
        MEDIUM_NAME(MEDIUM_GAS_OR_WATER_VALVE),
        MEDIUM_NAME(MEDIUM_CUSTOMER_DISPLAY),
        MEDIUM_NAME(MEDIUM_WASTE_WATER),
        MEDIUM_NAME(MEDIUM_GARBAGE),
        MEDIUM_NAME(MEDIUM_SERVICE_UNIT),
        MEDIUM_NAME(MEDIUM_RADIO_CONVERTER_SYSTEM),
        MEDIUM_NAME(MEDIUM_RADIO_CONVERTER_METER),
    };
#undef MEDIUM_NAME
    return medium < sizeof(names) / sizeof(names[0]) ? names[medium] : NULL;
}

static void identity_field(writer *w, const char *name, const mbus_identity *id, int present)
{
    const char *device_name;
    uint32_t serial = 0;
    size_t i;
    if (!present) {
        null_field(w, name);
        return;
    }
    for (i = sizeof(id->identification); i > 0; i--) {
        uint8_t byte = id->identification[i - 1];
        serial = serial * 100 + (byte >> 4) * 10 + (byte & 15);
    }
    key(w, name);
    begin(w, '{');
    unsigned_field(w, "serial_no", serial);
    manufacturer_field(w, "manufacturer", id->manufacturer);
    hex_field(w, "version", &id->version, 1);
    hex_field(w, "device_type", &id->medium, 1);
    device_name = device_type_name(id->medium);
    if (device_name != NULL)
        string_field(w, "medium", device_name);
    else
        null_field(w, "medium");
    end(w, '}');
}

static int same_group(const mbus_record *a, const mbus_record *b)
{
    return a->storage == b->storage && a->tariff == b->tariff &&
           a->function == b->function && a->subunit == b->subunit;
}

static void profile(writer *w, const mbus_result *result, const mbus_record *record)
{
    static const char *const units[] = {"s", "min", "h", "d"};
    unsigned unit = (record->profile_control >> MBUS_PROFILE_INTERVAL_SHIFT) & MBUS_PROFILE_INTERVAL_MASK;
    int monthly = record->profile_interval == MBUS_PROFILE_MONTHLY_INTERVAL;
    int semimonthly = record->profile_interval == MBUS_PROFILE_SEMIMONTHLY_INTERVAL;
    int no_interval = record->profile_interval == 0;
    size_t i;
    key(w, "profile");
    begin(w, '{');
    string_field(w, "kind", record->profile_kind == MBUS_PROFILE_INVERSE ? "inverse" :
                 record->profile_kind == MBUS_PROFILE_REGISTER ? "register" : "forward");
    unsigned_field(w, "control", record->profile_control);
    unsigned_field(w, "interval", monthly || semimonthly ? 1 : record->profile_interval);
    string_field(w, "interval_unit", monthly ? "month" : semimonthly ? "half_month" :
                 no_interval ? "none" : units[unit]);
    unsigned_field(w, "interval_raw", record->profile_interval);
    if (record->has_profile_base)
        unsigned_field(w, "base_record_index", record->profile_base_record);
    key(w, "values");
    begin(w, '[');
    for (i = 0; i < record->profile_value_count; i++) {
        const mbus_profile_value *v = &result->profile_values[record->profile_value_index + i];
        separator(w);
        begin(w, '{');
        if (v->valid) {
            real_field(w, "value", v->value);
            if (v->is_unsigned) {
                unsigned_field(w, "raw_value", v->unsigned_raw_value);
                unsigned_field(w, "decoded_integer", v->unsigned_integer);
            } else {
                signed_field(w, "raw_value", v->raw_value);
                signed_field(w, "decoded_integer", v->integer);
            }
        } else {
            null_field(w, "value");
            bool_field(w, "valid", 0);
        }
        end(w, '}');
    }
    end(w, ']');
    end(w, '}');
}

static void record(writer *w, const mbus_result *result, const mbus_record *r)
{
    static const char *const functions[] = {"instantaneous", "maximum", "minimum", "error"};
    static const char *const modifiers[] = {
        "average", "measurement_conditions", "forward_flow", "backward_flow", "base_conditions"
    };
    size_t i;
    begin(w, '{');
    string_field(w, "function", functions[r->function]);
    unsigned_field(w, "subunit", r->subunit);
    hex_field(w, "dif", &r->dif, 1);
    hex_field(w, "vif", &r->vif, 1);
    hex_field(w, "record_hex", result->telegram + r->record_offset, r->record_length);
    if (r->storage_is_register)
        bool_field(w, "storage_is_register", 1);
    if (r->manufacturer_specific)
        bool_field(w, "manufacturer_specific", 1);
    if (r->vife_count) {
        hex_field(w, "vife", &r->vifes[0], 1);
        key(w, "vifes");
        begin(w, '[');
        for (i = 0; i < r->vife_count; i++) {
            separator(w);
            printf("\"%02X\"", (unsigned)r->vifes[i]);
        }
        end(w, ']');
    }
    if (r->modifier_flags) {
        key(w, "modifiers");
        begin(w, '[');
        for (i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); i++) {
            if (r->modifier_flags & (1U << i)) {
                separator(w);
                quoted(modifiers[i]);
            }
        }
        end(w, ']');
    }
    if (r->unsupported) {
        string_field(w, "status", r->manufacturer_specific ? "undecoded" : "unsupported");
        null_field(w, "value");
    } else {
        string_field(w, "quantity", r->quantity);
        string_field(w, "unit", r->unit);
        signed_field(w, "exponent", r->exponent);
        if (r->is_compact_profile) {
            profile(w, result, r);
            null_field(w, "value");
        } else {
            if (r->has_value && r->value_type == MBUS_VALUE_DATETIME)
                string_field(w, "value", r->raw_value.datetime);
            else if (r->has_value)
                real_field(w, "value", r->value);
            else
                null_field(w, "value");
            if (r->invalid_value)
                bool_field(w, "valid", 0);
            else if (r->has_value && r->value_type == MBUS_VALUE_UNSIGNED)
                unsigned_field(w, "raw_value", r->raw_value.unsigned_value);
            else if (r->has_value && r->value_type == MBUS_VALUE_SIGNED)
                signed_field(w, "raw_value", r->raw_value.signed_value);
        }
    }
    end(w, '}');
}

void cli_json_result(const mbus_result *r, const cli_receiver_metadata *receiver)
{
    static const char *const functions[] = {"instantaneous", "maximum", "minimum", "error"};
    writer w = {0, {0}};
    size_t i, j;
    begin(&w, '{');
    string_field(&w, "format", r->format == MBUS_WIRED ? "wired" : "wireless");
    if (receiver != NULL) {
        decode_status(&w, CLI_STATUS_OK, "ok");
        timestamp_field(&w, receiver);
        receiver_field(&w, receiver);
    }
    hex_field(&w, "control", &r->control, 1);
    if (r->format == MBUS_WIRED)
        unsigned_field(&w, "address", r->address);
    identity_field(&w, "link_identity", &r->link_identity, r->has_link_identity);
    identity_field(&w, "transport_identity", &r->transport_identity, r->has_transport_identity);
    if (r->has_transport_identity) {
        string_field(&w, "primary_identity_layer", "application");
        identity_field(&w, "primary_identity", &r->transport_identity, 1);
    } else if (r->has_link_identity) {
        string_field(&w, "primary_identity_layer", "link");
        identity_field(&w, "primary_identity", &r->link_identity, 1);
    } else {
        null_field(&w, "primary_identity_layer");
        null_field(&w, "primary_identity");
    }
    if (r->has_extended_link_header) {
        key(&w, "extended_link");
        begin(&w, '{');
        {
            const uint8_t extended_ci = 0x8c;
            hex_field(&w, "ci", &extended_ci, 1);
        }
        unsigned_field(&w, "control", r->extended_link_control);
        unsigned_field(&w, "access_number", r->extended_link_access_number);
        end(&w, '}');
    }
    hex_field(&w, "ci", &r->ci, 1);
    if (r->has_transport_header) {
        unsigned_field(&w, "access_number", r->access_number);
        unsigned_field(&w, "status", r->meter_status);
        unsigned_field(&w, "configuration", r->configuration);
    }
    if (r->decrypted) {
        bool_field(&w, "decrypted", 1);
        bool_field(&w, "authenticated", r->authentication_verified);
    }
    if (r->has_security_parameters) {
        unsigned_field(&w, "key_id", r->security_key_id);
        unsigned_field(&w, "message_counter", r->message_counter);
        if (r->has_key_version)
            unsigned_field(&w, "key_version", r->security_key_version);
    }
    key(&w, "groups");
    begin(&w, '[');
    for (i = 0; i < r->record_count; i++) {
        const mbus_record *a = &r->records[i];
        for (j = 0; j < i; j++)
            if (same_group(&r->records[j], a)) break;
        if (j != i) continue;
        separator(&w);
        begin(&w, '{');
        unsigned_field(&w, "storage", a->storage);
        unsigned_field(&w, "tariff", a->tariff);
        string_field(&w, "function", functions[a->function]);
        unsigned_field(&w, "subunit", a->subunit);
        key(&w, "records");
        begin(&w, '[');
        for (j = i; j < r->record_count; j++) {
            if (same_group(&r->records[j], a)) {
                separator(&w);
                record(&w, r, &r->records[j]);
            }
        }
        end(&w, ']');
        end(&w, '}');
    }
    end(&w, ']');
    bool_field(&w, "more_records", r->more_records);
    if (r->has_manufacturer_data) {
        hex_field(&w, "manufacturer_data", r->manufacturer_data, r->manufacturer_data_length);
        string_field(&w, "manufacturer_data_status",
                     r->manufacturer_data_decoded ? "decoded" : "undecoded");
        if (r->has_link_identity || r->has_transport_identity)
            manufacturer_field(&w, "manufacturer_data_manufacturer",
                               r->manufacturer_data_manufacturer);
        else
            null_field(&w, "manufacturer_data_manufacturer");
        string_field(&w, "manufacturer_data_kind",
                     r->manufacturer_data_kind == MBUS_MANUFACTURER_CI ? "ci" :
                     r->manufacturer_data_kind == MBUS_MANUFACTURER_DIF ? "dif" :
                     r->manufacturer_data_kind == MBUS_MANUFACTURER_VIF ? "vif" : "vife");
    }
    end(&w, '}');
    putchar('\n');
}

void cli_json_error(const char *message, const char *code)
{
    writer w = {0, {0}};
    begin(&w, '{');
    string_field(&w, "error", message);
    if (code != NULL)
        string_field(&w, "code", code);
    end(&w, '}');
    putchar('\n');
}

void cli_json_telegram_error(const char *message, const char *code,
                             const uint8_t *telegram, size_t length)
{
    writer w = {0, {0}};
    begin(&w, '{');
    string_field(&w, "error", message);
    string_field(&w, "code", code);
    hex_field(&w, "datagram", telegram, length);
    end(&w, '}');
    putchar('\n');
}

void cli_json_stream_error(cli_status status, const char *message,
                           const cli_receiver_metadata *receiver,
                           const char *datagram)
{
    const char *code;
    writer w = {0, {0}};
    switch (status) {
    case CLI_STATUS_INVALID: code = "invalid"; break;
    case CLI_STATUS_UNSUPPORTED: code = "unsupported"; break;
    case CLI_STATUS_LIMIT: code = "limit"; break;
    case CLI_STATUS_CRC_FAILED: code = "crc_failed"; break;
    default: code = "invalid"; status = CLI_STATUS_INVALID; break;
    }
    begin(&w, '{');
    decode_status(&w, status, message);
    string_field(&w, "error", message);
    string_field(&w, "code", code);
    timestamp_field(&w, receiver);
    receiver_field(&w, receiver);
    if (datagram != NULL) {
        const unsigned char *p = (const unsigned char *)datagram;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            p += 2;
        input_string_field(&w, "datagram", (const char *)p);
    } else {
        null_field(&w, "datagram");
    }
    end(&w, '}');
    putchar('\n');
}
