#include "android_abi.h"

#define O_RDONLY_ 0
#define O_WRONLY_ 1
#define O_NONBLOCK_ 0x800
#define POLLIN_ 1

#define EV_SYN_ 0
#define EV_KEY_ 1
#define EV_ABS_ 3
#define SYN_REPORT_ 0
#define BTN_TOUCH_ 330
#define ABS_X_ 0
#define ABS_Y_ 1
#define ABS_MT_SLOT_ 47
#define ABS_MT_TOUCH_MAJOR_ 48
#define ABS_MT_POSITION_X_ 53
#define ABS_MT_POSITION_Y_ 54
#define ABS_MT_TRACKING_ID_ 57

#define UI_DEV_CREATE_ 0x5501ul
#define UI_DEV_DESTROY_ 0x5502ul
#define UI_SET_EVBIT_ 0x40045564ul
#define UI_SET_KEYBIT_ 0x40045565ul
#define UI_SET_ABSBIT_ 0x40045567ul
#define UI_SET_PROPBIT_ 0x4004556eul
#define INPUT_PROP_DIRECT_ 0x01

#define UINPUT_PATH "/dev/uinput"
#define FIFO_PATH "/data/local/tmp/tapfifo"
#define DEVICE_NAME "goodix_test"

#define LOG_INFO 4
#define LOG_ERROR 6

extern int nanosleep(const time_spec *, time_spec *);
extern int mkfifo(const char *, unsigned);

typedef struct {
    u16 bustype, vendor, product, version;
} input_id;

typedef struct {
    char name[80];
    input_id id;
    u32 ff_effects_max;
    i32 absmax[64];
    i32 absmin[64];
    i32 absfuzz[64];
    i32 absflat[64];
} uinput_user_dev;

static volatile int g_stop;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static void emit(int fd, u16 type, u16 code, i32 value) {
    input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    write(fd, &ev, sizeof(ev));
}

static void wait_ms(int ms) {
    time_spec ts;
    ts.seconds = ms / 1000;
    ts.nanoseconds = (i64)(ms % 1000) * 1000000LL;
    nanosleep(&ts, 0);
}

static int parse_int(const char *s) {
    int value = 0;
    int seen = 0;
    for (usize i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') break;
        value = value * 10 + (s[i] - '0');
        seen = 1;
    }
    return seen ? value : -1;
}

static int parse_nums(const char *s, int *out, int max) {
    int n = 0;
    const char *p = s;
    while (n < max) {
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) break;
        int v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        out[n++] = v;
    }
    return n;
}

static void tap(int fd, int x, int y) {
    emit(fd, EV_ABS_, ABS_MT_SLOT_, 0);
    emit(fd, EV_ABS_, ABS_MT_TRACKING_ID_, 1);
    emit(fd, EV_ABS_, ABS_X_, x);
    emit(fd, EV_ABS_, ABS_Y_, y);
    emit(fd, EV_ABS_, ABS_MT_POSITION_X_, x);
    emit(fd, EV_ABS_, ABS_MT_POSITION_Y_, y);
    emit(fd, EV_ABS_, ABS_MT_TOUCH_MAJOR_, 40);
    emit(fd, EV_KEY_, BTN_TOUCH_, 1);
    emit(fd, EV_SYN_, SYN_REPORT_, 0);
    wait_ms(60);
    emit(fd, EV_ABS_, ABS_MT_TRACKING_ID_, -1);
    emit(fd, EV_KEY_, BTN_TOUCH_, 0);
    emit(fd, EV_SYN_, SYN_REPORT_, 0);
}

static void swipe(int fd, int x1, int y1, int x2, int y2, int dur_ms) {
    if (dur_ms < 40) dur_ms = 40;
    if (dur_ms > 2000) dur_ms = 2000;
    emit(fd, EV_ABS_, ABS_MT_SLOT_, 0);
    emit(fd, EV_ABS_, ABS_MT_TRACKING_ID_, 1);
    emit(fd, EV_ABS_, ABS_X_, x1);
    emit(fd, EV_ABS_, ABS_Y_, y1);
    emit(fd, EV_ABS_, ABS_MT_POSITION_X_, x1);
    emit(fd, EV_ABS_, ABS_MT_POSITION_Y_, y1);
    emit(fd, EV_ABS_, ABS_MT_TOUCH_MAJOR_, 40);
    emit(fd, EV_KEY_, BTN_TOUCH_, 1);
    emit(fd, EV_SYN_, SYN_REPORT_, 0);
    int steps = dur_ms / 8;
    if (steps < 4) steps = 4;
    for (int i = 1; i <= steps; i++) {
        int x = x1 + (x2 - x1) * i / steps;
        int y = y1 + (y2 - y1) * i / steps;
        emit(fd, EV_ABS_, ABS_MT_POSITION_X_, x);
        emit(fd, EV_ABS_, ABS_MT_POSITION_Y_, y);
        emit(fd, EV_SYN_, SYN_REPORT_, 0);
        wait_ms(8);
    }
    emit(fd, EV_ABS_, ABS_MT_TRACKING_ID_, -1);
    emit(fd, EV_KEY_, BTN_TOUCH_, 0);
    emit(fd, EV_SYN_, SYN_REPORT_, 0);
}

int main(int, char **, char **);

__attribute__((noreturn, no_stack_protector)) void SailTap_start(void *raw_arguments) {
    static const unsigned char zero_structors[128] = {0};
    __libc_init(raw_arguments, 0, main, zero_structors);
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 3) {
        __android_log_print(LOG_ERROR, "SailTap", "usage: uinput_tap <x> <y>");
        return 2;
    }
    int x = parse_int(argv[1]);
    int y = parse_int(argv[2]);
    if (x < 0 || y < 0) {
        __android_log_print(LOG_ERROR, "SailTap", "bad coordinates");
        return 2;
    }

    signal(15, on_signal);
    signal(2, on_signal);

    int fd = open(UINPUT_PATH, O_WRONLY_ | O_NONBLOCK_);
    if (fd < 0) {
        __android_log_print(LOG_ERROR, "SailTap", "uinput open failed");
        return 1;
    }

    ioctl(fd, UI_SET_EVBIT_, EV_KEY_);
    ioctl(fd, UI_SET_EVBIT_, EV_ABS_);
    ioctl(fd, UI_SET_KEYBIT_, BTN_TOUCH_);
    ioctl(fd, UI_SET_ABSBIT_, ABS_X_);
    ioctl(fd, UI_SET_ABSBIT_, ABS_Y_);
    ioctl(fd, UI_SET_ABSBIT_, ABS_MT_SLOT_);
    ioctl(fd, UI_SET_ABSBIT_, ABS_MT_TOUCH_MAJOR_);
    ioctl(fd, UI_SET_ABSBIT_, ABS_MT_POSITION_X_);
    ioctl(fd, UI_SET_ABSBIT_, ABS_MT_POSITION_Y_);
    ioctl(fd, UI_SET_ABSBIT_, ABS_MT_TRACKING_ID_);
    ioctl(fd, UI_SET_PROPBIT_, INPUT_PROP_DIRECT_);

    uinput_user_dev dev;
    memset(&dev, 0, sizeof(dev));
    for (usize i = 0; i < sizeof(dev.name) - 1 && DEVICE_NAME[i]; i++) {
        dev.name[i] = DEVICE_NAME[i];
    }
    dev.id.bustype = 0x18;
    dev.absmax[ABS_X_] = 143999;
    dev.absmax[ABS_Y_] = 319999;
    dev.absmax[ABS_MT_SLOT_] = 9;
    dev.absmax[ABS_MT_TOUCH_MAJOR_] = 255;
    dev.absmax[ABS_MT_POSITION_X_] = 143999;
    dev.absmax[ABS_MT_POSITION_Y_] = 319999;
    dev.absmax[ABS_MT_TRACKING_ID_] = 65535;
    if (write(fd, &dev, sizeof(dev)) != (ssize)sizeof(dev)) {
        __android_log_print(LOG_ERROR, "SailTap", "uinput setup failed");
        close(fd);
        return 1;
    }
    if (ioctl(fd, UI_DEV_CREATE_) < 0) {
        __android_log_print(LOG_ERROR, "SailTap", "uinput create failed");
        close(fd);
        return 1;
    }
    wait_ms(1200);
    __android_log_print(LOG_INFO, "SailTap", "device ready at %d,%d", x, y);

    mkfifo(FIFO_PATH, 0666);
    int fifo = open(FIFO_PATH, O_RDONLY_ | O_NONBLOCK_);
    if (fifo < 0) {
        __android_log_print(LOG_ERROR, "SailTap", "fifo open failed");
        ioctl(fd, UI_DEV_DESTROY_);
        close(fd);
        return 1;
    }
    int keep = open(FIFO_PATH, O_WRONLY_ | O_NONBLOCK_);

    while (!g_stop) {
        poll_fd p;
        p.fd = fifo;
        p.events = POLLIN_;
        p.revents = 0;
        poll(&p, 1, 100);
        if (p.revents & POLLIN_) {
            char buf[64];
            ssize n = read(fifo, buf, sizeof(buf));
            if (n > 0) {
                buf[n < (ssize)sizeof(buf) - 1 ? n : (ssize)sizeof(buf) - 1] = 0;
                int vals[5];
                int cnt = parse_nums(buf, vals, 5);
                if (cnt >= 4) {
                    swipe(fd, vals[0], vals[1], vals[2], vals[3], cnt >= 5 ? vals[4] : 300);
                } else if (cnt >= 2) {
                    tap(fd, vals[0], vals[1]);
                }
            }
        }
    }

    close(fifo);
    if (keep >= 0) close(keep);
    ioctl(fd, UI_DEV_DESTROY_);
    close(fd);
    __android_log_print(LOG_INFO, "SailTap", "stopped");
    return 0;
}
