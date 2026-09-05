#pragma once

char *web_json_status(void);
char *web_json_state(void);
char *web_json_presets(void);
char *web_json_snapshot_event(const char *type);
char *web_json_accepted(unsigned generation);
char *web_json_error(const char *code, const char *message);
char *web_json_remote(void);
char *web_json_simple_accepted(void);
