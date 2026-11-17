#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void emit(int fd, int type, int code, int value)
{
	struct input_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = (unsigned short)type;
	ev.code = (unsigned short)code;
	ev.value = value;
	if (write(fd, &ev, sizeof(ev)) < 0)
		perror("write");
}

int main(int argc, char **argv)
{
	struct uinput_user_dev dev;
	int x = argc > 1 ? atoi(argv[1]) : 72000;
	int y = argc > 2 ? atoi(argv[2]) : 160000;
	int ready_ms = argc > 3 ? atoi(argv[3]) : 3000;
	int fd;

	fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("open /dev/uinput");
		return 1;
	}

	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
	ioctl(fd, UI_SET_ABSBIT, ABS_X);
	ioctl(fd, UI_SET_ABSBIT, ABS_Y);
	ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
	ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
	ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
	ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
	ioctl(fd, UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);
	ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

	memset(&dev, 0, sizeof(dev));
	snprintf(dev.name, sizeof(dev.name), "sailboat_tap");
	dev.id.bustype = BUS_VIRTUAL;
	dev.absmax[ABS_X] = 143999;
	dev.absmax[ABS_Y] = 319999;
	dev.absmax[ABS_MT_SLOT] = 9;
	dev.absmax[ABS_MT_TRACKING_ID] = 65535;
	dev.absmax[ABS_MT_POSITION_X] = 143999;
	dev.absmax[ABS_MT_POSITION_Y] = 319999;
	dev.absmax[ABS_MT_TOUCH_MAJOR] = 255;

	if (write(fd, &dev, sizeof(dev)) != (ssize_t)sizeof(dev)) {
		perror("write uinput_user_dev");
		close(fd);
		return 1;
	}

	if (ioctl(fd, UI_DEV_CREATE) < 0) {
		perror("UI_DEV_CREATE");
		close(fd);
		return 1;
	}

	usleep((useconds_t)ready_ms * 1000);

	emit(fd, EV_ABS, ABS_MT_SLOT, 0);
	emit(fd, EV_ABS, ABS_MT_TRACKING_ID, 1);
	emit(fd, EV_ABS, ABS_X, x);
	emit(fd, EV_ABS, ABS_Y, y);
	emit(fd, EV_ABS, ABS_MT_POSITION_X, x);
	emit(fd, EV_ABS, ABS_MT_POSITION_Y, y);
	emit(fd, EV_ABS, ABS_MT_TOUCH_MAJOR, 40);
	emit(fd, EV_KEY, BTN_TOUCH, 1);
	emit(fd, EV_SYN, SYN_REPORT, 0);
	usleep(60000);
	emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
	emit(fd, EV_KEY, BTN_TOUCH, 0);
	emit(fd, EV_SYN, SYN_REPORT, 0);
	usleep(200000);

	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	printf("tap %d %d\n", x, y);
	return 0;
}
