#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define GV_MOTOR_INIT 0x40044d6eUL
#define GV_MOTOR_MOVE 0x40044d70UL
#define GV_MOTOR_POSITION 0x40044d74UL
#define GV_MOTOR_STATUS 0x40044d76UL

/* A new manual command or Stop cancels an in-flight absolute movement. */
static void cancel_token(char out[80])
{
    FILE *file = fopen("/tmp/anyka-ptz-cancel", "r");
    out[0] = 0;
    if (file) { if (!fgets(out, 80, file)) out[0] = 0; fclose(file); }
}
static int manual_marker(int cancel)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    char path[96];
    snprintf(path, sizeof(path), "/tmp/anyka-ptz-manual.%ld", (long)getpid());
    FILE *file = fopen(path, "w");
    if (!file) return -1;
    int bad = fprintf(file, "%lu\n", (unsigned long)now.tv_sec + 30) < 0;
    if (fclose(file)) bad = 1;
    if (bad || rename(path, "/tmp/anyka-ptz-manual")) { unlink(path); return -1; }
    if (cancel) {
        snprintf(path, sizeof(path), "/tmp/anyka-ptz-cancel.%ld", (long)getpid());
        file = fopen(path, "w");
        if (!file) return -1;
        bad = fprintf(file, "%ld:%ld:%ld\n", (long)getpid(), (long)now.tv_sec, now.tv_nsec) < 0;
        if (fclose(file)) bad = 1;
        if (bad || rename(path, "/tmp/anyka-ptz-cancel")) { unlink(path); return -1; }
    }
    return 0;
}
static int absolute_move(int device, int positions[2], const int target[2], const char *token)
{
    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) return 1;
    for (int axis = 0; axis < 2; axis++) {
        while (positions[axis] != target[axis]) {
            char current[80]; cancel_token(current);
            if (strcmp(current, token)) return 5;
            if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec - start.tv_sec >= 60) return 1;
            if (manual_marker(0)) return 1;
            int delta = target[axis] - positions[axis], steps = abs(delta);
            if (steps > 32) steps = 32;
            int before = positions[axis];
            int move[4] = {axis, delta < 0 ? 1 : 0, steps, 0};
            if (ioctl(device, GV_MOTOR_MOVE, move) < 0) return errno == ENOSPC ? 3 : 1;
            int stopped = 0;
            for (int attempt = 0; attempt < 150; attempt++) {
                usleep(20000);
                cancel_token(current);
                if (strcmp(current, token)) return 5;
                int state[2] = {axis, 0};
                if (ioctl(device, GV_MOTOR_STATUS, state) < 0) return 1;
                if (!state[1]) { stopped = 1; break; }
            }
            if (!stopped) return 1; /* Closing the driver stops both axes. */
            int position[4] = {axis, 0, 0, 0};
            if (ioctl(device, GV_MOTOR_POSITION, position) < 0) return 1;
            positions[axis] = position[2];
            if (positions[axis] != before + (delta < 0 ? -steps : steps)) return 1;
        }
    }
    return manual_marker(0) ? 1 : 0;
}

int main(int argc, char **argv)
{
    int device;
    int axis;
    int result = 0;
    int direction = 0;
    int moving = 0;
    int homing = 0;
    int tracking = 0;
    int absolute = 0, target[2] = {0, 0};
    char token[80] = "";
    int lock_device = -1;
    int positions[2] = {0, 0};
    if (argc == 4 && !strcmp(argv[1], "move-to")) {
        for (int i = 0; i < 2; i++) {
            char *end; long n = strtol(argv[i+2], &end, 10);
            if (!*argv[i+2] || *end || n < 0 || n > (i ? 967 : 4039)) return 2;
            target[i] = (int)n;
        }
        absolute = moving = 1;
    } else if (argc != 2) return 2;
    if (absolute) { axis = 0; }
    else if (!strcmp(argv[1], "track-left")) { axis = 0; direction = 1; moving = tracking = 1; }
    else if (!strcmp(argv[1], "track-right")) { axis = 0; moving = tracking = 1; }
    else if (!strcmp(argv[1], "track-up")) { axis = 1; moving = tracking = 1; }
    else if (!strcmp(argv[1], "track-down")) { axis = 1; direction = 1; moving = tracking = 1; }
    else if (!strcmp(argv[1], "left")) { axis = 0; direction = 1; moving = 1; }
    else if (!strcmp(argv[1], "right")) { axis = 0; moving = 1; }
    else if (!strcmp(argv[1], "up")) { axis = 1; moving = 1; }
    else if (!strcmp(argv[1], "down")) { axis = 1; direction = 1; moving = 1; }
    else if (!strcmp(argv[1], "home")) { axis = 0; homing = 1; }
    else if (strcmp(argv[1], "status") && strcmp(argv[1], "stop") &&
             strcmp(argv[1], "init")) { return 2; }
    else { axis = 0; }
    if ((moving && !tracking) || homing || !strcmp(argv[1], "stop")) {
        if (manual_marker(1)) return 1;
        cancel_token(token);
    }
    if (strcmp(argv[1], "stop")) {
        lock_device = open("/tmp/anyka-ptz.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        int locked = 0;
        if (lock_device >= 0) for (int attempt = 0; attempt < (tracking ? 1 : 50); attempt++) {
            if (!flock(lock_device, LOCK_EX | LOCK_NB)) { locked = 1; break; }
            usleep(10000);
        }
        if (!locked) {
            perror("motor lock");
            if (lock_device >= 0) { close(lock_device); }
            return 1;
        }
    }
    if (moving && access("/tmp/anyka-ptz-homed", F_OK) < 0) {
        fputs("Motor homing required\n", stderr);
        close(lock_device);
        return 4;
    }
    device = open("/dev/gv-motor", O_RDWR | O_CLOEXEC);
    if (device < 0) {
        perror("gv-motor");
        if (lock_device >= 0) { close(lock_device); }
        return 1;
    }
    if (homing) {
        unlink("/tmp/anyka-ptz-homed");
        int parameters[12] = {4039, 967, 1, 0, 130, 130, -1, -1, 0, 0, 0, 0};
        int stopped = 0;
        if (ioctl(device, GV_MOTOR_INIT, parameters) < 0) {
            perror("motor home");
            result = 1;
            goto finish;
        }
        for (int attempt = 0; attempt < 1200; ++attempt) {
            int active = 0;
            usleep(100000);
            for (int index = 0; index < 2; ++index) {
                int state[2] = {index, 0};
                if (ioctl(device, GV_MOTOR_STATUS, state) < 0) {
                    perror("motor homing status");
                    result = 1;
                    goto finish;
                }
                if (state[1]) { active = 1; }
            }
            if (!active) { stopped = 1; break; }
        }
        if (!stopped) {
            fputs("Motor homing timed out\n", stderr);
            result = 1;
            goto finish;
        }
    }
    for (int index = 0; index < 2; ++index) {
        int position[4] = {index, 0, 0, 0};
        if (ioctl(device, GV_MOTOR_POSITION, position) < 0) {
            if (index == 0 && !strcmp(argv[1], "init") && errno == EINVAL) {
                int parameters[12] = {4039, 967, 2, 0, 130, 130, -1, -1, 0, 0, 0, 0};
                if (ioctl(device, GV_MOTOR_INIT, parameters) < 0) {
                    perror("motor init");
                    result = 1;
                    goto finish;
                }
                if (ioctl(device, GV_MOTOR_POSITION, position) == 0) {
                    positions[index] = position[2];
                    continue;
                }
            }
            perror("motor position");
            result = 1;
            goto finish;
        }
        positions[index] = position[2];
    }
    if (homing && (positions[0] < 2017 || positions[0] > 2021 ||
                   positions[1] < 481 || positions[1] > 485)) {
        fprintf(stderr, "Motor homing did not reach the center: pan=%d tilt=%d\n",
                positions[0], positions[1]);
        result = 1;
        goto finish;
    }
    if (homing) {
        int marker = open("/tmp/anyka-ptz-homed", O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
        if (marker < 0) {
            perror("motor homing marker");
            result = 1;
            goto finish;
        }
        close(marker);
    }
    if (absolute) {
        result = absolute_move(device, positions, target, token);
        goto finish;
    }
    if (moving) {
        int movement[4] = {axis, direction, tracking ? 8 : 32, 0};
        if (tracking) {
            FILE *marker = fopen("/tmp/anyka-ptz-manual", "r");
            unsigned long until = 0; struct timespec clock;
            if (marker) { if (fscanf(marker, "%lu", &until) != 1) until = ~0UL; fclose(marker); }
            if (clock_gettime(CLOCK_MONOTONIC, &clock) || (unsigned long)clock.tv_sec < until ||
                positions[axis] < 64 || positions[axis] > (axis ? 903 : 3975)) { result = 4; goto finish; }
        }
        int before = positions[axis];
        int stopped = 0;
        if (ioctl(device, GV_MOTOR_MOVE, movement) < 0) {
            int error_code = errno;
            perror("motor move");
            result = error_code == ENOSPC ? 3 : 1;
            goto finish;
        }
        for (int attempt = 0; attempt < 150; ++attempt) {
            int state[2] = {axis, 0};
            usleep(20000);
            if (ioctl(device, GV_MOTOR_STATUS, state) < 0) {
                result = 1;
                break;
            }
            if (!state[1]) { stopped = 1; break; }
            if (attempt == 149) { result = 1; }
        }
        movement[2] = 0;
        if (!stopped && ioctl(device, GV_MOTOR_MOVE, movement) < 0) { result = 1; }
        int position[4] = {axis, 0, 0, 0};
        if (ioctl(device, GV_MOTOR_POSITION, position) < 0) { result = 1; }
        positions[axis] = position[2];
        if (positions[axis] == before) {
            fputs("Motor position did not change\n", stderr);
            result = 1;
        }
    }
finish:
    close(device);
    if (lock_device >= 0) { close(lock_device); }
    if (!result) {
        printf("{\"ok\":true,\"pan\":%d,\"tilt\":%d}\n", positions[0], positions[1]);
    }
    return result;
}
