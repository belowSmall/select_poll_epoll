#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define BUFFER_LENGTH	1024
#define EPOLL_SIZE		1024

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

	int epoll_fd = epoll_create(EPOLL_SIZE);  // 创建一个epoll_fd
	struct epoll_event ev, events[EPOLL_SIZE] = {0};  // ev 临时使用(epoll_ctl)  events(epoll_wait)

	/*
		typedef union epoll_data
		{
			void *ptr;
			int fd;
			uint32_t u32;
			uint64_t u64;
		} epoll_data_t;

		struct epoll_event
		{
  			uint32_t events;
  			epoll_data_t data;
		}
	*/

	ev.events = EPOLLIN;
	ev.data.fd = sockfd;  // ev 服务器 sockfd

	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev); // 类似 FD_SET

	while (1) {
		int nready = epoll_wait(epoll_fd, events, EPOLL_SIZE, -1); // 类似 select  (nready和events返回就绪的 没就绪的不返回)
		if (nready == -1) {
			printf("epoll_wait\n");
			break;
		}

		int i = 0;
		for (i = 0; i < nready; i++) {
			if (events[i].data.fd == sockfd) {
				struct sockaddr_in client_addr;
				bzero(&client_addr, sizeof(struct sockaddr_in));
				socklen_t client_len = sizeof(client_addr);

				int clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
				if (clientfd <= 0) continue;

				char str[INET_ADDRSTRLEN] = {0};
				printf("recvived from %s at port %d, sockfd:%d, clientfd:%d\n", inet_ntop(AF_INET, &client_addr.sin_addr, str, sizeof(str)), ntohs(client_addr.sin_port), sockfd, clientfd);

				ev.events = EPOLLIN | EPOLLET; // 边缘触发
				ev.data.fd = clientfd;
				epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientfd, &ev); // 将clientfd添加到epoll_fd里
			} else { // 客户端fd
				int clientfd = events[i].data.fd;

				char buffer[BUFFER_LENGTH] = {0};
				int ret = recv(clientfd, buffer, BUFFER_LENGTH, 0);
				if (ret < 0) {
					if (errno == EAGAIN || errno == EWOULDBLOCK) {
						printf("read all data");
					}

					close(clientfd);

					ev.events =  EPOLLIN | EPOLLET;
					ev.data.fd = clientfd;
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, clientfd, &ev); // FD_CLR(i, &..);
				} else if (ret == 0) {
					printf("disconnect %d\n", clientfd);

					close(clientfd);

					ev.events = EPOLLIN | EPOLLET;
					ev.data.fd = clientfd;
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, clientfd, &ev);

					break;
				} else {
					printf("Recv: %s, %d Bytes\n", buffer, ret);
				}
			}
		}
	}
	return 0;
}