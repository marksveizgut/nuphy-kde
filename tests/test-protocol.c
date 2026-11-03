#include "../src/protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* Independently obtained from NuPhyIO's createCommand generator and
     * serializePartialLightData({brightness:37}, "main"). */
    const uint8_t vendor_get_base[64] = {0x55, 4, 0, 16, 16};
    const uint8_t vendor_get_func[64] = {0x55, 5, 0, 118, 54, 64};
    const uint8_t vendor_set_37[64] = {0x55, 6, 0, 111, 1, 73, 0, 0, 37};
    const uint8_t vendor_off[64] = {0x55, 6, 0, 73, 2, 72, 0, 0, 255, 0};
    uint8_t report[64], reply[64];
    make_report(report, GET_BASE, 0, 16, NULL);
    assert(memcmp(report, vendor_get_base, 64) == 0);
    make_report(report, GET_FUNC, 64, 54, NULL);
    assert(memcmp(report, vendor_get_func, 64) == 0);
    uint8_t level = 37;
    make_report(report, SET_FUNC, 73, 1, &level);
    assert(memcmp(report, vendor_set_37, 64) == 0);
    /* A write changes exactly one data byte, never neighboring color/effect. */
    for (size_t i = 9; i < 64; ++i) assert(report[i] == 0);
    uint16_t address = 0;
    assert(brightness_address(0, &address) && address == 9);
    assert(brightness_address(1, &address) && address == 73);
    assert(brightness_address(2, &address) && address == 137);
    assert(!brightness_address(3, &address));
    assert(!brightness_address(1000, &address));

    uint8_t off[2] = {EFFECT_OFF, 0};
    uint8_t off_report[64];
    make_report(off_report, SET_FUNC, 72, 2, off);
    assert(memcmp(off_report, vendor_off, 64) == 0);

    memcpy(reply, report, 64);
    reply[0] = 0xaa;
    assert(valid_reply(reply, 64, report));
    assert(!valid_reply(reply, 63, report));
    reply[8] ^= 1;
    assert(!valid_reply(reply, 64, report));
    reply[8] ^= 1;
    reply[5] = 9; /* A valid checksum from the wrong profile is still wrong. */
    reply[3] = checksum(reply);
    assert(!valid_reply(reply, 64, report));
    memcpy(reply, report, 64);
    reply[0] = 0xa3; /* Asynchronous lighting event, not a command response. */
    assert(!valid_reply(reply, 64, report));
    reply[0] = 0xaa;
    reply[1] = GET_FUNC;
    assert(!valid_reply(reply, 64, report));
    puts("Protocol fixtures and response validation passed.");
    return 0;
}
