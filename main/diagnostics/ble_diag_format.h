#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Pure formatting/parsing helpers for the BLE diagnostic console. Kept free of
 * NimBLE and ESP-IDF dependencies so the host test suite can cover them.
 */

/* Bluetooth Core characteristic property bits; asserted against NimBLE. */
#define BLE_DIAG_PROP_BROADCAST 0x01
#define BLE_DIAG_PROP_READ 0x02
#define BLE_DIAG_PROP_WRITE_NO_RSP 0x04
#define BLE_DIAG_PROP_WRITE 0x08
#define BLE_DIAG_PROP_NOTIFY 0x10
#define BLE_DIAG_PROP_INDICATE 0x20
#define BLE_DIAG_PROP_AUTH_SIGN_WRITE 0x40
#define BLE_DIAG_PROP_EXTENDED 0x80

typedef enum {
    BLE_DIAG_HEX_OK = 0,
    BLE_DIAG_HEX_EMPTY,
    BLE_DIAG_HEX_INVALID_CHAR,
    BLE_DIAG_HEX_ODD_LENGTH,
    BLE_DIAG_HEX_TOO_LONG,
} ble_diag_hex_result_t;

/*
 * Decodes a hex payload. Accepts an optional "0x" prefix and ' ', ':', '-' or
 * '_' separators between bytes. Never writes past capacity.
 */
ble_diag_hex_result_t ble_diag_hex_decode(const char *text, uint8_t *out, size_t capacity,
                                          size_t *written);

const char *ble_diag_hex_result_name(ble_diag_hex_result_t result);

/* Writes "AA BB CC" and always NUL-terminates. Returns bytes rendered. */
size_t ble_diag_hex_encode(const uint8_t *data, size_t length, char *out, size_t size);

/* Writes printable characters, '.' otherwise. Returns bytes rendered. */
size_t ble_diag_ascii_encode(const uint8_t *data, size_t length, char *out, size_t size);

/* Renders GATT characteristic property bits as "READ|WRITE|NOTIFY". */
void ble_diag_properties_string(uint8_t properties, char *out, size_t size);

/* True when the properties suggest a channel worth reverse engineering. */
bool ble_diag_properties_are_interesting(uint8_t properties);

/*
 * Parses "AA:BB:CC:DD:EE:FF" (also accepts '-' or no separator) into the
 * little-endian byte order NimBLE uses for ble_addr_t. Returns 0 on success.
 */
int ble_diag_address_parse(const char *text, uint8_t out[6]);

/* Renders a NimBLE little-endian address as "AA:BB:CC:DD:EE:FF". */
void ble_diag_address_format(const uint8_t address[6], char *out, size_t size);
