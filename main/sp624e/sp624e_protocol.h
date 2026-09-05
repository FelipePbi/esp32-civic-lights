#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SP624E_COMMAND_MAX_LEN 6
#define SP624E_MESSAGE_MAX_LEN 64

#define SP624E_EFFECT_SOLID 0x63
#define SP624E_EFFECT_WHITE 0xcc

typedef enum {
    SP624E_PROTOCOL_OK = 0,
    SP624E_PROTOCOL_INCOMPLETE = 1,
    SP624E_PROTOCOL_INVALID_ARGUMENT = -1,
    SP624E_PROTOCOL_BUFFER_TOO_SMALL = -2,
    SP624E_PROTOCOL_INVALID_VALUE = -3,
    SP624E_PROTOCOL_INVALID_PACKET = -4,
    SP624E_PROTOCOL_OUT_OF_SEQUENCE = -5,
    SP624E_PROTOCOL_OVERSIZED = -6,
} sp624e_protocol_result_t;

typedef struct {
    bool active;
    uint8_t expected_packet;
    uint8_t total_length;
    uint8_t received_length;
    uint8_t message[SP624E_MESSAGE_MAX_LEN];
} sp624e_reassembly_t;

void sp624e_reassembly_reset(sp624e_reassembly_t *reassembly);
sp624e_protocol_result_t sp624e_reassembly_push(sp624e_reassembly_t *reassembly,
                                                const uint8_t *packet,
                                                size_t packet_len,
                                                const uint8_t **message,
                                                size_t *message_len);

sp624e_protocol_result_t sp624e_build_state_query(uint8_t *output, size_t capacity,
                                                  size_t *output_len);
sp624e_protocol_result_t sp624e_build_power(int power, uint8_t *output, size_t capacity,
                                            size_t *output_len);
sp624e_protocol_result_t sp624e_build_brightness(unsigned level, uint8_t *output,
                                                 size_t capacity, size_t *output_len);
sp624e_protocol_result_t sp624e_build_rgb(unsigned red, unsigned green, unsigned blue,
                                          unsigned level, uint8_t *output, size_t capacity,
                                          size_t *output_len);
sp624e_protocol_result_t sp624e_build_effect(unsigned effect, uint8_t *output,
                                             size_t capacity, size_t *output_len);
sp624e_protocol_result_t sp624e_build_white(unsigned level, uint8_t *output,
                                            size_t capacity, size_t *output_len);
sp624e_protocol_result_t sp624e_build_speed(unsigned speed, uint8_t *output,
                                            size_t capacity, size_t *output_len);
sp624e_protocol_result_t sp624e_build_mode(unsigned mode, uint8_t *output,
                                           size_t capacity, size_t *output_len);
