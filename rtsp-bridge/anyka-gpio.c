#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define GPIO_INIT 0x4004476eUL
#define GPIO_SET 0x40044770UL
#define GPIO_GET 0x40044771UL

struct gpio_request {
    int pin;
    int direction;
    int value;
    int force;
};

static int gpio_read(int device, int pin)
{
    struct gpio_request request = {pin, 1, 0, 0};
    if (ioctl(device, GPIO_GET, &request) < 0)
        return -1;
    return request.value;
}

static int gpio_write(int device, int pin, int value)
{
    int initial = gpio_read(device, pin);
    if (initial < 0)
        return -1;
    struct gpio_request request = {pin, 1, initial, 0};
    if (ioctl(device, GPIO_INIT, &request) < 0)
        return -1;
    request.value = value;
    if (ioctl(device, GPIO_SET, &request) < 0)
        return -1;
    if (gpio_read(device, pin) != value) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static const char *requested_mode(void)
{
    static char mode[16];
    FILE *input = fopen("/tmp/anyka-daynight-mode", "r");
    int valid = input && fgets(mode, sizeof(mode), input);
    if (input)
        fclose(input);
    if (valid) {
        mode[strcspn(mode, "\r\n")] = 0;
        if (!strcmp(mode, "auto") || !strcmp(mode, "day") ||
            !strcmp(mode, "night") || !strcmp(mode, "manual"))
            return mode;
    }
    return "manual";
}

static int store_mode(const char *mode)
{
    FILE *output = fopen("/tmp/anyka-daynight-mode.tmp", "w");
    if (!output)
        return -1;
    int result = fprintf(output, "%s\n", mode) < 0;
    if (fclose(output))
        result = 1;
    if (!result && rename("/tmp/anyka-daynight-mode.tmp", "/tmp/anyka-daynight-mode"))
        result = 1;
    return result ? -1 : 0;
}

static int pulse_ircut(int device, int night)
{
    sigset_t blocked, previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGINT);
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) < 0)
        return -1;
    int result = gpio_write(device, 18, 0);
    if (gpio_write(device, 17, 0) < 0)
        result = -1;
    if (!result)
        result = gpio_write(device, night ? 17 : 18, 1);
    if (!result) {
        struct timespec remaining = {0, 100000000};
        while (nanosleep(&remaining, &remaining) < 0) {
            if (errno != EINTR) {
                result = -1;
                break;
            }
        }
    }
    if (gpio_write(device, 18, 0) < 0)
        result = -1;
    if (gpio_write(device, 17, 0) < 0)
        result = -1;
    sigprocmask(SIG_SETMASK, &previous, NULL);
    return result;
}

int main(int argc, char **argv)
{
    int pin = -1;
    int value = 0;
    int ircut = -1;
    const char *mode = NULL;
    if (argc != 2)
        return 2;
    if (!strcmp(argv[1], "ir-on")) { pin = 56; value = 1; }
    else if (!strcmp(argv[1], "ir-off")) { pin = 56; }
    else if (!strcmp(argv[1], "auto-ir-on")) { pin = 56; value = 1; }
    else if (!strcmp(argv[1], "auto-ir-off")) { pin = 56; }
    else if (!strcmp(argv[1], "blue-on")) { pin = 68; }
    else if (!strcmp(argv[1], "blue-off")) { pin = 68; value = 1; }
    else if (!strcmp(argv[1], "speaker-on")) { pin = 67; }
    else if (!strcmp(argv[1], "speaker-off")) { pin = 67; value = 1; }
    else if (!strcmp(argv[1], "ircut-day")) { ircut = 0; }
    else if (!strcmp(argv[1], "ircut-night")) { ircut = 1; }
    else if (!strcmp(argv[1], "mode-auto")) { mode = "auto"; }
    else if (!strcmp(argv[1], "mode-day")) { mode = "day"; }
    else if (!strcmp(argv[1], "mode-night")) { mode = "night"; }
    else if (!strcmp(argv[1], "mode-manual")) { mode = "manual"; }
    else if (!strcmp(argv[1], "mode-status")) {
        printf("{\"ok\":true,\"mode\":\"%s\"}\n", requested_mode());
        return 0;
    }
    else if (strcmp(argv[1], "status") && strcmp(argv[1], "speaker-status"))
        return 2;
    int mode_lock = -1;
    if (mode || !strncmp(argv[1], "ir-", 3)) {
        mode_lock = open("/tmp/anyka-daynight.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (mode_lock < 0 || flock(mode_lock, LOCK_EX) < 0) {
            perror("Day/night busy");
            return 1;
        }
        if (store_mode(mode ? mode : "manual") < 0) {
            perror("Day/night mode");
            return 1;
        }
        if (mode) {
            printf("{\"ok\":true,\"mode\":\"%s\"}\n", mode);
            close(mode_lock);
            return 0;
        }
    }
    int lock = open("/tmp/anyka-gpio.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0 || flock(lock, LOCK_EX) < 0) {
        perror("GPIO busy");
        return 1;
    }
    int device = open("/dev/gv-gpio", O_RDWR | O_CLOEXEC);
    if (device < 0 || (pin >= 0 && gpio_write(device, pin, value) < 0) ||
        (ircut >= 0 && pulse_ircut(device, ircut) < 0)) {
        perror("GPIO");
        return 1;
    }
    if (!strncmp(argv[1], "speaker-", 8)) {
        int speaker = gpio_read(device, 67);
        if (speaker < 0) {
            perror("Speaker GPIO status");
            return 1;
        }
        printf("{\"ok\":true,\"speaker\":%s}\n", speaker ? "false" : "true");
        close(device);
        close(lock);
        return 0;
    }
    int infrared = gpio_read(device, 56);
    int blue = gpio_read(device, 68);
    if (infrared < 0 || blue < 0) {
        perror("GPIO status");
        return 1;
    }
    printf("{\"ok\":true,\"infrared\":%s,\"blue\":%s}\n",
        infrared ? "true" : "false", blue ? "false" : "true");
    close(device);
    close(lock);
    if (mode_lock >= 0)
        close(mode_lock);
    return 0;
}