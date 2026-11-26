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

static void get_git_rev(const char *repo_dir, const char *rev,
                        char *out, size_t cap)
{
    const char *argv[] = { "git", "-C", repo_dir, "rev-parse", rev, NULL };
    tam_exec_result_t r = tam_exec(argv, 10000);
    assert(r.exit_code == 0 && r.stdout_buf != NULL);
    snprintf(out, cap, "%s", r.stdout_buf);
    char *nl = strchr(out, '\n');
    if (nl)
        *nl = '\0';
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

static bool wait_for_pty_text(int mfd, char *buf, size_t cap,
                              const char *needle, int max_wait_ms)
{
    size_t total = 0;
    buf[0] = '\0';
    int elapsed = 0;
    const int step_ms = 50;
    struct pollfd pfd = { .fd = mfd, .events = POLLIN };
    while (elapsed < max_wait_ms && total + 1 < cap) {
        int rc = poll(&pfd, 1, step_ms);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(mfd, buf + total, cap - total - 1);
            if (n <= 0)
                break;
            total += (size_t)n;
            buf[total] = '\0';
            if (strstr(buf, needle) != NULL) {
                /* Drain any remaining bytes of the current frame */
                while (total + 1 < cap && poll(&pfd, 1, 30) > 0 &&
                       (pfd.revents & POLLIN)) {
                    n = read(mfd, buf + total, cap - total - 1);
                    if (n <= 0)
                        break;
                    total += (size_t)n;
                    buf[total] = '\0';
                }
                return true;
            }
        } else {
            elapsed += step_ms;
        }
    }
    return strstr(buf, needle) != NULL;
}

static void test_pty_repo_sync(const char *bin)
{
    char tmp_tpl[] = "/tmp/knox-pty-sync-XXXXXX";
    char *ws_dir = mkdtemp(tmp_tpl);
    assert(ws_dir != NULL);

    char *remote_dir = tam_path_join(ws_dir, "remote.git");
    char *repo_ahead = tam_path_join(ws_dir, "repo-ahead");
    char *repo_behind = tam_path_join(ws_dir, "repo-behind");
    assert(tam_fs_mkdir_p(remote_dir, 0755));
    assert(tam_fs_mkdir_p(repo_ahead, 0755));

    /* 1. Bare remote + repo-ahead with initial commit pushed to origin/dev */
    run_git((const char *[]){"git", "-C", remote_dir, "init", "--bare", "-b", "dev", NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "init", "-b", "dev", NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "config", "user.name", "Fred", NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "config", "user.email", "fred@example.com", NULL});

    char *file_a = tam_path_join(repo_ahead, "file.txt");
    assert(tam_fs_write_atomic(file_a, "v1\n", 3, 0, 0644) == 0);
    run_git((const char *[]){"git", "-C", repo_ahead, "add", "--", "file.txt", NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "commit", "-m", "v1", NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "remote", "add", "origin", remote_dir, NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "push", "-u", "origin", "dev", NULL});

    /* 2. Clone repo-behind at v1, then push v2 from repo-ahead and fetch in repo-behind */
    run_git((const char *[]){"git", "clone", remote_dir, repo_behind, NULL});
    run_git((const char *[]){"git", "-C", repo_behind, "config", "user.name", "Fred", NULL});
    run_git((const char *[]){"git", "-C", repo_behind, "config", "user.email", "fred@example.com", NULL});

    assert(tam_fs_write_atomic(file_a, "v2\n", 3, 0, 0644) == 0);
    run_git((const char *[]){"git", "-C", repo_ahead, "add", "--", "file.txt", NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "commit", "-m", "v2", NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "push", NULL});
    run_git((const char *[]){"git", "-C", repo_behind, "fetch", "--prune", NULL});

    /* 3. Make local commit v3 in repo-ahead (ahead 1) while repo-behind is behind 1 */
    assert(tam_fs_write_atomic(file_a, "v3\n", 3, 0, 0644) == 0);
    run_git((const char *[]){"git", "-C", repo_ahead, "add", "--", "file.txt", NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "commit", "-m", "v3", NULL});

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
        unsetenv("TAM_KNOX_KEYMAP");
        execl(bin, bin, ws_dir, (char *)NULL);
        _exit(127);
    }

    char out[65536];
    size_t n = drain_pty(mfd, out, sizeof(out), 800);
    assert(n > 0);
    assert(strstr(out, "repo-ahead") != NULL);
    assert(strstr(out, "repo-behind") != NULL);
    assert(strstr(out, "\xe2\x86\x91" "1 " "\xe2\x86\x93" "0") != NULL);
    assert(strstr(out, "\xe2\x86\x91" "0 " "\xe2\x86\x93" "1") != NULL);

    /* 4. Press 's' on repo-ahead (selected by default) -> pushes v3 and clears badge */
    assert(write(mfd, "s", 1) == 1);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "pushed 1 commit(s)", 5000));

    char ahead_head[64], ahead_up[64], remote_head[64];
    get_git_rev(repo_ahead, "HEAD", ahead_head, sizeof(ahead_head));
    get_git_rev(repo_ahead, "origin/dev", ahead_up, sizeof(ahead_up));
    get_git_rev(remote_dir, "refs/heads/dev", remote_head, sizeof(remote_head));
    assert(strcmp(ahead_head, ahead_up) == 0);
    assert(strcmp(ahead_head, remote_head) == 0);

    /* Redraw cleanly with Ctrl+L and confirm repo-ahead's ahead badge is gone */
    assert(write(mfd, "\x0c", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 250);
    assert(strstr(out, "\xe2\x86\x91" "1 " "\xe2\x86\x93" "0") == NULL);

    /* 5. Move down to repo-behind ('j') and press 's' -> pulls v2+v3 and clears badge */
    assert(write(mfd, "js", 2) == 2);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "pulled 2 commit(s)", 5000));

    char behind_head[64], behind_up[64];
    get_git_rev(repo_behind, "HEAD", behind_head, sizeof(behind_head));
    get_git_rev(repo_behind, "origin/dev", behind_up, sizeof(behind_up));
    assert(strcmp(behind_head, behind_up) == 0);
    assert(strcmp(behind_head, remote_head) == 0);

    /* Redraw cleanly with Ctrl+L and confirm neither repo has an ahead/behind badge */
    assert(write(mfd, "\x0c", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 250);
    assert(strstr(out, "\xe2\x86\x91" "0 " "\xe2\x86\x93" "1") == NULL);
    assert(strstr(out, "\xe2\x86\x91" "0 " "\xe2\x86\x93" "2") == NULL);

    /* 6. Push v4 from repo-ahead directly to remote, then press 'f' on selected
     *    repo-behind in tam-knox -> fetches without pulling and shows ↓1 badge */
    assert(tam_fs_write_atomic(file_a, "v4\n", 3, 0, 0644) == 0);
    run_git((const char *[]){"git", "-C", repo_ahead, "add", "--", "file.txt", NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "commit", "-m", "v4", NULL});
    run_git((const char *[]){"git", "-C", repo_ahead, "push", NULL});

    assert(write(mfd, "f", 1) == 1);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "repo-behind (dev): fetched (+0/-1)", 5000));

    char behind_head_after_fetch[64], behind_up_after_fetch[64], remote_v4[64];
    get_git_rev(repo_behind, "HEAD", behind_head_after_fetch, sizeof(behind_head_after_fetch));
    get_git_rev(repo_behind, "origin/dev", behind_up_after_fetch, sizeof(behind_up_after_fetch));
    get_git_rev(remote_dir, "refs/heads/dev", remote_v4, sizeof(remote_v4));
    /* Working tree HEAD must remain at v3 while origin/dev moved to v4 */
    assert(strcmp(behind_head_after_fetch, behind_head) == 0);
    assert(strcmp(behind_up_after_fetch, remote_v4) == 0);
    assert(strcmp(behind_head_after_fetch, behind_up_after_fetch) != 0);

    assert(write(mfd, "\x0c", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 250);
    assert(strstr(out, "\xe2\x86\x91" "0 " "\xe2\x86\x93" "1") != NULL);

    /* Quit cleanly */
    assert(write(mfd, "\x11", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 200);

    int status = 0;
    waitpid(pid, &status, 0);
    close(mfd);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    free(file_a);
    free(repo_behind);
    free(repo_ahead);
    free(remote_dir);
    tam_fs_remove_tree(-100, ws_dir);
}

static void test_pty_explore_repo(const char *bin)
{
    char tmp_tpl[] = "/tmp/knox-pty-exp-XXXXXX";
    char *ws_dir = mkdtemp(tmp_tpl);
    assert(ws_dir != NULL);

    char *repo_a = tam_path_join(ws_dir, "alpha-repo");
    char *repo_b = tam_path_join(ws_dir, "beta-repo");
    char *sub_dir = tam_path_join(repo_b, "sub");
    assert(tam_fs_mkdir_p(repo_a, 0755));
    assert(tam_fs_mkdir_p(sub_dir, 0755));

    run_git((const char *[]){"git", "-C", repo_a, "init", "-b", "dev", NULL});
    run_git((const char *[]){"git", "-C", repo_a, "config", "user.name", "Fred", NULL});
    run_git((const char *[]){"git", "-C", repo_a, "config", "user.email", "fred@example.com", NULL});
    char *fa = tam_path_join(repo_a, "alpha_only.txt");
    assert(tam_fs_write_atomic(fa, "alpha\n", 6, 0, 0644) == 0);
    run_git((const char *[]){"git", "-C", repo_a, "add", "--", "alpha_only.txt", NULL});
    run_git((const char *[]){"git", "-C", repo_a, "commit", "-m", "alpha", NULL});

    run_git((const char *[]){"git", "-C", repo_b, "init", "-b", "dev", NULL});
    run_git((const char *[]){"git", "-C", repo_b, "config", "user.name", "Fred", NULL});
    run_git((const char *[]){"git", "-C", repo_b, "config", "user.email", "fred@example.com", NULL});
    char *fb = tam_path_join(repo_b, "beta_only.txt");
    char *fn = tam_path_join(sub_dir, "nested.txt");
    assert(tam_fs_write_atomic(fb, "beta\n", 5, 0, 0644) == 0);
    assert(tam_fs_write_atomic(fn, "nested contents\n", 16, 0, 0644) == 0);
    run_git((const char *[]){"git", "-C", repo_b, "add", "--", "beta_only.txt", "sub/nested.txt", NULL});
    run_git((const char *[]){"git", "-C", repo_b, "commit", "-m", "beta", NULL});

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
        unsetenv("TAM_KNOX_KEYMAP");
        execl(bin, bin, ws_dir, (char *)NULL);
        _exit(127);
    }

    char out[65536];
    size_t n = drain_pty(mfd, out, sizeof(out), 800);
    assert(n > 0);
    assert(strstr(out, "alpha-repo") != NULL);
    assert(strstr(out, "beta-repo") != NULL);

    /* 1. Move to beta-repo ('j') and press Right Arrow -> switches to Explorer & expands beta-repo */
    assert(write(mfd, "j\033[C", 4) == 4);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "beta_only.txt", 3000));
    assert(strstr(out, "EXPLORER") != NULL);
    assert(strstr(out, "Exploring beta-repo") != NULL);
    assert(strstr(out, "nested.txt") == NULL);

    /* 2. Move to closed folder 'sub' ('j') and press Right Arrow -> opens folder & shows nested.txt */
    assert(write(mfd, "j\033[C", 4) == 4);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "nested.txt", 3000));

    /* 3. Move to 'nested.txt' ('j') and press Right Arrow -> opens in File Viewer & focuses [Workbench] */
    assert(write(mfd, "j\033[C", 4) == 4);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "nested contents", 3000));
    assert(strstr(out, "[Workbench]") != NULL);

    /* 4. Press Left Arrow in File Viewer -> moves focus back to [Explorer] */
    assert(write(mfd, "\033[D", 3) == 3);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "[Explorer]", 3000));

    /* 5. Move up to open folder 'sub' ('k') and press Left Arrow -> closes folder & hides nested.txt */
    assert(write(mfd, "k\033[D", 4) == 4);
    drain_pty(mfd, out, sizeof(out), 250);
    assert(write(mfd, "\x0c", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 250);
    assert(strstr(out, "beta_only.txt") != NULL);
    assert(strstr(out, "sub") != NULL);

    /* 6. Press Backspace (0x7f) -> returns to Source Control with beta-repo selected */
    assert(write(mfd, "\x7f", 1) == 1);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "CHANGES: beta-repo", 3000));
    assert(strstr(out, "REPOSITORIES") != NULL);

    /* Quit cleanly */
    assert(write(mfd, "\x11", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 200);

    int status = 0;
    waitpid(pid, &status, 0);
    close(mfd);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    free(fn);
    free(fb);
    free(fa);
    free(sub_dir);
    free(repo_b);
    free(repo_a);
    tam_fs_remove_tree(-100, ws_dir);
}

static void test_pty_tab_number_select(const char *bin)
{
    char tmp_tpl[] = "/tmp/knox-pty-tab-XXXXXX";
    char *ws_dir = mkdtemp(tmp_tpl);
    assert(ws_dir != NULL);

    char *repo = tam_path_join(ws_dir, "demo-repo");
    assert(tam_fs_mkdir_p(repo, 0755));
    run_git((const char *[]){"git", "-C", repo, "init", "-b", "dev", NULL});
    run_git((const char *[]){"git", "-C", repo, "config", "user.name", "Fred", NULL});
    run_git((const char *[]){"git", "-C", repo, "config", "user.email", "fred@example.com", NULL});

    char *f = tam_path_join(repo, "notes.txt");
    assert(tam_fs_write_atomic(f, "line1\n", 6, 0, 0644) == 0);
    run_git((const char *[]){"git", "-C", repo, "add", "--", "notes.txt", NULL});
    run_git((const char *[]){"git", "-C", repo, "commit", "-m", "init notes", NULL});
    assert(tam_fs_write_atomic(f, "line1\nline2\n", 12, 0, 0644) == 0);

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
        unsetenv("TAM_KNOX_KEYMAP");
        execl(bin, bin, ws_dir, (char *)NULL);
        _exit(127);
    }

    char out[65536];
    size_t n = drain_pty(mfd, out, sizeof(out), 800);
    assert(n > 0);
    /* Top bar omits Alt+1/Alt+2 hints, aligns tab strip with main pane (col 38 on 110-col PTY),
     * and shows full 1:Git Graph title */
    assert(strstr(out, "[Alt+1 Source Control]") == NULL);
    assert(strstr(out, "[Alt+2 Explorer]") == NULL);
    assert(strstr(out, "\033[1;38H") != NULL);
    assert(strstr(out, "1:Git Graph") != NULL);

    /* Open tab 2 (Diff notes.txt) from Changes */
    assert(write(mfd, "\t\r", 2) == 2);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "2:Diff notes.txt", 3000));
    assert(strstr(out, "Unified Diff") != NULL);

    /* Focus sidebar (Alt+1 -> [Repos]), then press '1' -> selects & focuses Git Graph */
    assert(write(mfd, "\0331", 2) == 2);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "[Repos]", 3000));
    assert(write(mfd, "1", 1) == 1);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "[Workbench]", 3000));
    assert(strstr(out, "Git Graph | Repo: demo-repo") != NULL);

    /* Focus sidebar (Alt+1 -> [Repos]), then press '2' -> selects & focuses Diff tab */
    assert(write(mfd, "\0331", 2) == 2);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "[Repos]", 3000));
    assert(write(mfd, "2", 1) == 1);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "[Workbench]", 3000));
    assert(strstr(out, "Unified Diff") != NULL);

    /* Focus sidebar, press '0' when only 2 tabs exist -> safe no-op, stays in [Repos] */
    assert(write(mfd, "\0331", 2) == 2);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "[Repos]", 3000));
    assert(write(mfd, "0", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 200);
    assert(write(mfd, "\x0c", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 200);
    assert(strstr(out, "[Repos]") != NULL);

    /* Open 8 more tabs (Ctrl+N x 8) to reach 10 tabs; all 10 (1..9, 0) must appear */
    for (int k = 0; k < 8; k++) {
        assert(write(mfd, "\x0e", 1) == 1);
        drain_pty(mfd, out, sizeof(out), 80);
    }
    assert(write(mfd, "\x0c", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 250);
    assert(strstr(out, "1:") != NULL);
    assert(strstr(out, "2:") != NULL);
    assert(strstr(out, "9:") != NULL);
    assert(strstr(out, "0:") != NULL);

    /* Focus sidebar, press '0' -> selects & focuses 10th tab ([Workbench]) */
    assert(write(mfd, "\0331", 2) == 2);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "[Repos]", 3000));
    assert(write(mfd, "0", 1) == 1);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "[Workbench]", 3000));
    assert(strstr(out, "Git Graph | Repo: demo-repo") != NULL);

    /* Digits typed in search modal ('/1230') stay in prompt and don't switch tabs */
    assert(write(mfd, "/1230", 5) == 5);
    assert(wait_for_pty_text(mfd, out, sizeof(out), "Find: 1230", 3000));
    assert(write(mfd, "\033", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 150);

    /* Quit cleanly */
    assert(write(mfd, "\x11", 1) == 1);
    drain_pty(mfd, out, sizeof(out), 200);

    int status = 0;
    waitpid(pid, &status, 0);
    close(mfd);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    free(f);
    free(repo);
    tam_fs_remove_tree(-100, ws_dir);
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
    assert(tam_fs_write_atomic(md_file, md_content, strlen(md_content), 0, 0644) == 0);
    run_git((const char *[]){"git", "-C", repo1, "add", "--", "README.md", NULL});
    run_git((const char *[]){"git", "-C", repo1, "commit", "-m", "Initial README", NULL});

    /* Dirty the repo so Changes list has an entry */
    const char *md_dirty = "# Demo Repo\n\nWelcome to **tam-knox** v0.1.0.\n";
    assert(tam_fs_write_atomic(md_file, md_dirty, strlen(md_dirty), 0, 0644) == 0);

    /* Write a temporary keymap.ini to verify user keybinding overrides via TAM_KNOX_KEYMAP */
    char *km_file = tam_path_join(ws_dir, "keymap.ini");
    const char *km_ini = "[keymap]\nF5 = guidance\nF6 = fetch-all\n";
    assert(tam_fs_write_atomic(km_file, km_ini, strlen(km_ini), 0, 0644) == 0);

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

    /* 1. Focus Git Graph (Alt+3), then press Enter -> opens Commit Details split */
    assert(write(mfd, "\0333", 2) == 2);
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
    /* Centered modals (110x30 -> box 86x26 at x0=13, y0=3) draw a 1-row top/bottom
     * and 2-col left/right pure-black border (\033[48;5;16m) around the box. */
    assert(strstr(out, "\033[2;11H\033[48;5;16m") != NULL);
    assert(strstr(out, "\033[3;11H\033[48;5;16m  \033[0m") != NULL);
    assert(strstr(out, "\033[3;99H\033[48;5;16m  \033[0m") != NULL);
    assert(strstr(out, "\033[29;11H\033[48;5;16m") != NULL);
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
    assert(strstr(out, "\033[2;11H\033[48;5;16m") != NULL);
    assert(strstr(out, "\033[3;11H\033[48;5;16m  \033[0m") != NULL);
    assert(strstr(out, "\033[3;99H\033[48;5;16m  \033[0m") != NULL);
    assert(strstr(out, "\033[29;11H\033[48;5;16m") != NULL);

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

    test_pty_repo_sync(bin);
    test_pty_explore_repo(bin);
    test_pty_tab_number_select(bin);

    printf("test-pty: all interactive TUI checks passed\n");
    return 0;
}
