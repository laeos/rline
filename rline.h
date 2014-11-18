#ifndef RLINE_H
#define RLINE_H

#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

struct rline_table;

struct rline_command {
    const char *cmd;
    void (*func)(int argc, char *argv[]);
    const char *help;
    struct rline_table *params;
};

struct rline_table {
    const struct rline_command **commands;
    int ncommands;
    int max;
};

/* rline.cpp */
extern bool please_exit;

#define RLINE_INTERACTIVE	true
#define RLINE_BATCH		false
void rline_setup(const char *prefix, bool interactive);
void rline_cleanup(void);
void rline_register(const struct rline_command *c, size_t n);
void rline_read_ch(void);
void rline_main_loop(void);
void rline_message(const char *format, ...);
void rline_source_file(const char *fname);
void rline_exec(int argc, char *argv[]);

void rline_register_param(struct rline_table *t, const char * p);
void rline_free_table(struct rline_table *t);

#ifdef __cplusplus
}
#endif

#endif /* RLINE_H */
