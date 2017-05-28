#include <poll.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/param.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "rline.h"

struct arglist
{
    int count;
    char **argv;
    int max;
};

static struct rline_table command_table;

static bool rline_ready = false;
static char history_fname[MAXPATHLEN];

bool please_exit;

/* forward decl. */
static void parse_command(int use_history, char *line);
static bool split(char *line, struct arglist *ar);

static void dot_home_relative(const char *prefix, const char *suffix, char *result, size_t len)
{
    char *home = getenv("HOME");
    if (home == NULL)
        home = ".";
    snprintf(result, len, "%s/.%s-%s", home, prefix, suffix);
}

static int compare_command(const void *a, const void *b)
{
    const struct rline_command * const *aa = a;
    const struct rline_command * const *bb = b;
    return strcmp((*aa)->cmd, (*bb)->cmd);
}


/* ensure there is space for n new commands in the table */
static void table_ensure(struct rline_table *t, size_t n)
{
    if ((t->ncommands + n) >= t->max) {
        t->max += n;
        t->commands = realloc(t->commands, sizeof(struct rline_command *) * t->max);
        if (t->commands == NULL) {
            fprintf(stderr, "memory allocation error.");
            exit(1);
        }
    }
}

static void table_push(struct rline_table *t, const struct rline_command *c, size_t n)
{
    table_ensure(t, n);
    while (n--) {
        t->commands[t->ncommands++] = c++;
    }
    qsort(t->commands, t->ncommands, sizeof(struct rline_command *), compare_command);
}

/* free_data: is the data inside the array dynamic or static? */
static void table_free(struct rline_table *t, bool free_data)
{
    if (free_data) {
        while (t->ncommands) {
            const struct rline_command *c = t->commands[--t->ncommands];
            free((void *)c->cmd);
            free((void *)c);
        }
    }
    free(t->commands);
    memset(t, 0, sizeof(*t));
}

enum table_return {
    TR_FOUND,
    TR_AMBIG,
    TR_NOT_FOUND
};

static enum table_return table_find (const struct rline_table *table, const char *txt, const struct rline_command **cmd)
{
    int txtlen = strlen(txt);
    int i;

    *cmd = NULL;
    for (i = 0; i < table->ncommands; i++) {
        const struct rline_command *t = table->commands[i];
        int cmdlen = strlen(t->cmd);

        if (txtlen <= cmdlen) {
            if ((txtlen == cmdlen) && (strcasecmp(t->cmd, txt) == 0)) {
		*cmd = t; /* exact match, short circuit the rest */
                return TR_FOUND; 
	    }
            if (strncasecmp(t->cmd, txt, txtlen) == 0) {
                if (*cmd)
		    return TR_AMBIG;
                *cmd = t;
            }
        }
    }
    return *cmd ? TR_FOUND : TR_NOT_FOUND;
}





void rline_register(const struct rline_command *c, size_t n)
{
    table_push(&command_table, c, n);
}

void rline_register_param(struct rline_table *t, const char * p)
{
    struct rline_command *c = calloc(1, sizeof(struct rline_command));

    c->cmd = strdup(p);

    table_push(t, c, 1);
}

void rline_free_table(struct rline_table *t)
{
    table_free(t, 1);
}

static void help_cmd(int argc, char *argv[])
{
    int i;

    for (i = 0; i < command_table.ncommands; i++) {
        const struct rline_command *t = command_table.commands[i];
        printf("%s %s\n", t->cmd, t->help ? t->help : "");
    }
}

static void quit_cmd(int argc, char *argv[])
{
    please_exit = true;
}

static char *chomp(char *line)
{
    int n;

    while (*line && isspace((int)*line))
        ++line;

    n = strlen(line);

    while (n && isspace((int)line[n-1]))
        line[--n] = '\0';

    return line;
}

void rline_source_file(const char *fname)
{
    FILE *fp = fopen(fname, "r");
    char buffer[1024];

    if (fp == NULL)
    {
        printf("error opening %s: %s\n", fname, strerror(errno));
        return;
    }

    printf("reading commands from \"%s\"\n", fname);


    while (fgets(buffer, sizeof(buffer), fp))
    {
        parse_command(0, chomp(buffer));
    }

    fclose(fp);
}

static void source_cmd(int argc, char *argv[])
{
    if (argc != 1)
    {
        printf("Usage: source FILE\n");
        return;
    }
    rline_source_file(argv[0]);
}

static void history_cmd(int argc, char *argv[])
{
    HIST_ENTRY **the_list = history_list();
    int i;

    if (the_list)
        for (i = 0; the_list[i]; i++)
            printf ("%4d   %s\n", i + history_base, the_list[i]->line);
}

static char * command_generator_table (struct rline_table *t, int *list_index, int *len, const char *text, int state)
{
    const char *name;

    /* If this is a new word to complete, initialize now.  This includes
       saving the length of TEXT for efficiency, and initializing the index
       variable to 0. */
    if (!state) {
        *list_index = 0;
        *len = strlen (text);
    }

    if (*list_index >= t->ncommands)
        return NULL;

    /* Return the next name which partially matches from the command list. */
    while ((name = t->commands[*list_index]->cmd)) {
        (*list_index)++;
        if (strncmp (name, text, *len) == 0)
            return (strdup(name));
        if (*list_index >= t->ncommands)
            break;
    }

    /* If no names matched, then return NULL. */
    return ((char *)NULL);
}

static struct rline_table *generator_table;

static char * command_generator (const char *text, int state)
{
    static int list_index, len;
    return command_generator_table(generator_table, &list_index, &len, text, state);
}

// dup buffer
// split buffer, up to but not including word we're completing on
// walk the command tree to find the proper table
// run the generator using that table


static char **completion (const char *text, int start, int end)
{
    struct arglist ar = {0};
    char *expansion;
    int result;
    char *buf = strdup(rl_line_buffer);
    char **matches = (char **)NULL;

    buf[start] = '\0';

    result = history_expand (buf, &expansion);
    if (result)
        fprintf (stderr, "%s\n", expansion);

    if (result < 0 || result == 2) {
        free (expansion);
        return NULL;
    }

    if (split(expansion, &ar))
    {
        int i;
        generator_table = &command_table;

        for (i = 0; (i < ar.count) && generator_table; i++) {
            const struct rline_command *c;
	    table_find(generator_table, ar.argv[i], &c);
	    generator_table = c ? c->params : NULL;
        }
    }
    if (generator_table)
        matches = rl_completion_matches(text, command_generator);

    free(buf);
    free(ar.argv);
    free(expansion);

    return matches;
}

/* return error message OR NULL IF SUCCESS */
static char *find_matching(char delim, char *work, char **matching)
{
    int quoted = 0;

    while (*work)
    {
	if (quoted)
	{
	    int len = strlen(work);
	    memmove(work - 1, work, len + 1);
	    quoted = 0;
            continue; /* skip ++ below, as we've already moved to the next char */
	}
	else if (*work == delim)
	{
	    /* after quote should either be space, or EOL.
	     * anything else is an error */
	    if (isspace((int)work[1]) || !work[1])
	    {
		*matching = work;
		return NULL;
	    }
	    return "garbage after quote";
	}
	else if (*work == '\\')
	{
	    quoted = 1;
	}
	++work;
    }
    return "no matching quote";
}

/* return error string or NULL IF SUCCESS */
static char *next_arg(char **line, char **arg)
{
    char *work = *line;

    *arg = NULL;
    if (!*line)
	return NULL;
    while (*work && isspace((int)*work))
	++work;
    if (!*work)
	return NULL;

    *arg = work;
    if (*work == '"' || *work == '\'')
    {
	char delim = *work++;
	char *end;
	char *err = find_matching(delim, work, &end);
	if (err)
	    return err; /* shit need error_ret */
	*arg = work;
	work = end;
    }
    else
    {
	work = strchr(work, ' ');
    }

    if (work)
	*work++ = '\0';
    *line = work;
    return NULL;
}

static void ensure(struct arglist *ar)
{
    if (ar->count >= ar->max)
    {
	ar->max += 10;
	ar->argv = realloc(ar->argv, ar->max * sizeof(char *));
    }
}

static void push_arg(struct arglist *ar, char *str)
{
    ensure(ar);
    ar->argv[ar->count++] = str;
}

static bool split(char *line, struct arglist *ar)
{
    do {
	char *ptr = NULL;
	char *err = next_arg(&line, &ptr);

	if (err)
	{
	    printf("error: %s\n", err);
	    return false;
	}
	if (ptr == NULL)
	    return true;
	push_arg(ar, ptr);
    } while (1);
}

void rline_exec(int argc, char *argv[])
{
    const struct rline_command *c;
    bool saved = rline_ready;

    rline_ready = false;

    enum table_return rv = table_find(&command_table, argv[0], &c);

    switch (rv) {
	case TR_AMBIG:
	    printf("'%s': command is ambiguous.\n", argv[0]);
	    break;
	case TR_NOT_FOUND:
	    printf("'%s': command not found.\n", argv[0]);
	    break;
	case TR_FOUND:
	    ++argv, --argc;
	    (*c->func)(argc, argv);
	    break;
    }
    rline_ready = saved;
}

static int saved_point, saved_mark;

static void prep_display(void)
{
    rl_save_prompt();
    saved_point = rl_point;
    saved_mark = rl_mark;
    rl_begin_undo_group();
    rl_delete_text(0, rl_end);
    rl_end_undo_group();
    rl_message("");
}

static void restore_display(void)
{
    rl_restore_prompt();
    rl_do_undo();
    rl_point = saved_point;
    rl_mark = saved_mark;
    rl_forced_update_display();
}

void rline_message(const char *format, ...)
{
    va_list ma;

    if (rline_ready)
    {
        prep_display();
    }

    va_start(ma, format);
    vprintf(format, ma);
    va_end(ma);

    if (rline_ready)
    {
        restore_display();
    }
}

void rline_set_prompt(const char *format, ...)
{
    char buf[512];
    va_list ma;

    va_start(ma, format);
    vsnprintf(buf, sizeof(buf), format, ma);
    va_end(ma);

    rl_set_prompt(buf);
}

static void parse_command(int use_history, char *line)
{
    struct arglist ar = {0};
    char *expansion;
    int result;

    if (line == NULL)
        exit(1);
    if (strlen(line) <= 0)
        return;

    result = history_expand (line, &expansion);
    if (result)
        fprintf (stderr, "%s\n", expansion);

    if (result < 0 || result == 2) {
        free (expansion);
        return;
    }

    if (use_history)
        add_history (expansion);
    line = expansion;

    if (split(line, &ar))
        rline_exec(ar.count, ar.argv);
    free(ar.argv);
    free(line);
}

static void rl_callback(char *line)
{
    parse_command(1, line);
    free(line);
}

static const struct rline_command builtins[] = {
    { "quit", &quit_cmd, "\n\tquit" },
    { "help", &help_cmd, "\n\tshow help" },
    { "?", &help_cmd, "\n\tshow help" },
    { "source", &source_cmd, "FILE\n\tsource commands from FILE" },
    { "history", &history_cmd, "\n\tlist history" },
};

void rline_setup(const char *prefix, bool interactive)
{
    dot_home_relative(prefix, "history", history_fname, sizeof(history_fname));

    using_history();
    read_history(history_fname);

    rl_attempted_completion_function = completion;
    rline_register(builtins, ARRAY_SIZE(builtins));

    if (interactive)
	rl_callback_handler_install("> ", rl_callback);

    rline_ready = true;
}

void rline_cleanup(void)
{
    rl_callback_handler_remove();
    write_history(history_fname);

    table_free(&command_table, 0);
}

void rline_read_ch(void)
{
    rl_callback_read_char();
}

/* just a dummy main loop, you know, for simple things. */
void rline_main_loop(void)
{
    while (!please_exit) {
        struct pollfd pfd = {
            .fd = STDIN_FILENO,
            .events = POLLIN
        };
        int r = poll(&pfd, 1, -1);

        if ((r < 0) && (errno != EINTR)) {
            fprintf(stderr, "poll error: %s\n", strerror(errno));
            exit(1);
        } else if (r > 0) {
            rline_read_ch();
        }
    }
}
