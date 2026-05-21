#ifndef __CR_NETNS_HELPER_H__
#define __CR_NETNS_HELPER_H__

#include <stdbool.h>
#include <sys/types.h>

#include "cr_options.h"

struct ns_id;

struct netns_helper_req {
	pid_t ns_pid;
	int ns_fd; /* >= 0: use this fd; else open /proc/<ns_pid>/ns/net */
	bool for_dump;
	int root_pid; /* SELinux: label seqsk like this task */
};

struct netns_helper_resp {
	int nlsk; /* SOCK_DIAG, or -1 */
	int seqsk;
};

static inline bool netns_helper_needed(void)
{
	return opts.unprivileged;
}

extern int netns_helper_open_target(const struct ns_id *ns, int *out_fd);
extern int netns_helper_prep(const struct netns_helper_req *req, struct netns_helper_resp *resp);

#endif /* __CR_NETNS_HELPER_H__ */
