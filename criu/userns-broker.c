#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#include <grp.h>

#include "log.h"
#include "namespaces.h"
#include "userns-broker.h"
#include "util.h"

static int userns_broker_drop_groups(const char *op_name)
{
	if (setgroups(0, NULL)) {
		if (errno == EPERM) {
			gid_t *groups = NULL;
			int i, nr_groups;

			nr_groups = getgroups(0, NULL);
			if (nr_groups > 0) {
				groups = xmalloc(sizeof(*groups) * nr_groups);
				if (!groups)
					return -1;
				nr_groups = getgroups(nr_groups, groups);
			}

			for (i = 0; i < nr_groups; i++) {
				if (groups[i] != 0)
					break;
			}

			free(groups);
			if (nr_groups >= 0 && i == nr_groups) {
				pr_info("userns broker %s: setgroups denied, groups already safe\n", op_name);
				return 0;
			}

			if (in_noninitial_userns()) {
				pr_info("userns broker %s: tolerate setgroups denial in user namespace\n", op_name);
				return 0;
			}

		}
		pr_perror("userns broker %s: setgroups", op_name);
		return -1;
	}

	return 0;
}

static int userns_broker_enter_creds(const char *op_name)
{
	if ((geteuid() != 0 && setuid(0)) || (getegid() != 0 && setgid(0))) {
		pr_perror("userns broker %s: set uid/gid 0 in target userns", op_name);
		return -1;
	}

	return 0;
}

int userns_broker_enter(int pid, const char *op_name)
{
	int userns_fd;

	if (userns_broker_drop_groups(op_name))
		return -1;

	userns_fd = do_open_proc(pid, O_RDONLY, "ns/user");
	if (userns_fd < 0) {
		pr_perror("userns broker %s: open proc %d ns/user", op_name, pid);
		return -1;
	}

	if (setns(userns_fd, CLONE_NEWUSER)) {
		if (errno == EINVAL)
			pr_info("userns broker %s: already in target userns\n", op_name);
		else {
			pr_perror("userns broker %s: setns userns %d", op_name, pid);
			close(userns_fd);
			return -1;
		}
	}
	close(userns_fd);

	return userns_broker_enter_creds(op_name);
}
