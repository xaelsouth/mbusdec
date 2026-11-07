#ifndef CLI_JSON_H
#define CLI_JSON_H

#include "mbus.h"

typedef struct {
    uint32_t packet_rssi;
    uint32_t current_rssi;
} cli_receiver_metadata;

typedef enum {
    CLI_STATUS_OK = 0,
    CLI_STATUS_INVALID = 1,
    CLI_STATUS_UNSUPPORTED = 2,
    CLI_STATUS_LIMIT = 3,
    CLI_STATUS_CRC_FAILED = 4
} cli_status;

void cli_json_result(const mbus_result *result, const cli_receiver_metadata *receiver);
const char *cli_json_identity_error(const mbus_result *result);
void cli_json_error(const char *message, const char *code);
void cli_json_telegram_error(const char *message, const char *code,
                             const uint8_t *telegram, size_t length);
void cli_json_stream_error(cli_status status, const char *message,
                           const cli_receiver_metadata *receiver,
                           const char *datagram);

#endif
