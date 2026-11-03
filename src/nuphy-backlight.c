#define _GNU_SOURCE
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/hidraw.h>
#include <linux/uleds.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-login.h>
#include <time.h>
#include <unistd.h>

#define LED_NAME "nuphy::kbd_backlight"
#define LED_PATH "/sys/class/leds/" LED_NAME "/brightness"
#define UPOWER "org.freedesktop.UPower"
#define UPOWER_PATH "/org/freedesktop/UPower"
#define KBD_INTERFACE UPOWER ".KbdBacklight"
#define CONTROL_NAME "local.NuPhy.Backlight"
#define CONTROL_PATH "/local/NuPhy/Backlight"

struct controller {
    int hid, led;
    unsigned mode, level, effect;
    unsigned remembered[3];
    const char *state_dir;
};

static volatile sig_atomic_t stopping;
static void stop(int signo) { (void)signo; stopping = 1; }

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Air75 HE's unnumbered, 64-byte input/output configuration collection.
 * Matching the descriptor prevents opening either keyboard input interface. */
static const unsigned char control_descriptor[] = {
    0x05,0x01,0x09,0x00,0xa1,0x01,0x15,0x00,0x26,0xff,0x00,
    0x19,0x00,0x29,0x08,0x95,0x40,0x75,0x08,0x81,0x02,
    0x19,0x01,0x29,0x08,0x95,0x40,0x75,0x08,0x91,0x02,0xc0
};

static int find_keyboard(void)
{
    glob_t paths = {0};
    int chosen = -1;
    if (glob("/dev/hidraw*", 0, NULL, &paths) != 0) return -1;
    for (size_t i = 0; i < paths.gl_pathc; ++i) {
        int fd = open(paths.gl_pathv[i], O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        struct hidraw_devinfo info = {0};
        struct hidraw_report_descriptor desc = {0};
        int size = 0;
        bool matches = ioctl(fd, HIDIOCGRAWINFO, &info) == 0
            && info.vendor == 0x19f5 && info.product == 0x6120
            && ioctl(fd, HIDIOCGRDESCSIZE, &size) == 0
            && size == (int)sizeof(control_descriptor);
        if (matches) {
            desc.size = (unsigned)size;
            matches = ioctl(fd, HIDIOCGRDESC, &desc) == 0
                && memcmp(desc.value, control_descriptor, sizeof(control_descriptor)) == 0;
        }
        if (matches && flock(fd, LOCK_EX | LOCK_NB) == 0) {
            chosen = fd;
            fprintf(stderr, "Air75 HE control interface: %s\n", paths.gl_pathv[i]);
            break;
        }
        close(fd);
    }
    globfree(&paths);
    return chosen;
}

static int exchange(int fd, const uint8_t request[REPORT_SIZE], uint8_t reply[REPORT_SIZE])
{
    uint8_t discarded[REPORT_SIZE];
    for (int i = 0; i < 128 && read(fd, discarded, sizeof(discarded)) > 0; ++i) {}
    uint8_t output[REPORT_SIZE + 1] = {0}; /* hidraw requires report ID 0. */
    memcpy(output + 1, request, REPORT_SIZE);
    if (write(fd, output, sizeof(output)) != (ssize_t)sizeof(output)) return -1;
    int64_t deadline = now_ms() + 600;
    while (!stopping) {
        int remaining = (int)(deadline - now_ms());
        if (remaining <= 0) break;
        struct pollfd p = {.fd = fd, .events = POLLIN};
        int rc = poll(&p, 1, remaining);
        if (rc < 0 && errno == EINTR) continue;
        if (rc <= 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL))) break;
        ssize_t n = read(fd, reply, REPORT_SIZE);
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (n <= 0) break;
        if (valid_reply(reply, (size_t)n, request)) return 0;
    }
    return -1;
}

static int read_lighting(int fd, unsigned *mode, unsigned *brightness, unsigned *effect)
{
    uint8_t request[REPORT_SIZE], reply[REPORT_SIZE];
    make_report(request, GET_BASE, 0, 16, NULL);
    if (exchange(fd, request, reply) < 0) return -1;
    *mode = reply[8];
    uint16_t address;
    if (!brightness_address(*mode, &address)) return -1;
    /* Read the same complete function block NuPhyIO reads. */
    make_report(request, GET_FUNC, (uint16_t)(*mode * MODE_SIZE), 54, NULL);
    if (exchange(fd, request, reply) < 0 || reply[8 + BRIGHTNESS_OFFSET] > MAX_BRIGHTNESS)
        return -1;
    *brightness = reply[8 + BRIGHTNESS_OFFSET];
    *effect = reply[8 + EFFECT_OFFSET];
    if (*effect > MAX_EFFECT && *effect != EFFECT_OFF) return -1;
    return 0;
}

static int read_state(int fd, unsigned *mode, unsigned *brightness)
{
    unsigned effect;
    return read_lighting(fd, mode, brightness, &effect);
}

static int set_brightness(int fd, unsigned mode, unsigned value)
{
    uint8_t request[REPORT_SIZE], reply[REPORT_SIZE], byte = (uint8_t)value;
    uint16_t address;
    if (value > MAX_BRIGHTNESS || !brightness_address(mode, &address)) return -1;
    make_report(request, SET_FUNC, address, 1, &byte);
    return exchange(fd, request, reply);
}

static int set_sysfs(unsigned value)
{
    int fd = open(LED_PATH, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char text[16];
    int n = snprintf(text, sizeof(text), "%u\n", value);
    int rc = write(fd, text, (size_t)n) == n ? 0 : -1;
    close(fd);
    return rc;
}

/* Use only this LED's UPower object, so other keyboard backlights are untouched.
 * SetBrightness makes UPower emit its normal change signals to PowerDevil. */
static void sync_upower(unsigned value)
{
    sd_bus *bus = NULL;
    sd_bus_message *reply = NULL;
    if (sd_bus_open_system(&bus) < 0) return;
    sd_bus_set_method_call_timeout(bus, 500000);
    int rc = sd_bus_call_method(bus, UPOWER, UPOWER_PATH, UPOWER,
                               "EnumerateKbdBacklights", NULL, &reply, "");
    if (rc >= 0 && sd_bus_message_enter_container(reply, 'a', "o") >= 0) {
        const char *path;
        while (sd_bus_message_read(reply, "o", &path) > 0) {
            char *native = NULL;
            if (sd_bus_get_property_string(bus, UPOWER, path, KBD_INTERFACE,
                                           "NativePath", NULL, &native) >= 0) {
                const char *base = strrchr(native, '/');
                if (strcmp(base ? base + 1 : native, LED_NAME) == 0)
                    sd_bus_call_method(bus, UPOWER, path, KBD_INTERFACE,
                                       "SetBrightness", NULL, NULL, "i", (int)value);
            }
            free(native);
        }
    } else {
        /* Older UPower exposes one shared object. Use it only when ours is the
         * sole keyboard LED. Never propagate this keyboard's level to others. */
        glob_t leds = {0};
        if (glob("/sys/class/leds/*kbd_backlight", 0, NULL, &leds) == 0
            && leds.gl_pathc == 1 && strcmp(leds.gl_pathv[0], "/sys/class/leds/" LED_NAME) == 0)
            sd_bus_call_method(bus, UPOWER, UPOWER_PATH "/KbdBacklight", KBD_INTERFACE,
                               "SetBrightness", NULL, NULL, "i", (int)value);
        globfree(&leds);
    }
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
}

static int create_led(void)
{
    int fd = open("/dev/uleds", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    struct uleds_user_dev led = {.name = LED_NAME, .max_brightness = MAX_BRIGHTNESS};
    if (write(fd, &led, sizeof(led)) != sizeof(led)) { close(fd); return -1; }
    return fd;
}

static void publish_level(unsigned value)
{
    if (set_sysfs(value) < 0) fprintf(stderr, "Cannot update %s: %s\n", LED_PATH, strerror(errno));
    sync_upower(value);
}

static void load_effects(struct controller *c)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        c->remembered[mode] = 3; /* Static if the keyboard was already off. */
        char path[256];
        snprintf(path, sizeof(path), "%s/effect-%u", c->state_dir, mode);
        FILE *file = fopen(path, "r");
        unsigned effect;
        if (file) {
            if (fscanf(file, "%u", &effect) == 1 && effect <= MAX_EFFECT)
                c->remembered[mode] = effect;
            fclose(file);
        }
    }
}

static int remember_effect(struct controller *c, unsigned effect)
{
    if (c->mode > 2 || effect > MAX_EFFECT) return -1;
    char path[256], temp[256];
    snprintf(path, sizeof(path), "%s/effect-%u", c->state_dir, c->mode);
    snprintf(temp, sizeof(temp), "%s/effect-%u.tmp", c->state_dir, c->mode);
    FILE *file = fopen(temp, "w");
    if (!file) return -1;
    bool ok = fprintf(file, "%u\n", effect) > 0;
    if (fclose(file) != 0) ok = false;
    if (!ok || rename(temp, path) != 0) { unlink(temp); return -1; }
    c->remembered[c->mode] = effect;
    return 0;
}

static int refresh(struct controller *c)
{
    if (c->hid < 0) return -1;
    unsigned mode, level, effect;
    if (read_lighting(c->hid, &mode, &level, &effect) < 0) return -1;
    c->mode = mode;
    c->level = effect == EFFECT_OFF ? 0 : level;
    c->effect = effect;
    return 0;
}

static int write_lighting(struct controller *c, unsigned effect, unsigned value)
{
    uint8_t request[REPORT_SIZE], reply[REPORT_SIZE];
    uint8_t data[2] = {(uint8_t)effect, (uint8_t)value};
    unsigned mode = c->mode;
    /* One adjacent two-byte write preserves speed, color, and side lights. */
    make_report(request, SET_FUNC, (uint16_t)(mode * MODE_SIZE + EFFECT_OFFSET), 2, data);
    if (exchange(c->hid, request, reply) < 0 || refresh(c) < 0) return -1;
    return c->mode == mode && c->effect == effect && c->level == value ? 0 : -1;
}

static int apply_level(struct controller *c, unsigned value)
{
    if (value > MAX_BRIGHTNESS || refresh(c) < 0) return -1;
    unsigned effect = c->effect;
    if (value == 0) {
        if (effect != EFFECT_OFF && remember_effect(c, effect) < 0) return -1;
        return write_lighting(c, EFFECT_OFF, 0);
    }
    if (effect == EFFECT_OFF) return write_lighting(c, c->remembered[c->mode], value);
    unsigned mode = c->mode;
    if (set_brightness(c->hid, mode, value) < 0 || refresh(c) < 0) return -1;
    return c->mode == mode && c->level == value ? 0 : -1;
}

static int apply_effect(struct controller *c, unsigned effect)
{
    if (effect == EFFECT_OFF) return apply_level(c, 0);
    if (effect > MAX_EFFECT || refresh(c) < 0) return -1;
    if (remember_effect(c, effect) < 0) return -1;
    return write_lighting(c, effect, c->level ? c->level : 50);
}

static int get_state(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)error;
    struct controller *c = userdata;
    return sd_bus_reply_method_return(message, "buuu", c->hid >= 0,
                                       c->level, c->effect, c->mode);
}

static bool authorized(sd_bus_message *message)
{
    sd_bus_creds *creds = NULL;
    uid_t uid;
    bool ok = sd_bus_query_sender_creds(message, SD_BUS_CREDS_EUID, &creds) >= 0
        && sd_bus_creds_get_euid(creds, &uid) >= 0
        && (uid == 0 || sd_uid_is_on_seat(uid, 1, "seat0") > 0);
    sd_bus_creds_unref(creds);
    return ok;
}

static int change_lighting(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)error;
    struct controller *c = userdata;
    if (!authorized(message))
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED,
                                          "Only the active desktop user or root can change lighting.");
    uint32_t value;
    int rc = sd_bus_message_read(message, "u", &value);
    if (rc < 0) return rc;
    bool is_effect = strcmp(sd_bus_message_get_member(message), "SetEffect") == 0;
    if ((!is_effect && value > MAX_BRIGHTNESS)
        || (is_effect && value > MAX_EFFECT && value != EFFECT_OFF))
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS, "Value out of range.");
    rc = is_effect ? apply_effect(c, value) : apply_level(c, value);
    if (rc < 0) {
        if (c->hid >= 0) { close(c->hid); c->hid = -1; }
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED,
                                          "Could not change keyboard lighting. Check the service journal.");
    }
    publish_level(c->level);
    return sd_bus_reply_method_return(message, "");
}

static const sd_bus_vtable control_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetState", "", "buuu", get_state, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetBrightness", "u", "", change_lighting, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetEffect", "u", "", change_lighting, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

static int probe(void)
{
    int fd = find_keyboard();
    if (fd < 0) {
        fprintf(stderr, "No accessible Air75 HE (19f5:6120) control interface.\n"
                        "Connect it by USB and run with permission to open /dev/hidraw*.\n");
        return 1;
    }
    unsigned mode, brightness;
    int rc = read_state(fd, &mode, &brightness);
    close(fd);
    if (rc < 0) { fprintf(stderr, "Keyboard did not return a valid brightness report.\n"); return 1; }
    printf("NuPhy Air75 HE: mode %u, brightness %u%%\n", mode, brightness);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--probe") == 0) return probe();
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: nuphy-backlight [--probe|--help]\n"
             "Without arguments, bridge Air75 HE brightness to KDE via /dev/uleds.\n"
             "--probe reads hardware state without changing settings.");
        return 0;
    }
    if (argc != 1) { fprintf(stderr, "Unknown argument; use --help.\n"); return 2; }
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    int lock = open("/run/nuphy-backlight.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) {
        fprintf(stderr, "Cannot lock service (already running or insufficient permissions).\n");
        if (lock >= 0) close(lock);
        return 1;
    }
    struct controller c = {.hid = find_keyboard(), .led = -1,
                           .state_dir = "/var/lib/nuphy-backlight"};
    load_effects(&c);
    if (c.hid >= 0 && refresh(&c) < 0) { close(c.hid); c.hid = -1; }
    /* A stored zero level gets a real off effect, including after upgrading. */
    if (c.hid >= 0 && c.level == 0 && c.effect != EFFECT_OFF && apply_level(&c, 0) < 0)
        fprintf(stderr, "Could not apply explicit off effect. Check state directory permissions.\n");
    c.led = create_led();
    if (c.led < 0) {
        fprintf(stderr, "Cannot create backlight: %s. Load uleds and check permissions.\n", strerror(errno));
        if (c.hid >= 0) close(c.hid);
        close(lock);
        return 1;
    }
    if (set_sysfs(c.level) < 0) {
        fprintf(stderr, "Cannot initialize %s: %s\n", LED_PATH, strerror(errno));
        if (c.hid >= 0) close(c.hid);
        close(c.led);
        close(lock);
        return 1;
    }
    sd_bus *bus = NULL;
    if (sd_bus_open_system(&bus) < 0
        || sd_bus_add_object_vtable(bus, NULL, CONTROL_PATH, CONTROL_NAME, control_vtable, &c) < 0
        || sd_bus_request_name(bus, CONTROL_NAME, 0) < 0) {
        fprintf(stderr, "Lighting widget API unavailable. Install the D-Bus policy and restart.\n");
        bus = sd_bus_unref(bus);
    }
    fprintf(stderr, "Backlight bridge ready (%u%%, effect %u); checking every 2 seconds.\n", c.level, c.effect);
    sd_notify(0, "READY=1");
    int64_t next_check = now_ms() + 2000;
    bool waiting_logged = false;
    while (!stopping) {
        if (bus) {
            for (int i = 0; i < 32; ++i) {
                int processed = sd_bus_process(bus, NULL);
                if (processed < 0) { bus = sd_bus_unref(bus); break; }
                if (!processed) break;
            }
        }
        int64_t remaining = next_check - now_ms();
        if (bus) {
            uint64_t bus_deadline;
            if (sd_bus_get_timeout(bus, &bus_deadline) >= 0 && bus_deadline != UINT64_MAX) {
                int64_t bus_remaining = (int64_t)(bus_deadline / 1000) - now_ms();
                if (bus_remaining < remaining) remaining = bus_remaining;
            }
        }
        int timeout = remaining > 0 ? (int)remaining : 0;
        struct pollfd p[2] = {{.fd = c.led, .events = POLLIN},
                             {.fd = bus ? sd_bus_get_fd(bus) : -1,
                              .events = bus ? (short)sd_bus_get_events(bus) : 0}};
        int rc = poll(p, 2, timeout);
        if (rc < 0) { if (errno == EINTR) continue; break; }
        if (p[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (p[0].revents & POLLIN) {
            int desired = -1, latest;
            while (read(c.led, &latest, sizeof(latest)) == sizeof(latest)) desired = latest;
            if (desired >= 0 && desired <= MAX_BRIGHTNESS
                && ((unsigned)desired != c.level || (desired == 0 && c.effect != EFFECT_OFF))) {
                if (apply_level(&c, (unsigned)desired) < 0) {
                    fprintf(stderr, "Brightness change failed; waiting for keyboard/re-reading state.\n");
                    if (c.hid >= 0) { close(c.hid); c.hid = -1; }
                }
                publish_level(c.level);
            }
        }
        if (now_ms() >= next_check) {
            if (c.hid < 0) c.hid = find_keyboard();
            unsigned previous = c.level;
            if (c.hid >= 0 && refresh(&c) == 0) {
                if (waiting_logged) fprintf(stderr, "Keyboard connected.\n");
                waiting_logged = false;
                if (c.level != previous) publish_level(c.level);
            } else {
                if (c.hid >= 0) { close(c.hid); c.hid = -1; }
                if (!waiting_logged) fprintf(stderr, "Waiting for an accessible Air75 HE.\n");
                waiting_logged = true;
            }
            next_check = now_ms() + 2000;
        }
    }
    sd_bus_unref(bus);
    if (c.hid >= 0) close(c.hid);
    close(c.led);
    close(lock);
    sd_notify(0, "STOPPING=1");
    return stopping ? 0 : 1;
}
