#include "sp624e_mapping.h"

#include <string.h>

static const uint8_t s_magic[4] = {'S', 'P', 'M', '1'};

bool sp624e_address_equal(const sp624e_address_t *first, const sp624e_address_t *second)
{
    return first != NULL && second != NULL && first->type == second->type &&
           memcmp(first->val, second->val, sizeof(first->val)) == 0;
}

static bool mapping_valid(const sp624e_mapping_t *mapping)
{
    return mapping != NULL && mapping->valid && mapping->version == SP624E_MAPPING_VERSION &&
           mapping->left.type <= 3 && mapping->right.type <= 3 &&
           !sp624e_address_equal(&mapping->left, &mapping->right);
}

int sp624e_mapping_encode(const sp624e_mapping_t *mapping, uint8_t *output,
                          size_t capacity, size_t *output_len)
{
    if (mapping == NULL || output == NULL || output_len == NULL) return -1;
    if (!mapping_valid(mapping)) return -2;
    if (capacity < SP624E_MAPPING_ENCODED_LEN) return -3;

    memcpy(output, s_magic, sizeof(s_magic));
    output[4] = (uint8_t)mapping->version;
    output[5] = (uint8_t)(mapping->version >> 8);
    output[6] = (uint8_t)(mapping->version >> 16);
    output[7] = (uint8_t)(mapping->version >> 24);
    output[8] = mapping->left.type;
    memcpy(output + 9, mapping->left.val, sizeof(mapping->left.val));
    output[15] = mapping->right.type;
    memcpy(output + 16, mapping->right.val, sizeof(mapping->right.val));
    *output_len = SP624E_MAPPING_ENCODED_LEN;
    return 0;
}

int sp624e_mapping_decode(const uint8_t *data, size_t data_len, sp624e_mapping_t *mapping)
{
    if (data == NULL || mapping == NULL) return -1;
    memset(mapping, 0, sizeof(*mapping));
    if (data_len != SP624E_MAPPING_ENCODED_LEN ||
        memcmp(data, s_magic, sizeof(s_magic)) != 0) return -2;

    mapping->version = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                       ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    mapping->left.type = data[8];
    memcpy(mapping->left.val, data + 9, sizeof(mapping->left.val));
    mapping->right.type = data[15];
    memcpy(mapping->right.val, data + 16, sizeof(mapping->right.val));
    mapping->valid = true;
    if (!mapping_valid(mapping)) {
        memset(mapping, 0, sizeof(*mapping));
        return -3;
    }
    return 0;
}
