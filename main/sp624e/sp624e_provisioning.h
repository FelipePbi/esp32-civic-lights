#pragma once

#include "esp_err.h"
#include "sp624e_mapping.h"

esp_err_t sp624e_mapping_load(sp624e_mapping_t *mapping);
esp_err_t sp624e_mapping_save(const sp624e_mapping_t *mapping);
esp_err_t sp624e_mapping_clear(void);
esp_err_t sp624e_sync_test_done_load(bool *done);
esp_err_t sp624e_sync_test_done_save(bool done);
