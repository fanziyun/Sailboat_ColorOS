#ifndef Sail_FrameBoost_ABI_H
#define Sail_FrameBoost_ABI_H

typedef __INT32_TYPE__ i32;
typedef __UINT32_TYPE__ u32;
typedef __UINT16_TYPE__ u16;
typedef __INT64_TYPE__ i64;
typedef __SIZE_TYPE__ usize;
typedef __PTRDIFF_TYPE__ ssize;

typedef struct { i64 seconds, nanoseconds; } time_spec;
typedef struct { i64 seconds, microseconds; } time_value;
typedef struct { time_value time; u16 type, code; i32 value; } input_event;
_Static_assert(sizeof(input_event) == 24, "input_event ABI");

typedef struct { int fd; short events, revents; } poll_fd;

extern int open(const char *, int, ...);
extern ssize read(int, void *, usize);
extern ssize write(int, const void *, usize);
extern int close(int);
extern int pipe(int [2]);
extern int poll(poll_fd *, unsigned long, int);
extern int ioctl(int, unsigned long, ...);
extern int clock_gettime(int, time_spec *);
extern int fork(void);
extern int execv(const char *, char *const []);
extern int dup2(int, int);
extern void _exit(int) __attribute__((noreturn));
extern int waitpid(int, int *, int);
extern int kill(int, int);
typedef void (*signal_handler)(int);
extern signal_handler signal(int, signal_handler);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, usize);
extern usize strlen(const char *);
extern int snprintf(char *, usize, const char *, ...);
extern void *memset(void *, int, usize);
extern int __android_log_print(int, const char *, const char *, ...);
extern void __libc_init(void *, void (*)(void), int (*)(int, char **, char **), const void *) __attribute__((noreturn));

#endif
