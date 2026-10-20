#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static void fail(const char *operation)
{
	fprintf(stderr, "%s: %s\n", operation, strerror(errno));
	exit(EXIT_FAILURE);
}

static void move_mount(const char *new_root, const char *path)
{
	char destination[256];

	if (snprintf(destination, sizeof(destination), "%s%s", new_root, path) >= (int)sizeof(destination)) {
		errno = ENAMETOOLONG;
		fail(path);
	}

	if (mount(path, destination, NULL, MS_MOVE, NULL) < 0 && errno != ENOENT && errno != EINVAL)
		fail(path);
}

int main(int argc, char **argv)
{
	const char *new_root = argc > 1 ? argv[1] : "/newroot";

	move_mount(new_root, "/dev");

	if (chroot(new_root) < 0)
		fail("chroot");
	if (chdir("/") < 0)
		fail("chdir");

	execl("/sbin/init", "/sbin/init", (char *)NULL);
	fail("exec /sbin/init");
}