#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "runtime_animation.h"

void animation_manager_init(runtime_animation_event_fn event_fn);
void animation_manager_cancel_for_user(void);
bool animation_manager_cancel_for_user_and_wait(uint32_t timeout_ms);
void animation_manager_on_disconnect(void);
bool animation_manager_any_active(void);
