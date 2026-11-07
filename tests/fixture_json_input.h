#ifndef FIXTURE_JSON_INPUT_H
#define FIXTURE_JSON_INPUT_H

#include "mbus.h"

typedef enum {
    CLI_JSON_INVALID,
    CLI_JSON_OK,
    CLI_JSON_ODD_TELEGRAM
} cli_json_status;

cli_json_status cli_json_parse(const char *input, size_t length, uint8_t *telegram,
                               size_t *telegram_length, mbus_format *format,
                               mbus_key *keys, size_t *key_count);

#endif
