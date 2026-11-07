#include "cli_keys.h"
#include "mbus_internal.h"
#include "mjson.h"

#include <stdio.h>
#include <string.h>

#define KEY_FILE_LIMIT 32768

void cli_clear_sensitive(void *buffer, size_t length)
{
    volatile unsigned char *p = buffer;
    while (length--) *p++ = 0;
}

int cli_hex_decode(const char *text, size_t length, uint8_t *out, size_t capacity,
                   size_t *out_length)
{
    size_t i;
    if ((length & 1) || length / 2 > capacity) return 0;
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        unsigned n;
        if (c >= '0' && c <= '9') n = c - '0';
        else if (c >= 'a' && c <= 'f') n = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') n = c - 'A' + 10;
        else return 0;
        if (!(i & 1)) out[i / 2] = (uint8_t)(n << 4);
        else out[i / 2] |= (uint8_t)n;
    }
    *out_length = length / 2;
    return 1;
}

enum parse_state { START, ARRAY_FIRST, ARRAY_ITEM, OBJECT_FIRST, OBJECT_FIELD,
                   COLON, VALUE, OBJECT_AFTER, ARRAY_AFTER, DONE,
                   KEYS_FIRST, KEYS_ITEM, KEY_OBJECT_FIRST, KEY_OBJECT_FIELD,
                   KEY_COLON, KEY_VALUE, KEY_OBJECT_AFTER, KEYS_AFTER,
                   ID_FIRST, ID_FIELD, ID_COLON, ID_VALUE, ID_AFTER };
enum { HAS_ALA = 1, HAS_KEY = 2, HAS_TYPE = 4, HAS_KEYS = 8, HAS_LLA = 16 };
enum { ID_SERIAL = 1, ID_DEVICE = 2, ID_VERSION = 4, ID_MANUFACTURER = 8 };
enum skip_state { SKIP_ARRAY_FIRST, SKIP_ARRAY_VALUE, SKIP_ARRAY_AFTER,
                  SKIP_OBJECT_FIRST, SKIP_OBJECT_KEY, SKIP_OBJECT_COLON,
                  SKIP_OBJECT_VALUE, SKIP_OBJECT_AFTER };
typedef struct {
    enum parse_state state;
    cli_key_store *store;
    cli_key_entry entry;
    unsigned seen, field, key_seen, id_seen, id_field, id_target;
    size_t entry_start;
    enum skip_state skip[MJSON_MAX_DEPTH];
    size_t skip_depth;
    int failed, filter;
} parser;

static int literal(const char *s, int length, const char *wanted)
{
    return length == (int)strlen(wanted) + 2 && s[0] == '"' &&
           s[length - 1] == '"' && memcmp(s + 1, wanted, (size_t)length - 2) == 0;
}

static int identity_value(parser *p, int event, const char *s, int len)
{
    uint8_t *address = p->id_target == HAS_ALA ? p->entry.address : p->entry.link_address;
    size_t n;
    if (p->id_field == ID_SERIAL) {
        unsigned value = 0;
        int i, start = 0, digits = len;
        if (event == MJSON_TOK_STRING) {
            if (len != 10) return 0;
            start = 1;
            digits = 8;
        } else if (event != MJSON_TOK_NUMBER || len < 1 || len > 8 ||
                   (len > 1 && s[0] == '0')) return 0;
        for (i = 0; i < digits; i++) {
            char c = s[start + i];
            if (c < '0' || c > '9') return 0;
            value = value * 10U + (unsigned)(c - '0');
        }
        for (i = 0; i < 4; i++) {
            address[2 + i] = (uint8_t)((value % 10U) | (((value / 10U) % 10U) << 4));
            value /= 100U;
        }
        return 1;
    }
    if (event != MJSON_TOK_STRING) return 0;
    if (p->id_field == ID_MANUFACTURER) {
        uint16_t code;
        char name[4];
        if (len != 5) return 0;
        memcpy(name, s + 1, 3);
        name[3] = '\0';
        code = MANUF_CODE(name);
        if (code == MBUS_MANUFACTURER_INVALID) return 0;
        address[0] = (uint8_t)code;
        address[1] = (uint8_t)(code >> 8);
        return 1;
    }
    if (len != 4) return 0;
    return cli_hex_decode(s + 1, 2, address + (p->id_field == ID_VERSION ? 6 : 7),
                          1, &n) && n == 1;
}

static int json_string(const char *s, int len)
{
    int i;
    if (len < 2 || s[0] != '"' || s[len - 1] != '"') return 0;
    for (i = 1; i < len - 1; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == '"') return 0;
        if (c == '\\') {
            if (++i >= len - 1) return 0;
            c = (unsigned char)s[i];
            if (c == 'u') {
                int j;
                for (j = 0; j < 4; j++) {
                    if (++i >= len - 1) return 0;
                    c = (unsigned char)s[i];
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                          (c >= 'A' && c <= 'F'))) return 0;
                }
            } else if (!strchr("\"\\/bfnrt", c)) return 0;
        }
    }
    return 1;
}

static int json_number(const char *s, int len)
{
    int i = 0;
    if (i < len && s[i] == '-') i++;
    if (i == len) return 0;
    if (s[i] == '0') i++;
    else {
        if (s[i] < '1' || s[i] > '9') return 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') i++;
    }
    if (i < len && s[i] == '.') {
        i++;
        if (i == len || s[i] < '0' || s[i] > '9') return 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') i++;
    }
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < len && (s[i] == '+' || s[i] == '-')) i++;
        if (i == len || s[i] < '0' || s[i] > '9') return 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') i++;
    }
    return i == len;
}

static int skip_value(parser *p, int event, const char *s, int len)
{
    if (event == '[' || event == '{') {
        if (p->skip_depth == MJSON_MAX_DEPTH) return 0;
        p->skip[p->skip_depth++] = event == '[' ? SKIP_ARRAY_FIRST : SKIP_OBJECT_FIRST;
        return 1;
    }
    if (event == MJSON_TOK_STRING) return json_string(s, len);
    if (event == MJSON_TOK_NUMBER) return json_number(s, len);
    return event == MJSON_TOK_TRUE || event == MJSON_TOK_FALSE || event == MJSON_TOK_NULL;
}

static int skip_event(parser *p, int event, const char *s, int len)
{
    enum skip_state *state = &p->skip[p->skip_depth - 1];
    if ((*state == SKIP_ARRAY_FIRST && event == ']') ||
        (*state == SKIP_ARRAY_AFTER && event == ']') ||
        (*state == SKIP_OBJECT_FIRST && event == '}') ||
        (*state == SKIP_OBJECT_AFTER && event == '}')) {
        if (--p->skip_depth == 0) p->state = OBJECT_AFTER;
        return 1;
    }
    if (*state == SKIP_ARRAY_FIRST || *state == SKIP_ARRAY_VALUE) {
        *state = SKIP_ARRAY_AFTER;
        return skip_value(p, event, s, len);
    }
    if (*state == SKIP_ARRAY_AFTER && event == ',') *state = SKIP_ARRAY_VALUE;
    else if ((*state == SKIP_OBJECT_FIRST || *state == SKIP_OBJECT_KEY) &&
             event == MJSON_TOK_KEY && json_string(s, len)) *state = SKIP_OBJECT_COLON;
    else if (*state == SKIP_OBJECT_COLON && event == ':') *state = SKIP_OBJECT_VALUE;
    else if (*state == SKIP_OBJECT_VALUE) {
        *state = SKIP_OBJECT_AFTER;
        return skip_value(p, event, s, len);
    } else if (*state == SKIP_OBJECT_AFTER && event == ',') *state = SKIP_OBJECT_KEY;
    else return 0;
    return 1;
}

static int parse_event(int event, const char *s, int off, int len, void *data)
{
    parser *p = data;
    if (p->skip_depth) {
        if (!skip_event(p, event, s + off, len)) goto invalid;
        return 0;
    }
    if (p->filter && p->state == VALUE &&
        (p->field == HAS_KEY || p->field == HAS_KEYS || p->field == HAS_TYPE)) {
        if (!skip_value(p, event, s + off, len)) goto invalid;
        if (!p->skip_depth) p->state = OBJECT_AFTER;
        return 0;
    }
    if (p->state == START && event == '[') p->state = ARRAY_FIRST;
    else if ((p->state == ARRAY_FIRST || p->state == ARRAY_ITEM) && event == '{') {
        memset(&p->entry, 0, sizeof(p->entry));
        p->seen = 0;
        p->entry_start = p->store->count;
        p->state = OBJECT_FIRST;
    } else if ((p->state == OBJECT_FIRST || p->state == OBJECT_FIELD) && event == MJSON_TOK_KEY) {
        if (literal(s + off, len, "application_layer_address") || literal(s + off, len, "ala"))
            p->field = HAS_ALA;
        else if (literal(s + off, len, "link_layer_address") || literal(s + off, len, "lla"))
            p->field = HAS_LLA;
        else if (literal(s + off, len, "key")) p->field = HAS_KEY;
        else if (literal(s + off, len, "type")) p->field = HAS_TYPE;
        else if (literal(s + off, len, "keys")) p->field = HAS_KEYS;
        else goto invalid;
        if (p->seen & p->field) goto invalid;
        p->seen |= p->field;
        p->state = COLON;
    } else if (p->state == COLON && event == ':') p->state = VALUE;
    else if (p->state == VALUE && p->field == HAS_KEYS && event == '[')
        p->state = KEYS_FIRST;
    else if (p->state == VALUE && (p->field == HAS_ALA || p->field == HAS_LLA) && event == '{') {
        p->id_target = p->field;
        p->id_seen = 0;
        p->state = ID_FIRST;
    }
    else if (p->state == VALUE && event == MJSON_TOK_STRING) {
        size_t n;
        if (p->field == HAS_KEYS) goto invalid;
        if (p->field == HAS_TYPE) {
            if (literal(s + off, len, "synchronous")) p->entry.type = CLI_KEY_SYNCHRONOUS;
            else if (literal(s + off, len, "asynchronous")) p->entry.type = CLI_KEY_ASYNCHRONOUS;
            else goto invalid;
            p->state = OBJECT_AFTER;
            return 0;
        }
        uint8_t *destination = p->field == HAS_ALA ? p->entry.address :
                               p->field == HAS_LLA ? p->entry.link_address : p->entry.key.bytes;
        size_t expected = p->field == HAS_ALA || p->field == HAS_LLA ?
                          sizeof(p->entry.address) : sizeof(p->entry.key.bytes);
        if (p->field == HAS_LLA && len == 2) {
            p->state = OBJECT_AFTER;
            return 0;
        }
        if (len < 2 || !cli_hex_decode(s + off + 1, (size_t)len - 2, destination, expected, &n) ||
            n != expected) goto invalid;
        if (p->field == HAS_ALA) p->entry.application = 1;
        if (p->field == HAS_LLA) p->entry.has_link_constraint = 1;
        p->state = OBJECT_AFTER;
    } else if ((p->state == ID_FIRST || p->state == ID_FIELD) && event == MJSON_TOK_KEY) {
        if (literal(s + off, len, "serial_no") || literal(s + off, len, "serialNo"))
            p->id_field = ID_SERIAL;
        else if (literal(s + off, len, "device_type"))
            p->id_field = ID_DEVICE;
        else if (literal(s + off, len, "version")) p->id_field = ID_VERSION;
        else if (literal(s + off, len, "manufacturer")) p->id_field = ID_MANUFACTURER;
        else goto invalid;
        if (p->id_seen & p->id_field) goto invalid;
        p->id_seen |= p->id_field;
        p->state = ID_COLON;
    } else if (p->state == ID_COLON && event == ':') p->state = ID_VALUE;
    else if (p->state == ID_VALUE && (event == MJSON_TOK_NUMBER || event == MJSON_TOK_STRING)) {
        if (!identity_value(p, event, s + off, len)) goto invalid;
        p->state = ID_AFTER;
    } else if (p->state == ID_AFTER && event == ',') p->state = ID_FIELD;
    else if (p->state == ID_AFTER && event == '}') {
        if (p->id_seen != (ID_SERIAL | ID_DEVICE | ID_VERSION | ID_MANUFACTURER))
            goto invalid;
        if (p->id_target == HAS_ALA) p->entry.application = 1;
        else p->entry.has_link_constraint = 1;
        p->state = OBJECT_AFTER;
    } else if (p->state == OBJECT_AFTER && event == ',') p->state = OBJECT_FIELD;
    else if (p->state == OBJECT_AFTER && event == '}') {
        size_t i;
        if (!p->entry.application && !p->entry.has_link_constraint) goto invalid;
        if (!p->entry.application) {
            memcpy(p->entry.address, p->entry.link_address, sizeof(p->entry.address));
            p->entry.has_link_constraint = 0;
        }
        if (!p->filter && (p->seen & HAS_KEYS)) {
            if ((p->seen & (HAS_KEY | HAS_TYPE)) || p->store->count == p->entry_start)
                goto invalid;
            for (i = p->entry_start; i < p->store->count; i++) {
                memcpy(p->store->entries[i].address, p->entry.address, sizeof(p->entry.address));
                memcpy(p->store->entries[i].link_address, p->entry.link_address,
                       sizeof(p->entry.link_address));
                p->store->entries[i].application = p->entry.application;
                p->store->entries[i].has_link_constraint = p->entry.has_link_constraint;
            }
        } else {
            if ((!p->filter && !(p->seen & HAS_KEY)) ||
                p->store->count == CLI_MAX_KEY_ENTRIES)
                goto invalid;
            p->store->entries[p->store->count++] = p->entry;
        }
        cli_clear_sensitive(&p->entry, sizeof(p->entry));
        p->state = ARRAY_AFTER;
    } else if ((p->state == KEYS_FIRST || p->state == KEYS_ITEM) && event == '{') {
        p->key_seen = 0;
        cli_clear_sensitive(&p->entry.key, sizeof(p->entry.key));
        p->entry.type = CLI_KEY_UNTYPED;
        p->state = KEY_OBJECT_FIRST;
    } else if ((p->state == KEY_OBJECT_FIRST || p->state == KEY_OBJECT_FIELD) &&
               event == MJSON_TOK_KEY) {
        if (literal(s + off, len, "key")) p->field = HAS_KEY;
        else if (literal(s + off, len, "type")) p->field = HAS_TYPE;
        else goto invalid;
        if (p->key_seen & p->field) goto invalid;
        p->key_seen |= p->field;
        p->state = KEY_COLON;
    } else if (p->state == KEY_COLON && event == ':') p->state = KEY_VALUE;
    else if (p->state == KEY_VALUE && event == MJSON_TOK_STRING) {
        size_t n;
        if (p->field == HAS_TYPE) {
            if (literal(s + off, len, "synchronous") || literal(s + off, len, "s"))
                p->entry.type = CLI_KEY_SYNCHRONOUS;
            else if (literal(s + off, len, "asynchronous") || literal(s + off, len, "a"))
                p->entry.type = CLI_KEY_ASYNCHRONOUS;
            else goto invalid;
        } else if (len < 2 ||
                   !cli_hex_decode(s + off + 1, (size_t)len - 2, p->entry.key.bytes,
                                   sizeof(p->entry.key.bytes), &n) ||
                   n != sizeof(p->entry.key.bytes)) goto invalid;
        p->state = KEY_OBJECT_AFTER;
    } else if (p->state == KEY_OBJECT_AFTER && event == ',') p->state = KEY_OBJECT_FIELD;
    else if (p->state == KEY_OBJECT_AFTER && event == '}') {
        if (!(p->key_seen & HAS_KEY) || p->store->count == CLI_MAX_KEY_ENTRIES)
            goto invalid;
        p->store->entries[p->store->count++] = p->entry;
        cli_clear_sensitive(&p->entry.key, sizeof(p->entry.key));
        p->state = KEYS_AFTER;
    } else if (p->state == KEYS_AFTER && event == ',') p->state = KEYS_ITEM;
    else if (p->state == KEYS_AFTER && event == ']') p->state = OBJECT_AFTER;
    else if (p->state == ARRAY_AFTER && event == ',') p->state = ARRAY_ITEM;
    else if ((p->state == ARRAY_FIRST || p->state == ARRAY_AFTER) && event == ']') p->state = DONE;
    else goto invalid;
    return 0;
invalid:
    p->failed = 1;
    return 1;
}

static int load_store(cli_key_store *store, const char *path, const char **error,
                      int filter)
{
    static char buffer[KEY_FILE_LIMIT + 1];
    FILE *f = fopen(path, "rb");
    size_t n;
    int consumed, ok;
    parser p = {0};
    p.state = START;
    p.store = store;
    p.filter = filter;
    store->count = 0;
    if (!f) { *error = filter ? "cannot open filter file" : "cannot open key file"; return 0; }
    n = fread(buffer, 1, sizeof(buffer), f);
    if (ferror(f)) { fclose(f); *error = filter ? "cannot read filter file" : "cannot read key file"; cli_clear_sensitive(buffer, sizeof(buffer)); return 0; }
    if (fclose(f) != 0) { *error = filter ? "cannot close filter file" : "cannot close key file"; cli_clear_sensitive(buffer, sizeof(buffer)); return 0; }
    if (n > KEY_FILE_LIMIT) { *error = filter ? "filter file exceeds size limit" : "key file exceeds size limit"; cli_clear_sensitive(buffer, sizeof(buffer)); return 0; }
    buffer[n] = '\0';
    consumed = mjson(buffer, (int)n, parse_event, &p);
    ok = consumed >= 0 && !p.failed && p.state == DONE;
    if (ok) {
        while ((size_t)consumed < n) {
            char c = buffer[consumed++];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') { ok = 0; break; }
        }
    }
    cli_clear_sensitive(buffer, sizeof(buffer));
    cli_clear_sensitive(&p.entry, sizeof(p.entry));
    if (!ok) {
        cli_clear_sensitive(store, sizeof(*store));
        *error = filter ? "invalid filter file or too many entries" :
                          "invalid key file or too many key entries";
    }
    return ok;
}

int cli_keys_load(cli_key_store *store, const char *path, const char **error)
{
    return load_store(store, path, error, 0);
}

int cli_filter_load(cli_key_store *store, const char *path, const char **error)
{
    size_t i;
    if (!load_store(store, path, error, 1)) return 0;
    for (i = 0; i < store->count; i++)
        cli_clear_sensitive(&store->entries[i].key, sizeof(store->entries[i].key));
    return 1;
}

static int same_identity(const uint8_t address[8], const mbus_identity *identity)
{
    return address[0] == (uint8_t)identity->manufacturer &&
           address[1] == (uint8_t)(identity->manufacturer >> 8) &&
           memcmp(address + 2, identity->identification, 4) == 0 &&
           address[6] == identity->version && address[7] == identity->medium;
}

static int application_identity_matches(const cli_key_entry *entry, const mbus_header_info *info)
{
    return entry->application && info->has_transport &&
           same_identity(entry->address, &info->transport) &&
           (!entry->has_link_constraint ||
            (info->has_link && same_identity(entry->link_address, &info->link)));
}

mbus_status cli_filter_match(const cli_key_store *store, const uint8_t *telegram,
                             size_t length, mbus_format format, int *matched,
                             const char **error)
{
    mbus_header_info info;
    mbus_status status;
    size_t i;
    *matched = store->count == 0;
    if (*matched) return MBUS_OK;
    status = mbus_get_header_info(telegram, length, format, &info, error);
    if (status != MBUS_OK) return status;
    for (i = 0; i < store->count; i++) {
        const cli_key_entry *entry = &store->entries[i];
        if (application_identity_matches(entry, &info) ||
            (!entry->application && info.has_link &&
             same_identity(entry->address, &info.link))) {
            *matched = 1;
            break;
        }
    }
    return MBUS_OK;
}

mbus_status cli_keys_match(const cli_key_store *store, const uint8_t *telegram,
                           size_t length, mbus_format format, mbus_key *keys,
                           size_t *count, const char **error)
{
    mbus_header_info info;
    cli_key_type type = CLI_KEY_UNTYPED;
    int application_matches = 0, pass, passes;
    size_t i;
    mbus_status status = mbus_get_header_info(telegram, length, format, &info, error);
    if (status != MBUS_OK) return status;
    {
        mbus_synchronization sync = mbus_header_sync(
            format, info.has_extended_link_header, info.extended_link_control,
            info.has_transport_header, info.configuration);
        if (sync == MBUS_SYNCHRONOUS) type = CLI_KEY_SYNCHRONOUS;
        else if (sync == MBUS_ASYNCHRONOUS) type = CLI_KEY_ASYNCHRONOUS;
    }
    for (i = 0; i < store->count; i++)
        if (application_identity_matches(&store->entries[i], &info)) application_matches = 1;
    passes = type == CLI_KEY_UNTYPED ? 1 : 2;
    for (pass = 0; pass < passes; pass++) {
        for (i = 0; i < store->count; i++) {
            const cli_key_entry *e = &store->entries[i];
            if (type != CLI_KEY_UNTYPED && e->type != (pass == 0 ? type : CLI_KEY_UNTYPED))
                continue;
            if ((application_matches && application_identity_matches(e, &info)) ||
                (!application_matches && info.has_link && !e->application && same_identity(e->address, &info.link))) {
                if (*count == MBUS_MAX_KEYS) {
                    if (error) *error = "too many matching keys";
                    return MBUS_LIMIT;
                }
                keys[(*count)++] = e->key;
            }
        }
    }
    return MBUS_OK;
}
