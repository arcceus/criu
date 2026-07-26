#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "zdtmtst.h"

const char *test_doc = "Check rootless restore into a runtime-provided user namespace";
const char *test_author = "Deepak Anand <deepakanand1300@gmail.com>";

int main(int argc, char **argv)
{
	char c = 'R';
	int client, accepted;
	int port = 23456;
	int server = -1;

	test_init(argc, argv);

	if (system("ip link set lo up")) {
		fail("Can't set lo up");
		return 1;
	}

	if (system("ip addr add 1.2.3.4 dev lo")) {
		fail("Can't add addr on lo");
		return 1;
	}

	if (system("ip route add 1.2.3.5 dev lo")) {
		fail("Can't add route via lo");
		return 1;
	}

	if (system("ip route add 1.2.3.6 via 1.2.3.5")) {
		fail("Can't add route via lo (2)");
		return 1;
	}

	server = tcp_init_server(AF_INET, &port);
	if (server < 0) {
		fail("Can't create TCP server");
		return 1;
	}

	if (system("ip link > rootless_runtime_userns.dump.test && ip addr >> rootless_runtime_userns.dump.test && ip route >> rootless_runtime_userns.dump.test")) {
		fail("Can't save net config");
		return 1;
	}

	test_daemon();
	test_waitsig();

	if (system("ip link > rootless_runtime_userns.rst.test && ip addr >> rootless_runtime_userns.rst.test && ip route >> rootless_runtime_userns.rst.test")) {
		fail("Can't get net config");
		return 1;
	}

	if (system("diff rootless_runtime_userns.rst.test rootless_runtime_userns.dump.test")) {
		fail("Net config differs after restore");
		return 1;
	}

	client = tcp_init_client(AF_INET, "127.0.0.1", port);
	if (client < 0) {
		fail("Can't connect to restored TCP server");
		return 1;
	}

	accepted = tcp_accept_server(server);
	if (accepted < 0) {
		fail("Can't accept on restored TCP server");
		return 1;
	}

	if (write(client, &c, sizeof(c)) != sizeof(c)) {
		fail("Can't write to restored TCP connection");
		return 1;
	}

	c = '\0';
	if (read(accepted, &c, sizeof(c)) != sizeof(c)) {
		fail("Can't read from restored TCP connection");
		return 1;
	}

	if (c != 'R') {
		fail("Unexpected byte from restored TCP connection: %c", c);
		return 1;
	}

	close(accepted);
	close(client);
	close(server);

	pass();
	return 0;
}
