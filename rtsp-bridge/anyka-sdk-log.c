/* Run the stock SDK launcher with bounded stdout/stderr in RAM.
 * Keep its startup witness visible to the installed boot supervisor. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LOG_LIMIT (64 * 1024)
#define WITNESS "Video encoder open success"
#define HEADER WITNESS " [observed before log rotation]\n"
static volatile sig_atomic_t child_pid;
static volatile sig_atomic_t stopping;

static void stop_child(int sig)
{
	stopping = sig;
	if (child_pid > 0)
		kill((pid_t)child_pid, sig);
}

static int write_all(int fd, const void *data, size_t size)
{
	const char *p = data;
	while (size) {
		ssize_t n = write(fd, p, size);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return -1;
		p += n;
		size -= (size_t)n;
	}
	return 0;
}

static int new_log(const char *path)
{
	return open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_NONBLOCK, 0600);
}

int main(int argc, char **argv)
{
	int pipefd[2], out = -1, status = 0, failed = 0, witnessed = 0;
	size_t size = 0, tail_size = 0;
	char previous[512], pending[512], data[4096];
	char scan[4096 + sizeof(WITNESS)];
	char tail[sizeof(WITNESS)];
	struct sigaction action = {0};
	if (argc != 3 || snprintf(previous, sizeof(previous), "%s.1", argv[1]) >= (int)sizeof(previous) ||
	    snprintf(pending, sizeof(pending), "%s.new", argv[1]) >= (int)sizeof(pending)) {
		fprintf(stderr, "Usage: anyka-sdk-log LOGFILE SDK_START_SCRIPT\n");
		return 2;
	}
	out = new_log(argv[1]);
	struct stat st;
	if (out < 0 || fstat(out, &st) || !S_ISREG(st.st_mode) || pipe(pipefd)) {
		perror("SDK logger setup");
		if (out >= 0) close(out);
		return 1;
	}
	/* Remove a previous boot's witness/history before starting this SDK. */
	unlink(previous);
	unlink(pending);
	action.sa_handler = stop_child;
	sigemptyset(&action.sa_mask);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGHUP, &action, NULL);
	pid_t child = fork();
	if (child < 0) { perror("SDK logger fork"); return 1; }
	if (child == 0) {
		close(pipefd[0]); close(out);
		if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) _exit(126);
		close(pipefd[1]);
		execl("/bin/sh", "sh", argv[2], (char *)NULL);
		perror("SDK launcher"); _exit(127);
	}
	child_pid = child;
	close(pipefd[1]);
	for (;;) {
		ssize_t n = read(pipefd[0], data, sizeof(data));
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) { if (n < 0) failed = 1; break; }
		/* Match the witness even when a pipe read splits it in two. */
		memcpy(scan, tail, tail_size);
		memcpy(scan + tail_size, data, (size_t)n);
		size_t scan_size = tail_size + (size_t)n;
		for (size_t i = 0; i + sizeof(WITNESS) - 1 <= scan_size; ++i)
			if (!memcmp(scan + i, WITNESS, sizeof(WITNESS) - 1)) witnessed = 1;
		tail_size = scan_size;
		if (tail_size > sizeof(WITNESS) - 2) tail_size = sizeof(WITNESS) - 2;
		memcpy(tail, scan + scan_size - tail_size, tail_size);
		if (size + (size_t)n > LOG_LIMIT) {
			int next = new_log(pending);
			if (next < 0 || fstat(next, &st) || !S_ISREG(st.st_mode) ||
			    (witnessed && write_all(next, HEADER, sizeof(HEADER) - 1)) ||
			    rename(argv[1], previous) || rename(pending, argv[1])) {
				if (next >= 0) close(next);
				failed = 1; break;
			}
			close(out); out = next;
			size = witnessed ? sizeof(HEADER) - 1 : 0;
		}
		if (write_all(out, data, (size_t)n)) { failed = 1; break; }
		size += (size_t)n;
	}
	close(out); close(pipefd[0]);
	if (failed) { perror("SDK logger I/O"); kill(child, SIGTERM); }
	while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return 1;
	child_pid = 0;
	if (failed) return 1;
	if (stopping) return 128 + stopping;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
