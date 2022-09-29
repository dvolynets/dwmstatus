/*
 * Copy me if you can.
 * by 20h
 */

#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>

char *tzargentina = "America/Buenos_Aires";
char *tzutc = "UTC";
char *tzberlin = "Europe/Berlin";

static Display *dpy;
static Window win;
static GC gc;

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		return smprintf("");

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		return smprintf("");
	}

	return smprintf("%s", buf);
}

void drawtext(const char* text, const char* fonttype, int x, int y, int screen)
{
	Font font = XLoadFont(dpy, fonttype);
	XSetFont(dpy, gc, font);
	XSetForeground(dpy, gc, BlackPixel(dpy,screen));
	XDrawString(dpy, win, gc, x, y, text, strlen(text));
	XUnloadFont(dpy, font);
}

void simplenotification(int batterylevel, int charging)
{
	if (win) {
		XDestroyWindow(dpy, win);
		win = 0;
		return;
	}

	if (charging || batterylevel > 30) {
		return;
	}

	int screen = DefaultScreen(dpy);
	int dpywidth = DisplayWidth(dpy, screen);
	int dpyheight = DisplayHeight(dpy, screen);

	int offset = 25;
	int winwidth = (dpywidth / 4);
	int winheight = (dpyheight / 9);

	int x = dpywidth - winwidth - offset;
	int y = offset;

	win = XCreateSimpleWindow(
		dpy, RootWindow(dpy, screen),
		x, y, winwidth, winheight, 2,
		BlackPixel(dpy, screen),
		0xbbbbbb
	);

	XSetTransientForHint(dpy, win, RootWindow(dpy, screen));
	XMapWindow(dpy, win);
	XSelectInput(dpy, win, ExposureMask);

	gc = XCreateGC(dpy,win,0,NULL);
	XEvent event;

	while(1) {
		XNextEvent(dpy, &event);
		if (event.xany.window == win && event.type == Expose) {
			break;
		}
	}

	drawtext("LOW BATTERY LEVEL, TIME TO PLUG IN CHARGE DEVICE", "fixed", offset*3, offset*2, screen);
	drawtext(">>>>", "cursor", offset*7, offset*3, screen);

	XFreeGC(dpy, gc);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0)
		return smprintf("");

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

char *
readfile(char *base, char *file)
{
	char *path, line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	free(path);
	if (fd == NULL)
		return NULL;

	if (fgets(line, sizeof(line)-1, fd) == NULL)
		return NULL;
	fclose(fd);

	return smprintf("%s", line);
}

char *
getbattery(char *base, int* batterylevel, int* charging)
{
	char *co, status;
	int descap, remcap;

	descap = -1;
	remcap = -1;

	co = readfile(base, "present");
	if (co == NULL)
		return smprintf("");
	if (co[0] != '1') {
		free(co);
		return smprintf("not present");
	}
	free(co);

	co = readfile(base, "charge_full_design");
	if (co == NULL) {
		co = readfile(base, "energy_full_design");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &descap);
	free(co);

	co = readfile(base, "charge_now");
	if (co == NULL) {
		co = readfile(base, "energy_now");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &remcap);
	free(co);

	co = readfile(base, "status");
	if (!strncmp(co, "Discharging", 11)) {
		status = '-';
		*charging = 0;
	} else if(!strncmp(co, "Charging", 8)) {
		status = '+';
		*charging = 1;
	} else {
		status = '?';
		*charging = 0;
	}

	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

	*batterylevel = ((float)remcap / (float)descap) * 100;
	return smprintf("%.0f%%%c", ((float)remcap / (float)descap) * 100, status);
}

char *
gettemperature(char *base, char *sensor)
{
	char *co;

	co = readfile(base, sensor);
	if (co == NULL)
		return smprintf("");
	return smprintf("%02.0f°C", atof(co) / 1000);
}

char *
execscript(char *cmd)
{
	FILE *fp;
	char retval[1025], *rv;

	memset(retval, 0, sizeof(retval));

	fp = popen(cmd, "r");
	if (fp == NULL)
		return smprintf("");

	rv = fgets(retval, sizeof(retval), fp);
	pclose(fp);
	if (rv == NULL)
		return smprintf("");
	retval[strlen(retval)-1] = '\0';

	return smprintf("%s", retval);
}

int
main(void)
{
	char *status;
	char *avgs;
	char *bat;
	char *tmar;
	char *tmutc;
	char *tmbln;
	char *t0;
	char *t1;
	char *kbmap;

	int batterylevel, charging;

	win = 0;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(60)) {
		avgs = loadavg();
		bat = getbattery("/sys/class/power_supply/BAT1", &batterylevel, &charging);
		tmar = mktimes("%H:%M", tzargentina);
		tmutc = mktimes("%H:%M", tzutc);
		tmbln = mktimes("KW %W %a %d %b %H:%M %Z %Y", tzberlin);
		kbmap = execscript("setxkbmap -query | grep layout | cut -d':' -f 2- | tr -d ' '");
		t0 = gettemperature("/sys/devices/virtual/thermal/thermal_zone0", "temp");
		t1 = gettemperature("/sys/devices/virtual/thermal/thermal_zone1", "temp");

		status = smprintf("K:%s T:%s|%s L:%s B:%s A:%s U:%s %s",
				kbmap, t0, t1, avgs, bat, tmar, tmutc,
				tmbln);

		simplenotification(batterylevel, charging);
		setstatus(status);

		free(kbmap);
		free(t0);
		free(t1);
		free(avgs);
		free(bat);
		free(tmar);
		free(tmutc);
		free(tmbln);
		free(status);
	}

	if (win) {
		XDestroyWindow(dpy, win);
	}

	XCloseDisplay(dpy);

	return 0;
}

