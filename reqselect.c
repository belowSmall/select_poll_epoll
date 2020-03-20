#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <errno.h>
#include <fcntl.h>

#define BUFFER_LENGTH	1024

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

	fd_set rfds, rset; // rfds 用于保存 rset 用于操作

	FD_ZERO(&rfds);
	FD_SET(sockfd, &rfds);

	int max_fd = sockfd;
	int i = 0;

	while (1) {
		rset = rfds;

		// @param 5  struct timeval *timeout
		// 1、若将NULL以形参传入，即不传入时间结构，就是将select置于阻塞状态，一定等到监视文件描述符集合中某个文件描述符发生变化为止
		// 2、若将时间值设为0秒0毫秒，就变成一个纯粹的非阻塞函数，不管文件描述符是否有变化，都立刻返回继续执行，文件无变化返回0，有变化返回一个正值
		// 3、timeout的值大于0，这就是等待的超时时间，即select在timeout时间内阻塞，超时时间之内有事件到来就返回了，否则在超时后不管怎样一定返回，返回值同上述
		int nready = select(max_fd + 1, &rset, NULL, NULL, NULL); // 错误返回-1 超时返回0 返回发生时间的fd数
		if (nready < 0) { // 错误
			printf("select error : %d\n", errno);
			continue;
		}

		if (FD_ISSET(sockfd, &rset)) {
			struct sockaddr_in client_addr;
			bzero(&client_addr, sizeof(struct sockaddr_in));
			socklen_t client_len = sizeof(client_addr);

			int clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
			if (clientfd <= 0) continue;

			char str[INET_ADDRSTRLEN] = {0}; // #define INET_ADDRSTRLEN 16
			// inet_ntop (ipv4 ipv6) 将数值格式转化为点分十进制的ip地址格式
			printf("recvived from %s at port %d, sockfd:%d, clientfd:%d\n", inet_ntop(AF_INET, &client_addr.sin_addr, str, sizeof(str)), ntohs(client_addr.sin_port), sockfd, clientfd);

			if (max_fd == FD_SETSIZE) { // 超出最大值
				printf("clientfd --> out range\n");
				break;
			}

			FD_SET(clientfd, &rfds); // 设置clientfd  本次循环与clientfd无关 下次循环有关

			if (clientfd > max_fd) max_fd = clientfd;

			printf("sockfd:%d, max_fd:%d, clientfd:%d\n", sockfd, max_fd, clientfd);

			if (--nready == 0) continue;  // sockfd
		}

		for (i = sockfd + 1; i <= max_fd; i++) { // 检查每个fd(除了sockfd)
			if (FD_ISSET(i, &rset)) { // rset 是 rfds 子集
				char buffer[BUFFER_LENGTH] = {0};
				int ret = recv(i, buffer, BUFFER_LENGTH, 0);
				if (ret < 0) {
					if (errno == EAGAIN || errno == EWOULDBLOCK) { // 在 ret < 0 的情况下的 errno == EAGAIN || errno == EWOULDBLOCK 连接正常 继续接收
						printf("read all data");
					}
					FD_CLR(i, &rfds);
					close(i);
				} else if (ret == 0) { // 连接断开
					printf("disconnect %d\n", i);
					FD_CLR(i, &rfds);
					close(i);
					break;
				} else {
					printf("Recv: %s, %d Bytes\n", buffer, ret);
				}
				if (--nready == 0) break; // i
			}
		}
	}
	return 0;
}