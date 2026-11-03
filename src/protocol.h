#ifndef NUPHY_PROTOCOL_H
#define NUPHY_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { REPORT_SIZE = 64, GET_BASE = 4, GET_FUNC = 5, SET_FUNC = 6,
       MODE_SIZE = 64, EFFECT_OFFSET = 8, BRIGHTNESS_OFFSET = 9,
       MAX_BRIGHTNESS = 100, EFFECT_OFF = 255, MAX_EFFECT = 22 };

static inline uint8_t checksum(const uint8_t report[REPORT_SIZE])
{
    unsigned sum = 0;
    for (size_t i = 4; i < REPORT_SIZE; ++i) sum += report[i];
    return (uint8_t)sum;
}

static inline void make_report(uint8_t report[REPORT_SIZE], uint8_t command,
                               uint16_t address, uint8_t length, const uint8_t *data)
{
    for (size_t i = 0; i < REPORT_SIZE; ++i) report[i] = 0;
    report[0] = 0x55;
    report[1] = command;
    report[4] = length;
    report[5] = (uint8_t)address;
    report[6] = (uint8_t)(address >> 8);
    if (data && length <= REPORT_SIZE - 8)
        for (size_t i = 0; i < length; ++i) report[8 + i] = data[i];
    report[3] = checksum(report);
}

static inline bool valid_reply(const uint8_t *reply, size_t size,
                                const uint8_t request[REPORT_SIZE])
{
    return size == REPORT_SIZE && reply[0] == 0xaa && reply[1] == request[1]
        && reply[2] == 0 && reply[3] == checksum(reply)
        && reply[4] == request[4] && reply[5] == request[5]
        && reply[6] == request[6] && reply[7] == 0;
}

static inline bool brightness_address(unsigned mode, uint16_t *address)
{
    if (mode > 2) return false;
    *address = (uint16_t)(mode * MODE_SIZE + BRIGHTNESS_OFFSET);
    return true;
}
#endif
