#include "android_abi.h"

#define EV_KEY_ 1
#define EV_ABS_ 3
#define ABS_MT_TRACKING_ID_ 57
#define BTN_TOUCH_ 330

#define EVIOCGNAME_256 0x81004506u

#define KGSL_MIN_PWRLEVEL "/sys/class/kgsl/kgsl-3d0/min_pwrlevel"
#ifndef FB_TOUCH_PREFIX
#define FB_TOUCH_PREFIX "goodix_ts"
#endif
#define LOGCAT_PATH "/system/bin/logcat"
#define LAUNCHER_PREFIX "com.android.launcher"

#define O_RDONLY_ 0
#define O_WRONLY_ 1
#define POLLIN_ 1
#define CLOCK_MONOTONIC_ 1

#define LOG_INFO 4
#define LOG_ERROR 6

#ifndef FB_HOLD_MS
#define FB_HOLD_MS 1500
#endif
#ifndef FB_LAUNCH_GRACE_MS
#define FB_LAUNCH_GRACE_MS 800
#endif
#ifndef FB_HOLD_CAP_MS
#define FB_HOLD_CAP_MS 8000
#endif
#ifndef FB_TICK_MS
#define FB_TICK_MS 50
#endif
#ifndef FB_FLOOR_HIGH
#define FB_FLOOR_HIGH "0\n"
#endif

static volatile int g_stop;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static void log_line(int prio, const char *msg) {
    __android_log_print(prio, "SailFrameBoost", "%s", msg);
}

static long long now_ms(void) {
    time_spec ts;
    clock_gettime(CLOCK_MONOTONIC_, &ts);
    return (long long)ts.seconds * 1000LL + (long long)(ts.nanoseconds / 1000000);
}

static int read_int_file(const char *path) {
    char buf[32];
    int fd = open(path, O_RDONLY_);
    if (fd < 0) return -1;
    ssize n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    int value = 0;
    int seen = 0;
    for (ssize i = 0; i < n; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            if (seen) break;
            continue;
        }
        value = value * 10 + (buf[i] - '0');
        seen = 1;
    }
    return seen ? value : -1;
}

static int write_floor_str(const char *value) {
    int fd = open(KGSL_MIN_PWRLEVEL, O_WRONLY_);
    if (fd < 0) return -1;
    usize len = strlen(value);
    ssize written = write(fd, value, len);
    close(fd);
    return written == (ssize)len ? 0 : -1;
}

static int write_floor_int(int level) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d\n", level);
    return write_floor_str(buf);
}

static int open_touch(void) {
    for (int i = 0; i < 32; i++) {
        char path[40];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY_);
        if (fd < 0) continue;
        char name[256];
        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME_256, name) >= 0 &&
            strncmp(name, FB_TOUCH_PREFIX, sizeof(FB_TOUCH_PREFIX) - 1) == 0) {
            return fd;
        }
        close(fd);
    }
    return -1;
}

static int spawn_logcat(int *pid_out) {
    int p[2];
    if (pipe(p) != 0) return -1;
    int pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        if (dup2(p[1], 1) < 0) _exit(1);
        close(p[1]);
        char *argv[10];
        argv[0] = (char *)LOGCAT_PATH;
        argv[1] = (char *)"-b";
        argv[2] = (char *)"events";
        argv[3] = (char *)"-s";
        argv[4] = (char *)"wm_on_resume_called:I";
        argv[5] = (char *)"wm_on_paused_called:I";
        argv[6] = (char *)"wm_on_start_called:I";
        argv[7] = (char *)"-T";
        argv[8] = (char *)"20";
        argv[9] = 0;
        execv(LOGCAT_PATH, argv);
        _exit(1);
    }
    close(p[1]);
    *pid_out = pid;
    return p[0];
}

static int contains(const char *hay, const char *needle) {
    usize n = strlen(needle);
    for (usize i = 0; hay[i]; i++) {
        if (strncmp(hay + i, needle, n) == 0) return 1;
    }
    return 0;
}

static int query_launcher_fg(void) {
    static const char cmd[] =
        "/system/bin/dumpsys window displays 2>/dev/null | /system/bin/grep -m1 mFocusedApp";
    int p[2];
    if (pipe(p) != 0) return -1;
    int pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        if (dup2(p[1], 1) < 0) _exit(1);
        close(p[1]);
        char *argv[4];
        argv[0] = (char *)"/system/bin/sh";
        argv[1] = (char *)"-c";
        argv[2] = (char *)cmd;
        argv[3] = 0;
        execv("/system/bin/sh", argv);
        _exit(1);
    }
    close(p[1]);
    char buf[1024];
    usize len = 0;
    for (;;) {
        char chunk[128];
        ssize n = read(p[0], chunk, sizeof(chunk));
        if (n <= 0) break;
        for (ssize i = 0; i < n && len + 1 < sizeof(buf); i++) buf[len++] = chunk[i];
    }
    close(p[0]);
    if (pid > 0) kill(pid, 15);
    buf[len] = 0;
    if (len == 0) return -1;
    return contains(buf, LAUNCHER_PREFIX) ? 1 : 0;
}
#define EV_NONE 0
#define EV_LAUNCHER_RESUME 1
#define EV_LAUNCHER_PAUSE 2
#define EV_OTHER_RESUME 3
#define EV_OTHER_START 4

static const char *component_after(const char *at) {
    const char *c = at;
    while (*c && *c != ',') c++;
    if (*c == ',') c++;
    return c;
}

static int parse_wm_line(const char *line, const char **comp_out) {
    static const char rk[] = "wm_on_resume_called: [";
    static const char pk[] = "wm_on_paused_called: [";
    static const char sk[] = "wm_on_start_called: [";
    const usize lprefix = sizeof(LAUNCHER_PREFIX) - 1;
    for (usize i = 0; line[i]; i++) {
        if (strncmp(line + i, rk, sizeof(rk) - 1) == 0) {
            const char *c = component_after(line + i + sizeof(rk) - 1);
            *comp_out = c;
            return strncmp(c, LAUNCHER_PREFIX, lprefix) == 0 ? EV_LAUNCHER_RESUME : EV_OTHER_RESUME;
        }
        if (strncmp(line + i, pk, sizeof(pk) - 1) == 0) {
            const char *c = component_after(line + i + sizeof(pk) - 1);
            *comp_out = c;
            return strncmp(c, LAUNCHER_PREFIX, lprefix) == 0 ? EV_LAUNCHER_PAUSE : EV_NONE;
        }
        if (strncmp(line + i, sk, sizeof(sk) - 1) == 0) {
            const char *c = component_after(line + i + sizeof(sk) - 1);
            *comp_out = c;
            return strncmp(c, LAUNCHER_PREFIX, lprefix) == 0 ? EV_NONE : EV_OTHER_START;
        }
    }
    return EV_NONE;
}

static usize pkg_len(const char *comp) {
    usize i = 0;
    while (comp[i] && comp[i] != '/') i++;
    return i;
}

static int comp_matches(const char *comp, const char *pkg) {
    usize n = strlen(pkg);
    if (n == 0) return 0;
    if (strncmp(comp, pkg, n) == 0) return 1;
    usize m = pkg_len(comp);
    return m > 0 && strncmp(pkg, comp, m) == 0;
}

int main(int, char **, char **);

__attribute__((noreturn, no_stack_protector)) void Sail_FrameBoost_start(void *raw_arguments) {
    static const unsigned char zero_structors[128] = {0};
    __libc_init(raw_arguments, 0, main, zero_structors);
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    signal(15, on_signal);
    signal(2, on_signal);
    signal(1, on_signal);

    int restore_level = read_int_file(KGSL_MIN_PWRLEVEL);
    if (restore_level < 0) {
        log_line(LOG_ERROR, "min_pwrlevel unreadable");
        return 1;
    }

    int touch_fd = open_touch();
    if (touch_fd < 0) {
        log_line(LOG_ERROR, "goodix touch device not found");
        return 1;
    }

    int log_pid = -1;
    int log_fd = spawn_logcat(&log_pid);
    if (log_fd < 0) {
        log_line(LOG_ERROR, "logcat spawn failed");
        close(touch_fd);
        return 1;
    }
    log_line(LOG_INFO, "started");

    int fg_launcher = query_launcher_fg();
#ifdef FB_DEBUG
    {
        char dbg[32];
        snprintf(dbg, sizeof(dbg), "initial fg=%d", fg_launcher);
        log_line(LOG_INFO, dbg);
    }
#endif
    int boosting = 0;
    long long boost_start = 0;
    long long boost_until = 0;
    int launch_watch = 0;
    int launch_in_flight = 0;
    char launch_pkg[64];
    launch_pkg[0] = 0;
    char line[512];
    usize line_len = 0;
    int log_dead = 0;

    while (!g_stop) {
        poll_fd fds[2];
        fds[0].fd = touch_fd;
        fds[0].events = POLLIN_;
        fds[0].revents = 0;
        fds[1].fd = log_dead ? -1 : log_fd;
        fds[1].events = POLLIN_;
        fds[1].revents = 0;

        poll(fds, 2, FB_TICK_MS);
        long long now = now_ms();

        if (!log_dead && (fds[1].revents & POLLIN_)) {
            char chunk[256];
            ssize n = read(log_fd, chunk, sizeof(chunk));
            if (n <= 0) {
                log_dead = 1;
            } else {
                for (ssize i = 0; i < n; i++) {
                    char ch = chunk[i];
                    if (ch == '\n') {
                        line[line_len] = '\0';
                        const char *comp = 0;
                        int ev = parse_wm_line(line, &comp);
                        if (ev == EV_LAUNCHER_RESUME) {
                            fg_launcher = 1;
#ifdef FB_DEBUG
                            log_line(LOG_INFO, "fg launcher");
#endif
                        } else if (ev == EV_LAUNCHER_PAUSE) {
                            fg_launcher = 0;
#ifdef FB_DEBUG
                            log_line(LOG_INFO, "fg left launcher");
#endif
                        } else if (ev == EV_OTHER_START) {
                            if (launch_watch) {
                                usize pl = pkg_len(comp);
                                if (pl >= sizeof(launch_pkg)) pl = sizeof(launch_pkg) - 1;
                                for (usize k = 0; k < pl; k++) launch_pkg[k] = comp[k];
                                launch_pkg[pl] = 0;
                                launch_in_flight = 1;
#ifdef FB_DEBUG
                                log_line(LOG_INFO, "launch start");
#endif
                            }
                        } else if (ev == EV_OTHER_RESUME) {
                            if (launch_in_flight && comp_matches(comp, launch_pkg)) {
                                launch_in_flight = 0;
                                launch_watch = 0;
                                if (boosting) {
                                    long long ext = now + FB_LAUNCH_GRACE_MS;
                                    long long cap = boost_start + FB_HOLD_CAP_MS;
                                    if (ext > cap) ext = cap;
                                    if (ext > boost_until) boost_until = ext;
                                }
#ifdef FB_DEBUG
                                log_line(LOG_INFO, "launch resumed");
#endif
                            }
                        }
                        line_len = 0;
                    } else if (line_len + 1 < sizeof(line)) {
                        line[line_len++] = ch;
                    } else {
                        line_len = 0;
                    }
                }
            }
        }

        if (fds[0].revents & POLLIN_) {
            input_event events[16];
            ssize n = read(touch_fd, events, sizeof(events));
            if (n > 0) {
                ssize count = n / (ssize)sizeof(input_event);
                int down = 0;
                for (ssize i = 0; i < count; i++) {
                    if (events[i].type == EV_ABS_ &&
                        events[i].code == ABS_MT_TRACKING_ID_ &&
                        events[i].value >= 0) {
                        down = 1;
                    }
                    if (events[i].type == EV_KEY_ &&
                        events[i].code == BTN_TOUCH_ &&
                        events[i].value == 1) {
                        down = 1;
                    }
                }
                if (down) {
#ifdef FB_NO_FILTER
                    int allow = 1;
#else
                    int allow = fg_launcher == 1;
#endif
#ifdef FB_DEBUG
                    {
                        char dbg[48];
                        snprintf(dbg, sizeof(dbg), "down allow=%d fg=%d", allow, fg_launcher);
                        log_line(LOG_INFO, dbg);
                    }
#endif
                    if (allow) {
                        if (!boosting) {
                            if (write_floor_str(FB_FLOOR_HIGH) == 0) {
                                boosting = 1;
                                boost_start = now;
                            }
                        }
                        if (boosting) {
                            boost_until = now + FB_HOLD_MS;
                            launch_watch = 1;
                            launch_in_flight = 0;
                        }
                    }
                }
            }
        }

        if (boosting) {
            long long cap = boost_start + FB_HOLD_CAP_MS;
            int hold_launch = launch_in_flight && now < cap;
            if (!hold_launch && now >= boost_until) {
                if (write_floor_int(restore_level) == 0) {
                    boosting = 0;
                    launch_watch = 0;
                    launch_in_flight = 0;
                }
            }
        }
    }

    if (boosting) write_floor_int(restore_level);
    close(touch_fd);
    if (log_fd >= 0) close(log_fd);
    if (log_pid > 0) kill(log_pid, 15);
    log_line(LOG_INFO, "stopped");
    return 0;
}
