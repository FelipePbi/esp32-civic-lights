#include <stdio.h>
#include <string.h>

#include "web/api_validation.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; \
} } while (0)

static bool parse_state(const char *json, web_state_request_t *request)
{
    return web_api_parse_state(json, strlen(json), request);
}

int main(void)
{
    web_state_request_t request;
    CHECK(!web_api_body_length_valid(0));
    CHECK(web_api_body_length_valid(WEB_API_BODY_MAX));
    CHECK(!web_api_body_length_valid(WEB_API_BODY_MAX + 1));
    CHECK(parse_state("{\"mode\":\"rgb\",\"r\":255,\"g\":0,\"b\":80,\"brightness\":64}", &request));
    CHECK(request.mode == WEB_STATE_RGB && request.red == 255 && request.blue == 80 &&
          request.brightness == 64);
    CHECK(parse_state("{\"mode\":\"white\",\"brightness\":128}", &request));
    CHECK(request.mode == WEB_STATE_WHITE && request.brightness == 128);
    CHECK(!parse_state("{\"mode\":\"rgb\",\"r\":256,\"g\":0,\"b\":0,\"brightness\":64}", &request));
    CHECK(!parse_state("{\"mode\":\"rgb\",\"r\":1.5,\"g\":0,\"b\":0,\"brightness\":64}", &request));
    CHECK(!parse_state("{\"mode\":\"rgb\",\"r\":1,\"g\":0,\"brightness\":64}", &request));
    CHECK(!parse_state("{\"mode\":\"other\",\"brightness\":64}", &request));
    CHECK(!parse_state("not json", &request));
    const char *favorite = "{\"mode\":\"rgb\",\"r\":9,\"g\":8,\"b\":7,\"brightness\":6}";
    CHECK(web_api_parse_favorite(favorite, strlen(favorite), &request));
    const char *white = "{\"mode\":\"white\",\"brightness\":64}";
    CHECK(!web_api_parse_favorite(white, strlen(white), &request));
    remote_button4_config_t button4;
    const char *remote_rgb =
        "{\"type\":\"rgb\",\"r\":128,\"g\":0,\"b\":255,\"brightness\":64}";
    CHECK(web_api_parse_remote_button4(remote_rgb, strlen(remote_rgb), &button4));
    CHECK(button4.type == REMOTE_ACTION_RGB && button4.red == 128 &&
          button4.blue == 255 && button4.brightness == 64);
    const char *remote_animation =
        "{\"type\":\"animation\",\"animation\":\"premium_pulse\"}";
    CHECK(!web_api_parse_remote_button4(remote_animation, strlen(remote_animation), &button4));
    CHECK(!web_api_parse_remote_button4("{\"type\":\"rgb\",\"r\":999}", 22, &button4));
    police_speed_t speed;
    const char *police_speed = "{\"speed\":\"very_fast\"}";
    CHECK(web_api_parse_police_speed(police_speed, strlen(police_speed), &speed));
    CHECK(speed == POLICE_SPEED_VERY_FAST);
    CHECK(!web_api_parse_police_speed("{\"speed\":\"turbo\"}", 17, &speed));
    remote_button_t mapping[RF_CHANNEL_COUNT];
    const char *mapping_json =
        "{\"button1\":\"d2\",\"button2\":\"d0\",\"button3\":\"d3\",\"button4\":\"d1\"}";
    CHECK(web_api_parse_remote_mapping(mapping_json, strlen(mapping_json), mapping));
    CHECK(mapping[RF_CHANNEL_D0] == REMOTE_BUTTON_2 &&
          mapping[RF_CHANNEL_D1] == REMOTE_BUTTON_4 &&
          mapping[RF_CHANNEL_D2] == REMOTE_BUTTON_1 &&
          mapping[RF_CHANNEL_D3] == REMOTE_BUTTON_3);
    const char *duplicate_mapping =
        "{\"button1\":\"d0\",\"button2\":\"d0\",\"button3\":\"d2\",\"button4\":\"d3\"}";
    CHECK(!web_api_parse_remote_mapping(duplicate_mapping, strlen(duplicate_mapping), mapping));
    if (failures) return 1;
    puts("Web API validation tests: PASS");
    return 0;
}
