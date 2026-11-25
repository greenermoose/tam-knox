#include "knox.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(FILE *out)
{
    fprintf(out,
        "Usage: tam-knox [options] [directory]\n"
        "\n"
        "Multi-repository Git workbench, commit graph, explorer, and file viewer.\n"
        "\n"
        "Options:\n"
        "  -s, --status        Print multi-repository status table and exit\n"
        "  -g, --graph [N]     Print commit graph for repository and exit\n"
        "  -a, --ascii         Use ASCII box and graph characters\n"
        "      --color         Force ANSI color output\n"
        "      --no-color      Disable ANSI color output\n"
        "  -v, --version       Print version and exit\n"
        "  -h, --help          Print this help and exit\n");
}

int main(int argc, char **argv)
{
    setlocale(LC_CTYPE, "");
    bool mode_status = false;
    bool mode_graph = false;
    size_t graph_limit = 30;
    int color_opt = 0; /* 0 = auto, 1 = always, -1 = never */
    bool force_ascii = false;
    const char *target_dir = ".";

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            printf("tam-knox %s\n", KNOX_VERSION);
            return 0;
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--status") == 0) {
            mode_status = true;
        } else if (strcmp(arg, "-g") == 0 || strcmp(arg, "--graph") == 0) {
            mode_graph = true;
            if (i + 1 < argc && argv[i + 1][0] >= '1' && argv[i + 1][0] <= '9') {
                graph_limit = (size_t)atoi(argv[++i]);
            }
        } else if (strcmp(arg, "-a") == 0 || strcmp(arg, "--ascii") == 0) {
            force_ascii = true;
        } else if (strcmp(arg, "--color") == 0 || strcmp(arg, "--color=always") == 0) {
            color_opt = 1;
        } else if (strcmp(arg, "--no-color") == 0 || strcmp(arg, "--color=never") == 0) {
            color_opt = -1;
        } else if (strcmp(arg, "--") == 0) {
            if (i + 1 < argc)
                target_dir = argv[++i];
            break;
        } else if (arg[0] == '-') {
            fprintf(stderr, "tam-knox: unknown option: %s\n", arg);
            print_usage(stderr);
            return 2;
        } else {
            target_dir = arg;
        }
    }

    const char *term = getenv("TERM");
    bool linux_vt = (term && strcmp(term, "linux") == 0);
    bool dumb = (!term || strcmp(term, "dumb") == 0);
    bool no_color_env = (getenv("NO_COLOR") != NULL);

    bool use_color;
    if (color_opt == 1)
        use_color = true;
    else if (color_opt == -1 || no_color_env || dumb)
        use_color = false;
    else
        use_color = isatty(STDOUT_FILENO);

    knox_options_t opts = {
        .ascii = force_ascii || linux_vt || dumb,
        .color = use_color,
        .linux_vt = linux_vt
    };

    if (mode_status)
        return knox_print_status_table(target_dir, opts);
    if (mode_graph)
        return knox_print_repo_graph(target_dir, graph_limit, opts);

    return knox_run_workbench(target_dir, opts);
}
