/* Exercise real read/write/poll framing against a socket-backed keyboard.
 * No USB hardware or root access required. */
#define main service_main
#include "../src/nuphy-backlight.c"
#undef main
#include <assert.h>
#include <sys/socket.h>
#include <sys/wait.h>

static void emulate_keyboard(int fd, unsigned mode, bool corrupt_only, bool effects)
{
    uint8_t memory[192];
    memset(memory, 0x63, sizeof(memory));
    memory[8] = memory[72] = memory[136] = 3;
    memory[9] = memory[73] = memory[137] = 42;
    int writes = 0;
    for (;;) {
        uint8_t wire[65], reply[64];
        ssize_t n = read(fd, wire, sizeof(wire));
        if (n == 0) break;
        assert(n == 65 && wire[0] == 0);
        uint8_t *request = wire + 1;
        assert(request[0] == 0x55 && request[3] == checksum(request));
        memcpy(reply, request, sizeof(reply));
        reply[0] = 0xaa;
        unsigned address = (unsigned)request[5] | (unsigned)request[6] << 8;
        if (request[1] == GET_BASE) {
            assert(request[4] == 16 && address == 0);
            memset(reply + 8, 0, 56);
            reply[8] = (uint8_t)mode;
            reply[9] = 4;
        } else if (request[1] == GET_FUNC) {
            assert(request[4] == 54 && address == mode * 64);
            memcpy(reply + 8, memory + address, 54);
        } else {
            assert(request[1] == SET_FUNC);
            if (request[4] == 2) {
                assert(effects && address == mode * 64 + 8);
                assert(request[8] <= MAX_EFFECT || request[8] == EFFECT_OFF);
                assert(request[9] <= 100);
                if (request[8] == EFFECT_OFF) assert(request[9] == 0);
            } else {
                assert(request[4] == 1 && address == mode * 64 + 9 && request[8] == 37);
            }
            for (size_t i = 8 + request[4]; i < 64; ++i) assert(request[i] == 0);
            memcpy(memory + address, request + 8, request[4]);
            ++writes;
        }
        reply[3] = checksum(reply);
        if (corrupt_only) {
            reply[3] ^= 1;
            assert(write(fd, reply, 64) == 64);
            continue;
        }
        /* An async status event and a damaged packet must not be accepted. */
        uint8_t noise[64] = {0xa3};
        assert(write(fd, noise, 64) == 64);
        reply[3] ^= 1;
        assert(write(fd, reply, 64) == 64);
        reply[3] ^= 1;
        assert(write(fd, reply, 64) == 64);
    }
    if (mode <= 2 && !corrupt_only) assert(writes == (effects ? 6 : 1));
    for (size_t i = 0; i < sizeof(memory); ++i)
        if (i != 9 && i != 73 && i != 137 && i != 8 && i != 72 && i != 136)
            assert(memory[i] == 0x63);
    close(fd);
    _exit(0);
}

static void scenario(unsigned mode, bool corrupt_only)
{
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) { close(pair[0]); emulate_keyboard(pair[1], mode, corrupt_only, false); }
    close(pair[1]);
    assert(fcntl(pair[0], F_SETFL, O_NONBLOCK) == 0);
    unsigned actual_mode, level;
    int rc = read_state(pair[0], &actual_mode, &level);
    if (mode > 2 || corrupt_only) assert(rc == -1);
    else {
        assert(rc == 0 && actual_mode == mode && level == 42);
        assert(set_brightness(pair[0], mode, 101) == -1);
        assert(set_brightness(pair[0], 3, 37) == -1);
        assert(set_brightness(pair[0], mode, 37) == 0);
        assert(read_state(pair[0], &actual_mode, &level) == 0);
        assert(actual_mode == mode && level == 37);
    }
    close(pair[0]);
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void off_and_restore(void)
{
    char state_dir[] = "/tmp/nuphy-test-XXXXXX";
    assert(mkdtemp(state_dir));
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) { close(pair[0]); emulate_keyboard(pair[1], 1, false, true); }
    close(pair[1]);
    assert(fcntl(pair[0], F_SETFL, O_NONBLOCK) == 0);
    struct controller c = {.hid = pair[0], .state_dir = state_dir};
    load_effects(&c);
    assert(apply_level(&c, 0) == 0 && c.effect == EFFECT_OFF && c.level == 0);
    /* Simulate restarting while off: recovery comes from disk, not RAM. */
    c.remembered[1] = 17;
    load_effects(&c);
    assert(c.remembered[1] == 3);
    assert(apply_level(&c, 37) == 0 && c.effect == 3 && c.level == 37);
    assert(apply_effect(&c, 6) == 0 && c.effect == 6 && c.level == 37);
    assert(apply_level(&c, 0) == 0 && c.effect == EFFECT_OFF);
    c.remembered[1] = 3;
    load_effects(&c);
    assert(c.remembered[1] == 6);
    assert(apply_level(&c, 37) == 0 && c.effect == 6 && c.level == 37);
    assert(apply_effect(&c, EFFECT_OFF) == 0 && c.effect == EFFECT_OFF && c.level == 0);
    assert(apply_effect(&c, 23) == -1);
    close(pair[0]);
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    char path[256];
    snprintf(path, sizeof(path), "%s/effect-1", state_dir);
    assert(unlink(path) == 0 && rmdir(state_dir) == 0);
}

int main(void)
{
    scenario(0, false);
    scenario(1, false);
    scenario(2, false);
    scenario(3, false);
    scenario(0, true);
    off_and_restore();
    puts("Simulated keyboard transport, profile selection, preservation and timeout tests passed.");
    return 0;
}
