#include "sp624e_protocol.h"

#include <string.h>

static sp624e_protocol_result_t build(const uint8_t *command, size_t command_len,
                                      uint8_t *output, size_t capacity, size_t *output_len)
{
    if (command == NULL || output == NULL || output_len == NULL) {
        return SP624E_PROTOCOL_INVALID_ARGUMENT;
    }
    if (capacity < command_len) {
        return SP624E_PROTOCOL_BUFFER_TOO_SMALL;
    }
    memcpy(output, command, command_len);
    *output_len = command_len;
    return SP624E_PROTOCOL_OK;
}

void sp624e_reassembly_reset(sp624e_reassembly_t *reassembly)
{
    if (reassembly != NULL) {
        memset(reassembly, 0, sizeof(*reassembly));
    }
}

static sp624e_protocol_result_t fail_reassembly(sp624e_reassembly_t *reassembly,
                                                sp624e_protocol_result_t result)
{
    sp624e_reassembly_reset(reassembly);
    return result;
}

sp624e_protocol_result_t sp624e_reassembly_push(sp624e_reassembly_t *reassembly,
                                                const uint8_t *packet,
                                                size_t packet_len,
                                                const uint8_t **message,
                                                size_t *message_len)
{
    if (reassembly == NULL || packet == NULL || message == NULL || message_len == NULL) {
        return SP624E_PROTOCOL_INVALID_ARGUMENT;
    }
    *message = NULL;
    *message_len = 0;
    if (packet_len < 2) {
        return fail_reassembly(reassembly, SP624E_PROTOCOL_INVALID_PACKET);
    }

    uint8_t packet_number = packet[0];
    if (packet_number == 1) {
        if (packet_len < 3) {
            return fail_reassembly(reassembly, SP624E_PROTOCOL_INVALID_PACKET);
        }
        uint8_t total_length = packet[1];
        uint8_t payload_length = packet[2];
        if (total_length == 0 || total_length > SP624E_MESSAGE_MAX_LEN) {
            return fail_reassembly(reassembly, SP624E_PROTOCOL_OVERSIZED);
        }
        if (packet_len != (size_t)payload_length + 3 || payload_length > total_length) {
            return fail_reassembly(reassembly, SP624E_PROTOCOL_INVALID_PACKET);
        }
        sp624e_reassembly_reset(reassembly);
        memcpy(reassembly->message, packet + 3, payload_length);
        reassembly->total_length = total_length;
        reassembly->received_length = payload_length;
        if (payload_length == total_length) {
            *message = reassembly->message;
            *message_len = total_length;
            return SP624E_PROTOCOL_OK;
        }
        reassembly->active = true;
        reassembly->expected_packet = 2;
        return SP624E_PROTOCOL_INCOMPLETE;
    }

    if (!reassembly->active || packet_number != reassembly->expected_packet) {
        return fail_reassembly(reassembly, SP624E_PROTOCOL_OUT_OF_SEQUENCE);
    }
    uint8_t payload_length = packet[1];
    if (packet_len != (size_t)payload_length + 2) {
        return fail_reassembly(reassembly, SP624E_PROTOCOL_INVALID_PACKET);
    }
    size_t new_length = (size_t)reassembly->received_length + payload_length;
    if (new_length > reassembly->total_length || new_length > sizeof(reassembly->message)) {
        return fail_reassembly(reassembly, SP624E_PROTOCOL_OVERSIZED);
    }
    memcpy(reassembly->message + reassembly->received_length, packet + 2, payload_length);
    reassembly->received_length = (uint8_t)new_length;
    reassembly->expected_packet++;
    if (reassembly->received_length != reassembly->total_length) {
        return SP624E_PROTOCOL_INCOMPLETE;
    }
    reassembly->active = false;
    *message = reassembly->message;
    *message_len = reassembly->total_length;
    return SP624E_PROTOCOL_OK;
}

sp624e_protocol_result_t sp624e_build_state_query(uint8_t *output, size_t capacity,
                                                  size_t *output_len)
{
    const uint8_t command[] = {0x1d, 0x00};
    return build(command, sizeof(command), output, capacity, output_len);
}

sp624e_protocol_result_t sp624e_build_power(int power, uint8_t *output, size_t capacity,
                                            size_t *output_len)
{
    if (power != 0 && power != 1) {
        return SP624E_PROTOCOL_INVALID_VALUE;
    }
    const uint8_t command[] = {0x0f, 0x01, (uint8_t)power};
    return build(command, sizeof(command), output, capacity, output_len);
}

sp624e_protocol_result_t sp624e_build_brightness(unsigned level, uint8_t *output,
                                                 size_t capacity, size_t *output_len)
{
    if (level > UINT8_MAX) {
        return SP624E_PROTOCOL_INVALID_VALUE;
    }
    const uint8_t command[] = {0x12, 0x01, (uint8_t)level};
    return build(command, sizeof(command), output, capacity, output_len);
}

sp624e_protocol_result_t sp624e_build_rgb(unsigned red, unsigned green, unsigned blue,
                                          unsigned level, uint8_t *output, size_t capacity,
                                          size_t *output_len)
{
    if (red > UINT8_MAX || green > UINT8_MAX || blue > UINT8_MAX || level > UINT8_MAX) {
        return SP624E_PROTOCOL_INVALID_VALUE;
    }
    const uint8_t command[] = {
        0x13, 0x04, (uint8_t)red, (uint8_t)green, (uint8_t)blue, (uint8_t)level
    };
    return build(command, sizeof(command), output, capacity, output_len);
}

sp624e_protocol_result_t sp624e_build_effect(unsigned effect, uint8_t *output,
                                             size_t capacity, size_t *output_len)
{
    if (effect > UINT8_MAX) {
        return SP624E_PROTOCOL_INVALID_VALUE;
    }
    const uint8_t command[] = {0x15, 0x01, (uint8_t)effect};
    return build(command, sizeof(command), output, capacity, output_len);
}

sp624e_protocol_result_t sp624e_build_white(unsigned level, uint8_t *output,
                                            size_t capacity, size_t *output_len)
{
    if (level > UINT8_MAX) {
        return SP624E_PROTOCOL_INVALID_VALUE;
    }
    const uint8_t command[] = {0x21, 0x02, (uint8_t)level, 0xff};
    return build(command, sizeof(command), output, capacity, output_len);
}

sp624e_protocol_result_t sp624e_build_speed(unsigned speed, uint8_t *output,
                                            size_t capacity, size_t *output_len)
{
    if (speed < 1 || speed > 10) {
        return SP624E_PROTOCOL_INVALID_VALUE;
    }
    const uint8_t command[] = {0x14, 0x01, (uint8_t)speed};
    return build(command, sizeof(command), output, capacity, output_len);
}

sp624e_protocol_result_t sp624e_build_mode(unsigned mode, uint8_t *output,
                                           size_t capacity, size_t *output_len)
{
    if (mode > 2) {
        return SP624E_PROTOCOL_INVALID_VALUE;
    }
    const uint8_t command[] = {0x16, 0x01, (uint8_t)mode};
    return build(command, sizeof(command), output, capacity, output_len);
}
