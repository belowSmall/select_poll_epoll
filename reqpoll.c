#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/poll.h>

#define BUFFER_LENGTH	1024
#define POLL_SIZE		1024

int main(int argc, char *argv[]) {
	if (argc < 2) return -1;

	int port = atoi(argv[1]); // 字符串

	int sockfd = socket(AF_INET, SOCK_STREAM, 0); // 基于ipv4寻址、面向字节流
	if (sockfd < 0) {
		perror("socket");
		return -2;
	}

	struct sockaddr_in addr;
	bzero(&addr, sizeof(struct sockaddr_in));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY; // 0x00000000

	if (bind(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
		perror("bind");
		return -3;
	}

	if (listen(sockfd, SOMAXCONN)) { // SOMAXCONN 默认128 (cat /proc/sys/net/core/somaxconn)
		perror("listen");
		return -4;
	}

	/*
		struct pollfd
		{
			int fd;
			short int events; // 关心的事件类型
			short int revents; // 返回的关心的事件的类型(用于判断)
		};
	*/
	struct pollfd fds[POLL_SIZE] = {0};
	fds[0].fd = sockfd;     // 设置sockfd  FD_SET(sockfd, &..)
	fds[0].events = POLLIN;

	int max_fd = 0;
	int i = 0;
	for (i = 1; i < POLL_SIZE; i++) { // FD_ZERO
		fds[i].fd = -1;
	}

	while (1) {
		// int poll (struct pollfd *__fds, nfds_t __nfds, int __timeout)
		int nready = poll(fds, max_fd + 1, 5);
		if (nready <= 0) continue;

		if ((fds[0].revents & POLLIN) == POLLIN) { // sockfd --> fds[0].fd  ==> FD_ISSET(sockfd, &..))
			struct sockaddr_in client_addr;
			bzero(&client_addr, sizeof(sockaddr_in));
			socklen_t client_len = sizeof(client_addr);

			int clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
			if (clientfd <= 0) continue;

			char str[INET_ADDRSTRLEN] = {0};
			printf("recvived from %s at port %d, sockfd:%d, clientfd:%d\n", inet_ntop(AF_INET, &client_addr.sin_addr, str, sizeof(str)), ntohs(client_addr.sin_port), sockfd, clientfd);

			fds[clientfd].fd = clientfd;
			fds[clientfd].events = POLLIN; // FD_SET(clientfd, &..)

			if (clientfd > max_fd) max_fd = clientfd;
			if (--nready == 0) continue;
		}

		for (i = sockfd + 1; i <= max_fd; i++) {
			if (fds[i].revents & (POLLIN | POLLERR)) {
				char buffer[BUFFER_LENGTH] = {0};
				if (ret < 0) {
					if (errno == EAGAIN || errno == EWOULDBLOCK) {
						printf("read all data");
					}

					close(i);
					fds[i].fd = -1;
				} else if (ret == 0) {
					printf("disconnect %d\n", i);

					close(clientfd);
					fds[i].fd = -1;  // FD_CLR
					break;
				} else {
					printf("Recv: %s, %d Bytes\n", buffer, ret);
				}
				if (--nready == 0) break;
			}
		}
	}
	return 0;
}