#ifndef CLI_KEYS_H
#define CLI_KEYS_H

#include "mbus.h"

#define CLI_MAX_KEY_ENTRIES 128

typedef enum {
    CLI_KEY_UNTYPED,
    CLI_KEY_SYNCHRONOUS,
    CLI_KEY_ASYNCHRONOUS
} cli_key_type;

typedef struct {
    uint8_t address[8];
    uint8_t link_address[8];
    mbus_key key;
    int application;
    int has_link_constraint;
    cli_key_type type;
} cli_key_entry;

typedef struct {
    cli_key_entry entries[CLI_MAX_KEY_ENTRIES];
    size_t count;
} cli_key_store;

int cli_keys_load(cli_key_store *store, const char *path, const char **error);
int cli_filter_load(cli_key_store *store, const char *path, const char **error);
mbus_status cli_filter_match(const cli_key_store *store, const uint8_t *telegram,
                             size_t length, mbus_format format, int *matched,
                             const char **error);
mbus_status cli_keys_match(const cli_key_store *store, const uint8_t *telegram,
                           size_t length, mbus_format format, mbus_key *keys,
                           size_t *count, const char **error);
int cli_hex_decode(const char *text, size_t length, uint8_t *out, size_t capacity,
                   size_t *out_length);
void cli_clear_sensitive(void *buffer, size_t length);

#endif
