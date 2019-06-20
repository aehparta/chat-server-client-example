
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>


#define SERVER_TCP_PORT 1717
#define MAX_CLIENTS 2

int server_fd = -1;
int clients[MAX_CLIENTS];
fd_set r_fds, w_fds;
int max_fd = -1;
int motd_fd = -1;


int p_init(int argc, char *argv[])
{
	int opt;
	struct sockaddr_in addr;

	/* init stuff */
	FD_ZERO(&r_fds);
	FD_ZERO(&w_fds);
	for (opt = 0; opt < MAX_CLIENTS; opt++) {
		clients[opt] = -1;
	}

	/* open welcome message file descriptor */
	if (argc > 1) {
		int fd = open(argv[1], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "welcome message file could not be opened\n");
		} else {
			motd_fd = fd;
		}
	}

	/* create and setup tcp socket */
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		fprintf(stderr, "socket() failed\n");
		return -1;
	}
	FD_SET(server_fd, &r_fds);
	max_fd = server_fd > max_fd ? server_fd : max_fd;
	/* enable reuse */
	opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		fprintf(stderr, "setsockopt() failed\n");
		return -1;
	}

	/* bind to specific port */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(SERVER_TCP_PORT);
	if (bind(server_fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "bind() failed\n");
		return -1;
	}

	/* start to listen */
	if (listen(server_fd, 5) < 0) {
		fprintf(stderr, "listen() failed\n");
		return -1;
	}

	return 0;
}

void p_quit(int retval)
{
	close(server_fd);
	exit(retval);
}

int p_new_client(int fd)
{
	FD_SET(fd, &r_fds);
	max_fd = fd > max_fd ? fd : max_fd;

	/* send welcome message if file is open */
	if (motd_fd >= 0) {
		char byte;
		lseek(motd_fd, 0, SEEK_SET);
		while (read(motd_fd, &byte, 1) == 1) {
			write(fd, &byte, 1);
		}
		write(fd, "\n", 1);
	}

	fprintf(stderr, "server accepted new connection\n");
	return 0;
}

int p_recv(int fd)
{
	int n;
	char line[1500];
	struct sockaddr_in addr;
	socklen_t l = sizeof(addr);

	/* if new connection */
	if (fd == server_fd) {
		n = accept(server_fd, (struct sockaddr *)&addr, &l);
		if (n >= 0) {
			for (int i = 0; i < MAX_CLIENTS; i++) {
				if (clients[i] < 0) {
					clients[i] = n;
					p_new_client(n);
					return 0;
				}
			}
			/* no room for new client */
			dprintf(n, "server is full\n");
			close(n);
			fprintf(stderr, "client denied, server is full\n");
			return -1;
		}
		fprintf(stderr, "accept() failed\n");
		return -1;
	}

	/* receive data */
	memset(line, 0, sizeof(line));
	n = recv(fd, line, sizeof(line), 0);
	if (n <= 0) {
		/* connection was closed */
		fprintf(stderr, "client disconnected\n");
		close(fd);
		FD_CLR(fd, &r_fds);
		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (fd == clients[i]) {
				clients[i] = -1;
				break;
			}
		}
	} else {
		/* we got some data from a client, echo to others */
		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (fd != clients[i] && clients[i] > 0) {
				send(clients[i], line, n, 0);
			}
		}
	}

	return 0;
}

void p_run(void)
{
	int err, i;

	while (1) {
		fd_set r = r_fds;
		fd_set w = w_fds;

		err = select(max_fd + 1, &r, &w, NULL, NULL);
		if (err < 0) {
			fprintf(stderr, "select() failed, reason: %s", strerror(errno));
			break;
		}
		for (i = 0; i <= max_fd; i++) {
			/* check if read fd is set */
			if (FD_ISSET(i, &r)) {
				p_recv(i);
			}
			/* check if write fd is set */
			if (FD_ISSET(i, &w)) {
			}
		}
	}
}

int main(int argc, char *argv[])
{
	if (p_init(argc, argv)) {
		return EXIT_FAILURE;
	}

	while (1) {
		p_run();
	}

	p_quit(EXIT_SUCCESS);
	return EXIT_SUCCESS;
}
