#include "cli_json.h"
#include "cli_keys.h"

#include <stdio.h>
#include <string.h>

#define INPUT_SIZE 8192

static char input[INPUT_SIZE];
static uint8_t telegram[MBUS_MAX_TELEGRAM];
static mbus_context ctx;
static mbus_result result;
static mbus_key keys[MBUS_MAX_KEYS];
static size_t key_count;
static cli_key_store key_store;
static cli_key_store filter_store;
static int have_key_store;
static int have_filter;

typedef enum {
    RTL_INVALID,
    RTL_DECODED,
    RTL_FILTERED,
    RTL_DECODE_FAILED,
    RTL_BAD_CRC,
    RTL_ODD_TELEGRAM
} rtl_outcome;

static int parse_rssi(const char *text, uint32_t *value)
{
    uint32_t n = 0;
    const unsigned char *p = (const unsigned char *)text;
    if (!*p) return 0;
    for (; *p; p++) {
        unsigned digit;
        if (*p < '0' || *p > '9') return 0;
        digit = *p - '0';
        if (n > (UINT32_MAX - digit) / 10) return 0;
        n = n * 10 + digit;
    }
    *value = n;
    return 1;
}

static rtl_outcome decode(mbus_format format, size_t length, const cli_receiver_metadata *receiver,
                          const char *datagram)
{
    const char *error = NULL;
    mbus_status status = MBUS_OK;
    int matched = 1;
    if (have_filter)
        status = cli_filter_match(&filter_store, telegram, length, format, &matched, &error);
    if (status == MBUS_OK && !matched)
        return RTL_FILTERED;
    if (status == MBUS_OK && have_key_store)
        status = cli_keys_match(&key_store, telegram, length, format, keys, &key_count, &error);
    if (status == MBUS_OK)
        status = mbus_decode_with_keys(&ctx, telegram, length, format, keys, key_count, &result, &error);
    if (status == MBUS_OK) {
        error = cli_json_identity_error(&result);
        if (error != NULL)
            status = MBUS_INVALID;
    }
    cli_clear_sensitive(keys, sizeof(keys));
    key_count = 0;
    if (status != MBUS_OK) {
        cli_json_stream_error(status == MBUS_INVALID ? CLI_STATUS_INVALID :
                              status == MBUS_LIMIT ? CLI_STATUS_LIMIT : CLI_STATUS_UNSUPPORTED,
                              error, receiver, datagram);
        return RTL_DECODE_FAILED;
    }
    cli_json_result(&result, receiver);
    return RTL_DECODED;
}

static rtl_outcome rtl_line(char *line, size_t length,
                            cli_receiver_metadata *receiver, int *has_receiver,
                            const char **datagram)
{
    char *fields[9];
    size_t count = 1, i, start = 0, telegram_length;
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) length--;
    line[length] = '\0';
    fields[0] = line;
    for (i = 0; i < length; i++) {
        if (line[i] == '\0') return RTL_INVALID;
        if (line[i] == ';') {
            if (count == sizeof(fields) / sizeof(fields[0])) return RTL_INVALID;
            line[i] = '\0';
            fields[count++] = line + i + 1;
        }
    }
    if (count == 8 || count == 9) *datagram = fields[count - 1];
    if (count == 9 && (!strcmp(fields[0], "rla") || !strcmp(fields[0], "t2a"))) start = 1;
    else if (count != 8) return RTL_INVALID;
    *has_receiver = 1;
    receiver->timestamp = fields[start + 3][0] ? fields[start + 3] : NULL;
    receiver->has_rssi = 0;
    if (strcmp(fields[start], "T1") && strcmp(fields[start], "C1") && strcmp(fields[start], "S1"))
        return RTL_INVALID;
    receiver->has_rssi = parse_rssi(fields[start + 4], &receiver->packet_rssi) &&
                         parse_rssi(fields[start + 5], &receiver->current_rssi);
    if (!strcmp(fields[start + 1], "0")) return RTL_BAD_CRC;
    if (strcmp(fields[start + 1], "1") || strcmp(fields[start + 2], "1") ||
        !fields[start + 3][0] || !fields[start + 6][0] || !receiver->has_rssi)
        return RTL_INVALID;
    if (fields[start + 7][0] != '0' ||
        (fields[start + 7][1] != 'x' && fields[start + 7][1] != 'X'))
        return RTL_INVALID;
    if (strlen(fields[start + 7] + 2) & 1) return RTL_ODD_TELEGRAM;
    if (!cli_hex_decode(fields[start + 7] + 2, strlen(fields[start + 7] + 2), telegram,
                        sizeof(telegram), &telegram_length))
        return RTL_INVALID;
    return decode(MBUS_WIRELESS, telegram_length, receiver, *datagram);
}

static int process_line(size_t n, int overflow)
{
    cli_receiver_metadata receiver;
    int has_receiver = 0;
    const char *datagram = NULL;
    rtl_outcome outcome = overflow ? RTL_INVALID : rtl_line(input, n, &receiver, &has_receiver, &datagram);
    const cli_receiver_metadata *metadata = has_receiver ? &receiver : NULL;
    if (outcome == RTL_INVALID)
        cli_json_stream_error(CLI_STATUS_INVALID, "invalid rtl-wmbus line or failed receiver checks", metadata, datagram);
    else if (outcome == RTL_ODD_TELEGRAM)
        cli_json_stream_error(CLI_STATUS_INVALID, "telegram hexadecimal string must have an even number of digits", metadata, datagram);
    else if (outcome == RTL_BAD_CRC)
        cli_json_stream_error(CLI_STATUS_CRC_FAILED, "receiver reported CRC failure", metadata, datagram);
    cli_clear_sensitive(input, sizeof(input));
    return outcome != RTL_DECODED && outcome != RTL_FILTERED;
}

static int rtl_stream(void)
{
    size_t n = 0;
    int c, overflow = 0, failed = 0;
    while ((c = getchar()) != EOF) {
        if (c == '\n') {
            if (process_line(n, overflow)) failed = 1;
            if (fflush(stdout) != 0) { fprintf(stderr, "failed to write stdout\n"); return 1; }
            n = 0;
            overflow = 0;
        } else if (n < sizeof(input) - 1) {
            input[n++] = (char)c;
        } else {
            overflow = 1;
        }
    }
    if (ferror(stdin)) { fprintf(stderr, "failed to read stdin\n"); return 1; }
    if (n || overflow) {
        if (process_line(n, overflow)) failed = 1;
    }
    if (fflush(stdout) != 0) { fprintf(stderr, "failed to write stdout\n"); return 1; }
    return failed;
}

int main(int argc, char **argv)
{
    const char *error = NULL, *key_path = NULL, *filter_path = NULL;
    int rtl_option_seen = 0, i, result_code;
    mbus_context_init(&ctx);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rtl-wmbus") && !rtl_option_seen) rtl_option_seen = 1;
        else if (!strcmp(argv[i], "--keys") && !key_path && i + 1 < argc) key_path = argv[++i];
        else if (!strcmp(argv[i], "--filter") && !filter_path && i + 1 < argc) filter_path = argv[++i];
        else {
            fprintf(stderr, "usage: mbusdec [--rtl-wmbus] [--keys FILE] [--filter FILE]\n");
            return 1;
        }
    }
    if (key_path) {
        if (!cli_keys_load(&key_store, key_path, &error)) {
            fprintf(stderr, "%s: %s\n", key_path, error);
            return 1;
        }
        have_key_store = 1;
    }
    if (filter_path) {
        if (!cli_filter_load(&filter_store, filter_path, &error)) {
            fprintf(stderr, "%s: %s\n", filter_path, error);
            cli_clear_sensitive(&key_store, sizeof(key_store));
            return 1;
        }
        have_filter = 1;
    }
    result_code = rtl_stream();
    cli_clear_sensitive(&key_store, sizeof(key_store));
    cli_clear_sensitive(&filter_store, sizeof(filter_store));
    return result_code;
}
