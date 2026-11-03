#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <systemd/sd-bus.h>

static uint32_t level = 42, effect = 3;
static int get(sd_bus_message *m, void *data, sd_bus_error *error)
{
    (void)data; (void)error;
    return sd_bus_reply_method_return(m, "buuu", 1, level, effect, (uint32_t)1);
}
static int set(sd_bus_message *m, void *data, sd_bus_error *error)
{
    (void)data; (void)error;
    uint32_t value;
    assert(sd_bus_message_read(m, "u", &value) >= 0);
    if (strcmp(sd_bus_message_get_member(m), "SetEffect") == 0) {
        assert(value == 6);
        effect = value;
    } else {
        assert(value == 0);
        level = value;
        effect = 255;
    }
    return sd_bus_reply_method_return(m, "");
}
static const sd_bus_vtable vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetState", "", "buuu", get, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetBrightness", "u", "", set, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetEffect", "u", "", set, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};
int main(void)
{
    sd_bus *bus = NULL;
    assert(sd_bus_open_user(&bus) >= 0);
    assert(sd_bus_add_object_vtable(bus, NULL, "/local/NuPhy/Backlight",
                                   "local.NuPhy.Backlight", vtable, NULL) >= 0);
    assert(sd_bus_request_name(bus, "local.NuPhy.Backlight", 0) >= 0);
    for (;;) {
        int rc = sd_bus_process(bus, NULL);
        assert(rc >= 0);
        if (rc == 0) assert(sd_bus_wait(bus, UINT64_MAX) >= 0);
    }
}
