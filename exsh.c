
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/poll.h>

#include "rline.h"

static const char *prog_name;


static void hello_cmd(int argc, char *argv[])
{
    int i; 

    rline_message("hello.\n");

    for (i = 0; i < argc; i++)
	rline_message("%d: [%s]\n", i, argv[i]);

    rline_message("goodbye.\n");
}

static void prompt_cmd(int argc, char *argv[])
{
    if (argc > 0)
	rline_set_prompt(argv[0]);
}

static const struct rline_command ex_commands[] = {
    {"hello",	&hello_cmd, 		"\n\thello\n" },
    {"prompt",	&prompt_cmd, 		"\n\tprompt [string]\n" },
};

static void read_loop(int timeout, int check_stdin)
{
    struct pollfd pfd[2];
    int nfd = 0;
    int i;

    memset(pfd, 0, sizeof(pfd));
    if (check_stdin)
    {
	pfd[nfd].fd = STDIN_FILENO;
	pfd[nfd].events = POLLIN;
	++nfd;
    }

    int r = poll(pfd, nfd, timeout);

    if ((r < 0) && (errno != EINTR))
    {
	fprintf(stderr, "poll error: %s\n", strerror(errno));
	exit(1);
    }
    else if (r > 0)
    {
	for (i = 0; i < nfd; i++)
	{
	    if (pfd[i].revents & POLLIN)
	    {
		if (pfd[i].fd == STDIN_FILENO)
		{
		    rline_read_ch();
		}
	    }
	}
    }
}

static void main_loop(void)
{
    while (!please_exit)
	read_loop(-1, 1);
}

static void usage(void)
{
    fprintf(stderr, "%s: Usage: [<cmd ...>]\n", prog_name);
    exit(1);
}

int main(int argc, char *argv[])
{
    prog_name = argv[0];
    if (argc < 1) 
	usage();

    rline_setup("exsh", argc > 1 ? RLINE_BATCH : RLINE_INTERACTIVE);
    rline_register(ex_commands, ARRAY_SIZE(ex_commands));

    if (argc > 1)
    {
	--argc, ++argv;
	rline_exec(argc, argv);
    }
    else
    {
	main_loop();
    }
    rline_cleanup();
}

