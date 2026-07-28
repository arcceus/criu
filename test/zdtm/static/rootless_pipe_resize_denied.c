#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#include "zdtmtst.h"

const char *test_doc = "Check pipe data survives C/R when pipe resize is denied";
const char *test_author = "Deepak Anand <deepakanand1300@gmail.com>";

#define PIPE_SIZE (1 << 20)
#define DATA_SIZE 4096

int main(int argc, char **argv)
{
	uint8_t buf[DATA_SIZE];
	uint32_t crc = ~0U;
	int p[2];
	int ret = 1;

	test_init(argc, argv);

	if (pipe2(p, O_NONBLOCK)) {
		pr_perror("pipe");
		return 1;
	}

	if (fcntl(p[1], F_SETPIPE_SZ, PIPE_SIZE) == -1) {
		pr_perror("Unable to change pipe size");
		goto out;
	}

	datagen(buf, sizeof(buf), &crc);

	if (write(p[1], buf, sizeof(buf)) != sizeof(buf)) {
		pr_perror("write");
		goto out;
	}

	test_daemon();
	test_waitsig();

	if (read(p[0], buf, sizeof(buf)) != sizeof(buf)) {
		pr_perror("read");
		goto out;
	}

	crc = ~0U;
	if (datachk(buf, sizeof(buf), &crc)) {
		fail("pipe data corrupted after C/R");
		goto out;
	}

	pass();
	ret = 0;

out:
	close(p[0]);
	close(p[1]);
	return ret;
}
