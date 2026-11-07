#include "fixture_json_input.h"
#include "mjson.h"

#include <string.h>

enum state {
    ROOT_START, ROOT_FIRST, ROOT_KEY, ROOT_COLON, ROOT_VALUE, ROOT_AFTER,
    ARRAY_FIRST, ARRAY_VALUE, ARRAY_AFTER, ROOT_DONE
};
enum field { FIELD_TELEGRAM = 1, FIELD_FORMAT = 2, FIELD_KEYS = 4 };

typedef struct {
    enum state state;
    unsigned seen, field;
    int failed;
    int odd_telegram;
    uint8_t *telegram;
    size_t *telegram_length;
    mbus_format *format;
    mbus_key *keys;
    size_t *key_count;
} request_parser;

static void clear_sensitive(void *buffer, size_t length)
{
    volatile unsigned char *p = buffer;
    while (length--)
        *p++ = 0;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode_string(const char *s, int length, char *out, size_t capacity,
                         size_t *decoded_length)
{
    int i;
    size_t n = 0;
    if (length < 2 || s[0] != '"' || s[length - 1] != '"')
        return 0;
    for (i = 1; i < length - 1; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c >= 0x80)
            return 0;
        if (c == '\\') {
            if (++i >= length - 1)
                return 0;
            c = (unsigned char)s[i];
            if (c == 'u') {
                unsigned u = 0;
                int j, digit;
                if (i + 4 >= length - 1)
                    return 0;
                for (j = 0; j < 4; j++) {
                    digit = hex_digit(s[++i]);
                    if (digit < 0)
                        return 0;
                    u = (u << 4) | (unsigned)digit;
                }
                if (u == 0 || u >= 0x80)
                    return 0;
                c = (unsigned char)u;
            } else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c != '"' && c != '\\' && c != '/') return 0;
        }
        if (n + 1 < capacity)
            out[n] = (char)c;
        n++;
    }
    if (decoded_length != NULL) *decoded_length = n;
    if (n >= capacity) return 0;
    out[n] = '\0';
    return 1;
}

static int decode_hex(const char *s, uint8_t *bytes, size_t capacity, size_t *length)
{
    size_t len = strlen(s), i;
    if ((len & 1) || len / 2 > capacity)
        return 0;
    for (i = 0; i < len / 2; i++) {
        int hi = hex_digit(s[2 * i]), lo = hex_digit(s[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return 0;
        bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    *length = len / 2;
    return 1;
}

static int request_event(int event, const char *buf, int offset, int len, void *userdata)
{
    request_parser *p = userdata;
    char value[MBUS_MAX_TELEGRAM * 2 + 1];
    if (p->state == ROOT_START && event == '{') {
        p->state = ROOT_FIRST;
    } else if ((p->state == ROOT_FIRST || p->state == ROOT_KEY) && event == MJSON_TOK_KEY) {
        if (!decode_string(buf + offset, len, value, sizeof(value), NULL))
            goto invalid;
        if (strcmp(value, "telegram") == 0) p->field = FIELD_TELEGRAM;
        else if (strcmp(value, "format") == 0) p->field = FIELD_FORMAT;
        else if (strcmp(value, "keys") == 0) p->field = FIELD_KEYS;
        else goto invalid;
        if (p->seen & p->field) goto invalid;
        p->seen |= p->field;
        p->state = ROOT_COLON;
    } else if (p->state == ROOT_COLON && event == ':') {
        p->state = ROOT_VALUE;
    } else if (p->state == ROOT_VALUE && event == '[' && p->field == FIELD_KEYS) {
        p->state = ARRAY_FIRST;
    } else if (p->state == ROOT_VALUE && event == MJSON_TOK_STRING && p->field != FIELD_KEYS) {
        size_t decoded_length = 0;
        int decoded = decode_string(buf + offset, len, value, sizeof(value), &decoded_length);
        if (p->field == FIELD_TELEGRAM && (decoded_length & 1)) {
            p->odd_telegram = 1;
            goto invalid;
        }
        if (!decoded) goto invalid;
        if (p->field == FIELD_TELEGRAM) {
            if (!decode_hex(value, p->telegram, MBUS_MAX_TELEGRAM, p->telegram_length))
                goto invalid;
        } else if (strcmp(value, "wired") == 0) {
            *p->format = MBUS_WIRED;
        } else if (strcmp(value, "wireless") == 0) {
            *p->format = MBUS_WIRELESS;
        } else goto invalid;
        p->state = ROOT_AFTER;
    } else if ((p->state == ARRAY_FIRST || p->state == ARRAY_VALUE) && event == MJSON_TOK_STRING) {
        size_t key_length;
        if (*p->key_count == MBUS_MAX_KEYS ||
            !decode_string(buf + offset, len, value, sizeof(value), NULL) ||
            !decode_hex(value, p->keys[*p->key_count].bytes, sizeof(p->keys[0].bytes), &key_length) ||
            key_length != sizeof(p->keys[0].bytes))
            goto invalid;
        ++*p->key_count;
        p->state = ARRAY_AFTER;
    } else if ((p->state == ARRAY_FIRST || p->state == ARRAY_AFTER) && event == ']') {
        p->state = ROOT_AFTER;
    } else if (p->state == ARRAY_AFTER && event == ',') {
        p->state = ARRAY_VALUE;
    } else if (p->state == ROOT_AFTER && event == ',') {
        p->state = ROOT_KEY;
    } else if ((p->state == ROOT_FIRST || p->state == ROOT_AFTER) && event == '}') {
        p->state = ROOT_DONE;
    } else {
        goto invalid;
    }
    clear_sensitive(value, sizeof(value));
    return 0;
invalid:
    clear_sensitive(value, sizeof(value));
    p->failed = 1;
    return 1;
}

cli_json_status cli_json_parse(const char *input, size_t length, uint8_t *telegram,
                               size_t *telegram_length, mbus_format *format,
                               mbus_key *keys, size_t *key_count)
{
    request_parser p = {ROOT_START, 0, 0, 0, 0, telegram, telegram_length, format, keys, key_count};
    int consumed = mjson(input, (int)length, request_event, &p);
    if (consumed < 0 || p.failed || p.state != ROOT_DONE ||
        (p.seen & (FIELD_TELEGRAM | FIELD_FORMAT)) != (FIELD_TELEGRAM | FIELD_FORMAT))
        return p.odd_telegram ? CLI_JSON_ODD_TELEGRAM : CLI_JSON_INVALID;
    while ((size_t)consumed < length) {
        char c = input[consumed++];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
            return CLI_JSON_INVALID;
    }
    return CLI_JSON_OK;
}
