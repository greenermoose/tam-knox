#include <tam.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static void run_git(const char *const argv[])
{
    tam_exec_result_t r = tam_exec(argv, 10000);
    assert(r.exit_code == 0);
    tam_exec_result_free(&r);
}

static size_t drain_pty(int mfd, char *buf, size_t cap, int wait_ms)
{
    size_t total = 0;
    struct pollfd pfd = { .fd = mfd, .events = POLLIN };
    while (total + 1 < cap && poll(&pfd, 1, wait_ms) > 0) {
        if (!(pfd.revents & POLLIN))
            break;
        ssize_t n = read(mfd, buf + total, cap - total - 1);
        if (n <= 0)
            break;
        total += (size_t)n;
        wait_ms = 30;
    }
    buf[total] = '\0';
    return total;
}

int main(int argc, char **argv)
{
    const char *bin = (argc > 1) ? argv[1] : "build/tam-knox";

    char tmp_tpl[] = "/tmp/knox-pty-XXXXXX";
    char *ws_dir = mkdtemp(tmp_tpl);
    assert(ws_dir != NULL);

    char *repo1 = tam_path_join(ws_dir, "demo-repo");
    assert(tam_fs_mkdir_p(repo1, 0755));
    run_git((const char *[]){"git", "-C", repo1, "init", "-b", "dev", NULL});
    run_git((const char *[]){"git", "-C", repo1, "config", "user.name", "Fred", NULL});
    run_git((const char *[]){"git", "-C", repo1, "config", "user.email", "fred@example.com", NULL});

    char *md_file = tam_path_join(repo1, "README.md");
    const char *md_content = "# Demo Repo\n\nWelcome to **tam-knox**.\n";
    assert(tam_fs_write_atomic(md_file, md_content, strlen(md_content), 0644));
    run_git((const char *[]){"git", "-C", repo1, "add", "--", "README.md", NULL});
    run_git((const char *[]){"git", "-C", repo1, "commit", "-m", "Initial README", NULL});

    /* Dirty the repo so Changes list has an entry */
    const char *md_dirty = "# Demo Repo\n\nWelcome to **tam-knox** v0.1.0.\n";
    assert(tam_fs_write_atomic(md_file, md_dirty, strlen(md_dirty), 0644));

    /* Write a temporary keymap.ini to verify user keybinding overrides via TAM_KNOX_KEYMAP */
    char *km_file = tam_path_join(ws_dir, "keymap.ini");
    const char *km_ini = "[keymap]\nF5 = guidance\nF6 = fetch-all\n";
    assert(tam_fs_write_atomic(km_file, km_ini, strlen(km_ini), 0644));

    int mfd = posix_openpt(O_RDWR | O_NOCTTY);
    assert(mfd >= 0);
    assert(grantpt(mfd) == 0);
    assert(unlockpt(mfd) == 0);
    char *sname = ptsname(mfd);
    assert(sname != NULL);

    struct winsize wsz = { .ws_row = 30, .ws_col = 110 };
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(mfd);
        setsid();
        int sfd = open(sname, O_RDWR);
        if (sfd < 0)
            _exit(127);
        ioctl(sfd, TIOCSCTTY, 0);
        ioctl(sfd, TIOCSWINSZ, &wsz);
        dup2(sfd, STDIN_FILENO);
        dup2(sfd, STDOUT_FILENO);
        dup2(sfd, STDERR_FILENO);
        if (sfd > 2)
            close(sfd);
        setenv("TERM", "xterm-256color", 1);
        setenv("TAM_KNOX_KEYMAP", km_file, 1);
        execl(bin, bin, ws_dir, (char *)NULL);
        _exit(127);
    }

    char out[65536];
    size_t n = drain_pty(mfd, out, sizeof(out), 800);
    assert(n > 0);
    assert(strstr(out, "tam-knox") != NULL);
    assert(strstr(out, "demo-repo") != NULL);
    assert(strstr(out, "Initial README") != NULL);

    /* 1. Press Enter on repo -> focuses Git Graph, then Enter again -> opens Commit Details split */
    assert(write(mfd, "\r", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 150);
    assert(write(mfd, "\r", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 250);
    assert(strstr(out, "Commit Details:") != NULL);

    /* Press Enter on the changed file in Commit Details -> opens Diff Viewer, then 'd' toggles Side-by-Side Diff */
    assert(write(mfd, "\r", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 200);
    assert(strstr(out, "Unified Diff") != NULL);

    assert(write(mfd, "d", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 200);
    assert(strstr(out, "Side-by-Side Diff") != NULL);

    /* Open Branch Modal (Alt+K) and close with Esc, then open Stash Modal (Alt+Z) and close with Esc */
    assert(write(mfd, "\033k", 2) == 2);
    drain_pty(mfd, out, sizeof(out), 200);
    assert(strstr(out, "Branches: demo-repo") != NULL);
    assert(write(mfd, "\033", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 150);

    assert(write(mfd, "\033z", 2) == 2);
    drain_pty(mfd, out, sizeof(out), 200);
    assert(strstr(out, "Stashes: demo-repo") != NULL);
    assert(write(mfd, "\033", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 150);

    /* 2. Switch sidebar to Explorer (Alt+2), filter with '/', then clear with Esc */
    assert(write(mfd, "\0332", 2) == 2);
    drain_pty(mfd, out, sizeof(out), 150);
    assert(strstr(out, "EXPLORER") != NULL);

    assert(write(mfd, "/readme\r", 8) == 8);
    drain_pty(mfd, out, sizeof(out), 200);
    assert(strstr(out, "filter: \"readme\"") != NULL);
    assert(strstr(out, "README.md") != NULL);

    assert(write(mfd, "\033", 1) == 1); /* Esc clears Explorer filter */
    drain_pty(mfd, out, sizeof(out), 150);
    assert(strstr(out, "Cleared Explorer filter") != NULL);

    /* Expand demo-repo and open README.md */
    assert(write(mfd, "j\rj\r", 4) == 4);
    drain_pty(mfd, out, sizeof(out), 250);
    assert(strstr(out, "Demo Repo") != NULL);

    /* 3. Toggle Raw Syntax mode ('R'), split vertically (Alt+V), test batch fetch (Alt+A),
     *    and test custom keymap.ini binding (F5 -> guidance), close, quit (Ctrl+Q) */
    assert(write(mfd, "R", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 150);
    assert(strstr(out, "Raw Syntax") != NULL);

    assert(write(mfd, "\033v", 2) == 2);
    drain_pty(mfd, out, sizeof(out), 150);
    assert(strstr(out, "Split pane vertically") != NULL);

    assert(write(mfd, "\033a", 2) == 2); /* Alt+A runs multi-repo batch fetch */
    drain_pty(mfd, out, sizeof(out), 250);
    assert(strstr(out, "Fetched 0/1") != NULL);

    assert(write(mfd, "\033[15~", 5) == 5); /* F5 bound to guidance via keymap.ini */
    drain_pty(mfd, out, sizeof(out), 150);
    assert(strstr(out, "Keybinding Guidance") != NULL);
    assert(strstr(out, "F6*") != NULL);

    assert(write(mfd, "g\x11", 2) == 2); /* g closes guidance, then Ctrl+Q quits */
    drain_pty(mfd, out, sizeof(out), 200);

    int status = 0;
    waitpid(pid, &status, 0);
    close(mfd);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    free(km_file);
    free(md_file);
    free(repo1);
    tam_fs_remove_tree(-100, ws_dir);

    printf("test-pty: all interactive TUI checks passed\n");
    return 0;
}
