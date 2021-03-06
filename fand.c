/*-
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <tobias.rehbein@web.de> wrote this file. As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you
 * think this stuff is worth it, you can buy me a beer in return.
 *                                                              Tobias Rehbein
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <libutil.h>

#define SCTL_CPUS	"kern.smp.cpus"
#define SCTL_FAN	"dev.acpi_ibm.0.fan"
#define SCTL_LEVEL	"dev.acpi_ibm.0.fan_level"
#define SCTL_LEVEL_LEN	4
#define SCTL_TEMP	"dev.cpu.%d.temperature"
#define SCTL_TEMP_LEN	4

#define SLEEP		500000

#define C2K(c)		(((c) * 10) + 2732 - 5)
#define K2C(k)		(((k) - 2732 + 5) / 10)
#define ASIZE(a)	(sizeof(a)/sizeof(a[0]))

struct fanctl {
	int threshold;
	int level;
};

/**
 * Level	Speed
 * 1		~1950
 * 2		~3600
 * 3		~3800
 * 4		-> 3
 * 5		~4000
 * 6		-> 5
 * 7		~4500
 */
struct fanctl fanctls[] = {
	{ C2K( 0), 0 },
	{ C2K(40), 1 },
	{ C2K(45), 2 },
	{ C2K(50), 3 },
	{ C2K(55), 5 },
	{ C2K(60), 7 },
};

static void handle_signal(int sig);
static void cleanup(void);
static void usage(void);

static bool nflag, vflag, dflag;
static char *pidfile;
struct pidfh *pfh;

int
main(int argc, char **argv)
{
	int opt;
	pidfile = NULL;
	nflag = vflag = dflag = false;
	while ((opt = getopt(argc, argv, "nvdp:")) != -1) {
		switch (opt) {
		case 'n':
			nflag = true;
			break;
		case 'v':
			vflag = true;
			break;
		case 'd':
			dflag = true;
			break;
		case 'p':
			pidfile = strdup(optarg);
			break;
		case 'h':
		default:
			usage();
		}
	}

	if (!dflag) {
		if (pidfile != NULL) {
			pid_t other;
			pfh = pidfile_open(pidfile, 0600, &other);
			if (pfh == NULL) {
				if (errno == EEXIST) {
					fprintf(stderr,
					    "%s: daemon already running, pid: "
					    "%jd\n", getprogname(),
					    (intmax_t)other);
					exit(EXIT_FAILURE);
				}
				fprintf(stderr,
				    "%s: cannot open or create pidfile:%s\n",
				    getprogname(), strerror(errno));
			}
		}

		if (daemon(0, 0) == -1) {
			fprintf(stderr, "%s: cannot daemonize: %s\n",
			    getprogname(), strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (pidfile != NULL)
			pidfile_write(pfh);
	}

	int cpus;
	if (sysctlbyname(SCTL_CPUS, &cpus,
	    &(size_t){ sizeof(cpus) }, NULL, 0) == -1)
		err(EXIT_FAILURE, "could not get number of cpus");

	int temp_mibs[cpus][SCTL_TEMP_LEN];
	for (int i = 0; i < cpus; i++) {
		char *mib_rep;
		if (asprintf(&mib_rep, SCTL_TEMP, i) == -1)
			err(EXIT_FAILURE, "could not generate sysctl name");

		if (sysctlnametomib(mib_rep, temp_mibs[i],
		    &(size_t){ SCTL_TEMP_LEN }) == -1)
			err(EXIT_FAILURE, "could not find MIB for \"%s\"",
			    mib_rep);

		free(mib_rep);
	}

	int level_mib[SCTL_LEVEL_LEN];
	if (sysctlnametomib(SCTL_LEVEL, level_mib,
	    &(size_t){ sizeof(level_mib) }))
		err(EXIT_FAILURE, "could not find MIB for \"%s\"", SCTL_LEVEL);

	atexit(cleanup);
	signal(SIGINT, handle_signal);
	signal(SIGHUP, handle_signal);
	signal(SIGTERM, handle_signal);

	if (!nflag) {
		if (sysctlbyname(SCTL_FAN, NULL, NULL,
		    &(int){ 0 }, sizeof(int)) == -1)
			err(EXIT_FAILURE, "could not take over fan control");

		if (vflag)
			fprintf(stderr, "%s: took over fan control\n",
			    getprogname());
	}

	int oldlevel = 0;
	for (;;) {
		int maxtemp = 0;
		for (int i = 0; i < cpus; i++) {
			int temp;
			if (sysctl(temp_mibs[i], (int){ SCTL_TEMP_LEN },
			    &temp, &(size_t){ sizeof(temp) }, NULL, 0) == -1)
				err(EXIT_FAILURE, 
				    "could not get temperature for cpu %d", i);

			if (temp > maxtemp)
				maxtemp = temp;
		}

		int newlevel = 0;
		for (int i = ASIZE(fanctls) - 1; i >= 0; i--) {
			newlevel = fanctls[i].level;
			if (maxtemp > fanctls[i].threshold)
				break;
		}

		if (oldlevel != newlevel) {
			if (vflag)
				fprintf(stderr, "%s: temp %d, %d -> %d\n",
				    getprogname(), K2C(maxtemp),
				    oldlevel, newlevel);

			if (sysctl(level_mib, SCTL_LEVEL_LEN, NULL, NULL,
			    &newlevel, sizeof(int)) == -1)
				err(EXIT_FAILURE,
				    "could not set fan level: %d -> %d",
				    oldlevel, newlevel);
		}

		oldlevel = newlevel;

		usleep(SLEEP);
	}

	exit(EXIT_SUCCESS);
}

static void
handle_signal(int sig __unused) 
{
	exit(EXIT_SUCCESS);
}

static void
cleanup(void)
{
	if (!nflag) {
		if (sysctlbyname(SCTL_FAN, NULL, NULL, &(int){ 1 },
		    sizeof(int)) == -1)
			err(EXIT_FAILURE, "could not hand over fan control");

		if (vflag)
			fprintf(stderr, "%s: handed over fan control\n",
			    getprogname());
	}

	if (!dflag && pidfile != NULL)
		pidfile_remove(pfh);

	free(pidfile);
}

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-vnd] [-p pidfile]\n", getprogname());
	fprintf(stderr, "       %s -h\n", getprogname());

	exit(EXIT_FAILURE);
}
