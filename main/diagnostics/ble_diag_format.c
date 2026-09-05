#include "ble_diag_format.h"

#include <stdio.h>
#include <string.h>

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool is_separator(char c)
{
    return c == ' ' || c == '\t' || c == ':' || c == '-' || c == '_';
}

ble_diag_hex_result_t ble_diag_hex_decode(const char *text, uint8_t *out, size_t capacity,
                                          size_t *written)
{
    if (written != NULL) *written = 0;
    if (text == NULL || out == NULL) return BLE_DIAG_HEX_EMPTY;
    if ((text[0] == '0') && (text[1] == 'x' || text[1] == 'X')) text += 2;

    size_t count = 0;
    int high = -1;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (is_separator(*cursor)) {
            /* A separator may only fall on a byte boundary. */
            if (high >= 0) return BLE_DIAG_HEX_ODD_LENGTH;
            continue;
        }
        int value = hex_value(*cursor);
        if (value < 0) return BLE_DIAG_HEX_INVALID_CHAR;
        if (high < 0) {
            high = value;
            continue;
        }
        if (count >= capacity) return BLE_DIAG_HEX_TOO_LONG;
        out[count++] = (uint8_t)((high << 4) | value);
        high = -1;
    }
    if (high >= 0) return BLE_DIAG_HEX_ODD_LENGTH;
    if (count == 0) return BLE_DIAG_HEX_EMPTY;
    if (written != NULL) *written = count;
    return BLE_DIAG_HEX_OK;
}

const char *ble_diag_hex_result_name(ble_diag_hex_result_t result)
{
    switch (result) {
    case BLE_DIAG_HEX_OK: return "OK";
    case BLE_DIAG_HEX_EMPTY: return "EMPTY_PAYLOAD";
    case BLE_DIAG_HEX_INVALID_CHAR: return "INVALID_HEX_CHARACTER";
    case BLE_DIAG_HEX_ODD_LENGTH: return "ODD_NUMBER_OF_HEX_DIGITS";
    case BLE_DIAG_HEX_TOO_LONG: return "PAYLOAD_TOO_LONG";
    default: return "UNKNOWN";
    }
}

size_t ble_diag_hex_encode(const uint8_t *data, size_t length, char *out, size_t size)
{
    if (out == NULL || size == 0) return 0;
    out[0] = '\0';
    if (data == NULL) return 0;
    size_t used = 0;
    size_t rendered = 0;
    for (size_t i = 0; i < length; ++i) {
        /* Each byte needs "XX" plus a separator and the terminator. */
        if (used + 4 > size) break;
        used += (size_t)snprintf(out + used, size - used, "%s%02X", rendered > 0 ? " " : "",
                                 data[i]);
        rendered++;
    }
    return rendered;
}

size_t ble_diag_ascii_encode(const uint8_t *data, size_t length, char *out, size_t size)
{
    if (out == NULL || size == 0) return 0;
    out[0] = '\0';
    if (data == NULL) return 0;
    size_t rendered = 0;
    while (rendered < length && rendered + 1 < size) {
        uint8_t value = data[rendered];
        out[rendered] = (value >= 0x20 && value <= 0x7E) ? (char)value : '.';
        rendered++;
    }
    out[rendered] = '\0';
    return rendered;
}

void ble_diag_properties_string(uint8_t properties, char *out, size_t size)
{
    if (out == NULL || size == 0) return;
    static const struct {
        uint8_t flag;
        const char *name;
    } values[] = {
        {BLE_DIAG_PROP_BROADCAST, "BROADCAST"},
        {BLE_DIAG_PROP_READ, "READ"},
        {BLE_DIAG_PROP_WRITE_NO_RSP, "WRITE_NO_RESPONSE"},
        {BLE_DIAG_PROP_WRITE, "WRITE"},
        {BLE_DIAG_PROP_NOTIFY, "NOTIFY"},
        {BLE_DIAG_PROP_INDICATE, "INDICATE"},
        {BLE_DIAG_PROP_AUTH_SIGN_WRITE, "AUTH_SIGNED_WRITE"},
        {BLE_DIAG_PROP_EXTENDED, "EXTENDED"},
    };
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        if ((properties & values[i].flag) == 0 || used + 1 >= size) continue;
        used += (size_t)snprintf(out + used, size - used, "%s%s", used > 0 ? "|" : "",
                                 values[i].name);
    }
    if (used == 0) snprintf(out, size, "NONE");
}

bool ble_diag_properties_are_interesting(uint8_t properties)
{
    return (properties & (BLE_DIAG_PROP_WRITE | BLE_DIAG_PROP_WRITE_NO_RSP |
                          BLE_DIAG_PROP_NOTIFY | BLE_DIAG_PROP_INDICATE)) != 0;
}

int ble_diag_address_parse(const char *text, uint8_t out[6])
{
    if (text == NULL || out == NULL) return -1;
    uint8_t bytes[6];
    size_t written = 0;
    if (ble_diag_hex_decode(text, bytes, sizeof(bytes), &written) != BLE_DIAG_HEX_OK) {
        return -1;
    }
    if (written != 6) return -1;
    /* NimBLE stores addresses least-significant byte first. */
    for (size_t i = 0; i < 6; ++i) out[i] = bytes[5 - i];
    return 0;
}

void ble_diag_address_format(const uint8_t address[6], char *out, size_t size)
{
    if (out == NULL || size == 0) return;
    if (address == NULL) {
        snprintf(out, size, "<none>");
        return;
    }
    snprintf(out, size, "%02X:%02X:%02X:%02X:%02X:%02X", address[5], address[4], address[3],
             address[2], address[1], address[0]);
}
