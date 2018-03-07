#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/wireless.h>
#include <X11/Xlib.h>

#define UPDATE_INTERVAL 2
#define CLOCK_FORMAT    "\x01%a: %d : %b - %H:%M"
#define WIRED_DEVICE    "enp0s25"
#define WIRELESS_DEVICE "wlp4s0"
#define BATTERY_FULL    "/sys/class/power_supply/BAT0/energy_full"
#define BATTERY_NOW     "/sys/class/power_supply/BAT0/energy_now"
#define ON_AC           "/sys/class/power_supply/AC/online"
#define VOLUME          "/home/ok/.volume"

#define TOTAL_JIFFIES get_jiffies(7)
#define WORK_JIFFIES get_jiffies(3)






void get_time(char *buf, int bufsize)
{
	time_t tm;

	time(&tm);
	strftime(buf, bufsize, CLOCK_FORMAT, localtime(&tm));
}


char *
smprintf(char *fmt, ...)
{
    va_list fmtargs;
    char *buf = NULL;

    va_start(fmtargs, fmt);
    if (vasprintf(&buf, fmt, fmtargs) == -1){
        fprintf(stderr, "malloc vasprintf\n");
        exit(1);
    }
    va_end(fmtargs);

    return buf;
} 

char*
runcmd(char* cmd) {
      FILE* fp = popen(cmd, "r");
        if (fp == NULL) return NULL;
          char ln[50];
            fgets(ln, sizeof(ln)-1, fp);
              pclose(fp);
                ln[strlen(ln)-1]='\0';
                  return smprintf("%s", ln);
}
 

void get_vol(char *buf, int bufsize)
{
    float volume ;
    sscanf(runcmd("amixer -D pulse get Master | grep -e 'Front Left:' | grep -o '[0-9%]*%'"), "%f%%", &volume);

    snprintf(buf, bufsize, "%cVolume: %.2f ", '\x01', volume);
}    

void get_mem(char *buf, int bufsize)
{
	FILE *fp;
	float total, free, buffers, cached, available;

	fp = fopen("/proc/meminfo", "r");
	if(fp != NULL) {
		fscanf(fp, "MemTotal: %f kB\nMemFree: %f kB\nMemAvailable:%f kB\nBuffers: %f kB\nCached: %f kB\n",
			&total, &free, &available, &buffers, &cached);
		snprintf(buf, bufsize, "%cMem: %.2f", '\x01', (total - free - buffers - cached) / total);
		fclose(fp);
	}
}

void get_bat(char *buf, int bufsize)
{
	FILE *f1p, *f2p, *f3p;
	float now, full;
	int ac;

	f1p = fopen(BATTERY_NOW, "r");
	f2p = fopen(BATTERY_FULL, "r");
	f3p = fopen(ON_AC, "r");
	if(f1p == NULL || f2p == NULL || f3p == NULL)
		snprintf(buf, bufsize, "%cBat\x02N/A", '\x01');
	else {
		fscanf(f1p, "%f", &now);
		fscanf(f2p, "%f", &full);
		fscanf(f3p, "%d", &ac);
		if(ac)
			snprintf(buf, bufsize, "%cAc: %.2f", '\x01', now / full);
		else
			snprintf(buf, bufsize, "%cBat: %.2f", '\x01', now / full);
	}
	if(f1p != NULL)
		fclose(f1p);
	if(f2p != NULL)
		fclose(f2p);
	if(f3p != NULL)
		fclose(f3p);
}

long get_jiffies(int n)
{
	FILE *fp;
	int i;
	long j, jiffies;

	fp = fopen("/proc/stat", "r");
	if(fp == NULL)
		return 0;
	fscanf(fp, "cpu %ld", &jiffies);
	for(i = 0; i < n - 1; i++) {
		fscanf(fp, "%ld", &j);
		jiffies += j;
	}
	fclose(fp);
	return jiffies;
}

void get_cpu(char *buf, int bufsize, long total_jiffies, long work_jiffies)
{
	long work_over_period, total_over_period;
	float cpu;
	
	work_over_period = WORK_JIFFIES - work_jiffies;
	total_over_period = TOTAL_JIFFIES - total_jiffies;
	if(total_over_period > 0)
		cpu = (float)work_over_period / (float)total_over_period;
	else
		cpu = 0.;
	snprintf(buf, bufsize, "%cCpu: %.2f", '\x01', cpu);
}

int is_up(char *device)
{
	FILE *fp;
	char fn[32], state[5];

	snprintf(fn, sizeof(fn), "/sys/class/net/%s/operstate", device);
	fp = fopen(fn, "r");
	if(fp == NULL)
		return 0;
	fscanf(fp, "%s", state);
	fclose(fp);
	if(strcmp(state, "up") == 0)
		return 1;
	return 0;
}

void get_net(char *buf, int bufsize)
{
	int sockfd, qual = 0;
	char ssid[IW_ESSID_MAX_SIZE + 1] = "N/A";
	struct iwreq wreq;
	struct iw_statistics stats;
	
	if(is_up(WIRED_DEVICE))
		snprintf(buf, bufsize, "%cEth\x02On", '\x01');
	else if(is_up(WIRELESS_DEVICE)) {
		memset(&wreq, 0, sizeof(struct iwreq));
		sprintf(wreq.ifr_name, WIRELESS_DEVICE);
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
		if(sockfd != -1) {
			wreq.u.essid.pointer = ssid;
			wreq.u.essid.length = sizeof(ssid);
			if(!ioctl(sockfd, SIOCGIWESSID, &wreq))
				ssid[0] = toupper(ssid[0]);

			wreq.u.data.pointer = (void*) &stats;
			wreq.u.data.length = sizeof(struct iw_statistics);
			wreq.u.data.flags = 1;
			if(!ioctl(sockfd, SIOCGIWSTATS, &wreq))
				qual = stats.qual.qual;
		}
		snprintf(buf, bufsize, "%c%s: %d", '\x01', ssid, qual);
		close(sockfd);
	} else{
		snprintf(buf, bufsize, "%cEth\x02No", '\x01');
        }
}


int main(void)
{
	Display *dpy;
	Window root;
	char status[512], time[32], net[64], vol[16], bat[16], cpu[16], mem[16];
	long total_jiffies, work_jiffies;

	dpy = XOpenDisplay(NULL);
	if(dpy == NULL) {
		fprintf(stderr, "error: could not open display\n");
		return 1;
	}
	root = XRootWindow(dpy, DefaultScreen(dpy));

	total_jiffies = TOTAL_JIFFIES;
	work_jiffies = WORK_JIFFIES;
	
	while(1) {
		get_cpu(cpu, sizeof(cpu), total_jiffies, work_jiffies);
		get_mem(mem, sizeof(mem));
		get_bat(bat, sizeof(bat));
		get_net(net, sizeof(net));
		get_time(time, sizeof(time));
		get_vol(vol, sizeof(vol));

		snprintf(status, sizeof(status), "%s %s %s %s  %s %s ", vol, cpu, mem, bat, net, time);

		total_jiffies = TOTAL_JIFFIES;
		work_jiffies = WORK_JIFFIES;

		XStoreName(dpy, root, status);
		XFlush(dpy);
		sleep(UPDATE_INTERVAL);
	}

	XCloseDisplay(dpy);
	return 0;
}

