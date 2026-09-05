#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SP624E_MAPPING_VERSION 1u
#define SP624E_MAPPING_ENCODED_LEN 22u

typedef struct {
    uint8_t type;
    uint8_t val[6];
} sp624e_address_t;

typedef struct {
    bool valid;
    sp624e_address_t left;
    sp624e_address_t right;
    uint32_t version;
} sp624e_mapping_t;

bool sp624e_address_equal(const sp624e_address_t *first, const sp624e_address_t *second);
int sp624e_mapping_encode(const sp624e_mapping_t *mapping, uint8_t *output,
                          size_t capacity, size_t *output_len);
int sp624e_mapping_decode(const uint8_t *data, size_t data_len, sp624e_mapping_t *mapping);
