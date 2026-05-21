#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/netlink.h>

#ifdef CONFIG_HAS_SELINUX
#include <selinux/selinux.h>
#endif

#include "common/scm.h"
#include "cr_options.h"
#include "external.h"
#include "files.h"
#include "kerndat.h"
#include "log.h"
#include "namespaces.h"
#include "netns-helper.h"
#include "util.h"

#undef LOG_PREFIX
#define LOG_PREFIX "netns: "

struct netns_helper_msg {
	int ret;
};

static int prep_netns_sockets_in_child(const struct netns_helper_req *req, int *nlsk, int *seqsk)
{
	int ret;

	*nlsk = -1;
	*seqsk = -1;

	if (req->for_dump) {
		ret = socket(PF_NETLINK, SOCK_RAW, NETLINK_SOCK_DIAG);
		if (ret < 0) {
			pr_perror("Can't create sock diag socket");
			return -1;
		}
		*nlsk = ret;
	}

#ifdef CONFIG_HAS_SELINUX
	if (kdat.lsm == LSMTYPE__SELINUX) {
		char *ctx;

		ret = getpidcon_raw(req->root_pid, &ctx);
		if (ret < 0) {
			pr_perror("Getting SELinux context for PID %d failed", req->root_pid);
			goto err;
		}

		ret = setsockcreatecon(ctx);
		freecon(ctx);
		if (ret < 0) {
			pr_perror("Setting SELinux socket context for PID %d failed", req->root_pid);
			goto err;
		}
	}
#endif

	ret = socket(PF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
	if (ret < 0) {
		pr_perror("Can't create seqsk for parasite");
		goto err;
	}
	*seqsk = ret;

#ifdef CONFIG_HAS_SELINUX
	if (kdat.lsm == LSMTYPE__SELINUX) {
		ret = setsockcreatecon_raw(NULL);
		if (ret < 0) {
			pr_perror("Resetting SELinux socket context failed");
			goto err;
		}
	}
#endif

	return 0;

err:
	if (*seqsk >= 0) {
		close(*seqsk);
		*seqsk = -1;
	}
	if (*nlsk >= 0) {
		close(*nlsk);
		*nlsk = -1;
	}
	return -1;
}

int netns_helper_open_target(const struct ns_id *ns, int *out_fd)
{
	int fd;

	if (ns->ext_key) {
		fd = inherit_fd_lookup_id(ns->ext_key);
		if (fd >= 0) {
			*out_fd = fd;
			return 0;
		}
	}

	fd = open_proc(ns->ns_pid, "ns/net");
	if (fd < 0)
		return -1;

	*out_fd = fd;
	return 0;
}

int netns_helper_prep(const struct netns_helper_req *req, struct netns_helper_resp *resp)
{
	int sk[2], target_fd = -1, pid, status;
	int fds[2], nr_fds;
	struct netns_helper_msg msg = {};
	bool target_owned = false;

	resp->nlsk = -1;
	resp->seqsk = -1;

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sk) < 0) {
		pr_perror("netns helper: socketpair");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		pr_perror("netns helper: fork");
		close(sk[0]);
		close(sk[1]);
		return -1;
	}

	if (pid == 0) {
		int nlsk = -1, seqsk = -1, ret;

		close(sk[0]);

		if (req->ns_fd >= 0) {
			target_fd = req->ns_fd;
		} else {
			target_fd = open_proc(req->ns_pid, "ns/net");
			if (target_fd < 0)
				goto child_err;
			target_owned = true;
		}

		if (setns(target_fd, CLONE_NEWNET)) {
			pr_perror("netns helper: setns into %d", req->ns_pid);
			goto child_err;
		}

		if (target_owned)
			close(target_fd);
		target_fd = -1;

		ret = prep_netns_sockets_in_child(req, &nlsk, &seqsk);
		if (ret)
			goto child_err;

		nr_fds = 0;
		if (req->for_dump)
			fds[nr_fds++] = nlsk;
		fds[nr_fds++] = seqsk;

		msg.ret = 0;
		if (send_fds(sk[1], NULL, 0, fds, nr_fds, &msg, sizeof(msg)))
			goto child_err;

		close(sk[1]);
		_exit(0);

child_err:
		msg.ret = -1;
		send_fds(sk[1], NULL, 0, NULL, 0, &msg, sizeof(msg));
		close(sk[1]);
		if (target_owned && target_fd >= 0)
			close(target_fd);
		_exit(1);
	}

	close(sk[1]);

	nr_fds = req->for_dump ? 2 : 1;
	if (recv_fds(sk[0], fds, nr_fds, &msg, sizeof(msg)) < 0) {
		pr_perror("netns helper: recv fds");
		close(sk[0]);
		waitpid(pid, &status, 0);
		return -1;
	}

	close(sk[0]);

	if (waitpid(pid, &status, 0) < 0) {
		pr_perror("netns helper: waitpid");
		return -1;
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || msg.ret) {
		int i;

		pr_err("netns helper: child failed (exit %d, ret %d)\n",
		       WIFEXITED(status) ? WEXITSTATUS(status) : -1, msg.ret);
		for (i = 0; i < nr_fds; i++)
			if (fds[i] >= 0)
				close(fds[i]);
		return -1;
	}

	if (req->for_dump) {
		resp->nlsk = fds[0];
		resp->seqsk = fds[1];
	} else {
		resp->seqsk = fds[0];
	}

	pr_info("Prepared netns sockets via helper for pid %d\n", req->ns_pid);
	return 0;
}
