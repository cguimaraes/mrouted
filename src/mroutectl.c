/*
 * Copyright (c) 2018-2019 Joachim Nilsson <troglobit@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "defs.h"
#include <getopt.h>
#include <poll.h>

struct cmd {
	char        *cmd;
	struct cmd  *ctx;
	int        (*cb)(char *arg);
	int         op;
};

static int detail = 0;
static int heading = 1;


static int do_connect(void)
{
	struct sockaddr_un sun;
	int sd;

	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (-1 == sd)
		goto error;

#ifdef HAVE_SOCKADDR_UN_SUN_LEN
	sun.sun_len = 0;	/* <- correct length is set by the OS */
#endif
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, _PATH_MROUTED_SOCK, sizeof(sun.sun_path));
	if (connect(sd, (struct sockaddr*)&sun, sizeof(sun)) == -1) {
		close(sd);
		goto error;
	}

	return sd;
error:
	if (errno == ENOENT)
		fprintf(stderr, "Cannot connect to mrouted, verify it has started.\n");
	else
		perror("Failed connecting to mrouted");

	return -1;
}

static int show_generic(int cmd, int detail)
{
	struct pollfd pfd;
	struct ipc msg = { 0 };
	int sd;
	
	sd = do_connect();
	if (-1 == sd)
		return -1;

	msg.cmd = cmd;
	msg.detail = detail;
	if (write(sd, &msg, sizeof(msg)) == -1) {
		close(sd);
		return -1;
	}

	pfd.fd = sd;
	pfd.events = POLLIN;
	while (poll(&pfd, 1, 2000) > 0) {
		ssize_t len;

		len = read(sd, &msg, sizeof(msg));
		if (len != sizeof(msg) || msg.cmd)
			break;

		fputs(msg.buf, stdout);
	}

	return close(sd);
}

static int usage(int rc)
{
	fprintf(stderr,
		"Usage: mroutectl [OPTIONS] [COMMAND]\n"
		"\n"
		"Options:\n"
		"  -d, --detail              Detailed output, where applicable\n"
		"  -h, --help                This help text\n"
		"\n"
		"Commands:\n"
		"  help                      This help text\n"
		"  kill                      Kill running daemon, like SIGTERM\n"
		"  restart                   Restart deamon and reload .conf file, like SIGHUP\n"
		"  version                   Show pimd version\n"
		"  show status               Show pimd status, default\n"
		"  show igmp groups          Show IGMP group memberships\n"
		"  show igmp interface       Show IGMP interface status\n"
		"  show pim interface        Show PIM interface table\n"
		"  show pim neighbor         Show PIM neighbor table\n"
		"  show pim routes           Show PIM routing table\n"
		"  show pim rp               Show PIM Rendezvous-Point (RP) set\n"
		"  show pim crp              Show PIM Candidate Rendezvous-Point (CRP) from BSR\n"
		"  show pim compat           Show PIM status, compat mode, previously `pimd -r`\n"
		);

	return rc;
}

static int help(char *arg)
{
	(void)arg;
	return usage(0);
}

static int version(char *arg)
{
	(void)arg;
	printf("v%s\n", PACKAGE_VERSION);

	return 0;
}

static int string_match(const char *a, const char *b)
{
   size_t min = MIN(strlen(a), strlen(b));

   return !strncasecmp(a, b, min);
}

static int cmd_parse(int argc, char *argv[], struct cmd *command)
{
	int i;

	for (i = 0; argc > 0 && command[i].cmd; i++) {
		if (!string_match(command[i].cmd, argv[0]))
			continue;

		if (command[i].ctx)
			return cmd_parse(argc - 1, &argv[1], command[i].ctx);

		if (command[i].cb) {
			char arg[80] = "";
			int j;

			for (j = 1; j < argc; j++) {
				if (j > 1)
					strlcat(arg, " ", sizeof(arg));
				strlcat(arg, argv[j], sizeof(arg));
			}

			return command[i].cb(arg);
		}

		return show_generic(command[i].op, detail);
	}

	return usage(1);
}

int main(int argc, char *argv[])
{
	struct option long_options[] = {
		{ "detail",     0, NULL, 'd' },
		{ "help",       0, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	struct cmd command[] = {
		{ "help",      NULL, help,         0                   },
		{ "kill",      NULL, NULL,         IPC_KILL_CMD        },
		{ "interface", NULL, NULL,         IPC_SHOW_IFACE_CMD  },
		{ "iface",     NULL, NULL,         IPC_SHOW_IFACE_CMD  },
		{ "igmp",      NULL, NULL,         IPC_SHOW_IGMP_CMD  },
		{ "status",    NULL, NULL,         IPC_SHOW_STATUS_CMD },
		{ "restart",   NULL, NULL,         IPC_RESTART_CMD     },
		{ "version",   NULL, version,      0                   },
		{ NULL }
	};
	int c;

	while ((c = getopt_long(argc, argv, "dh?v", long_options, NULL)) != EOF) {
		switch(c) {
		case 'd':
			detail = 1;
			break;

		case 'h':
		case '?':
			return usage(0);
		}
	}

	if (optind >= argc)
		return show_generic(IPC_SHOW_STATUS_CMD, detail);

	return cmd_parse(argc - optind, &argv[optind], command);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
