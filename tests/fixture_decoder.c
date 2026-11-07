#include "fixture_json_input.h"
#include "cli_json.h"
#include "cli_keys.h"

#include <stdio.h>

static char input[8192];
static uint8_t telegram[MBUS_MAX_TELEGRAM];
static mbus_key keys[MBUS_MAX_KEYS];
static mbus_result result;

int main(void)
{
    size_t input_length = fread(input, 1, sizeof(input), stdin);
    size_t length = 0, key_count = 0;
    mbus_format format = MBUS_WIRED;
    const char *error = NULL;
    cli_json_status parsed;
    mbus_status status;
    if (ferror(stdin)) {
        fprintf(stderr, "failed to read stdin\n");
        return 1;
    }
    if (input_length == sizeof(input)) {
        cli_json_error("input exceeds maximum length", NULL);
        return 1;
    }
    parsed = cli_json_parse(input, input_length, telegram, &length, &format, keys, &key_count);
    cli_clear_sensitive(input, sizeof(input));
    if (parsed != CLI_JSON_OK) {
        cli_clear_sensitive(keys, sizeof(keys));
        if (parsed == CLI_JSON_ODD_TELEGRAM)
            cli_json_error("telegram hexadecimal string must have an even number of digits", "invalid");
        else
            cli_json_error("invalid JSON request, format, telegram or keys", NULL);
        return 1;
    }
    status = mbus_decode_with_keys(telegram, length, format, keys, key_count, &result, &error);
    cli_clear_sensitive(keys, sizeof(keys));
    if (status == MBUS_OK) {
        error = cli_json_identity_error(&result);
        if (error != NULL) {
            cli_json_telegram_error(error, "invalid", telegram, length);
            return 1;
        }
    }
    if (status != MBUS_OK) {
        cli_json_error(error, status == MBUS_INVALID ? "invalid" :
                       status == MBUS_LIMIT ? "limit" : "unsupported");
        return 1;
    }
    cli_json_result(&result, NULL);
    return ferror(stdout) ? 1 : 0;
}
