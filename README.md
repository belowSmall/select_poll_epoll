> 在Unix下有五种IO模型：

> **1.阻塞IO**

> **2.非阻塞IO**

> **3.IO多路复用**

> **4.信号驱动IO**

> **5.异步IO**

本文描述的是**IO多路复用**。IO多路复用的本质就是让一个进程来监视多个fd，当某个fd就绪，就通知应用程序。

在Linux下有**select**、**poll** 和 **epoll** 这三种IO复用方式
它们的诞生时间，最早是 **select**，然后是 **poll**，最后是 **epoll**

## select
### 性能
```select``` 将 **整个** ```fd_set``` 集合从用户态拷贝到内核态，再从内核态拷贝回用户态，这是相当耗性能的。

而为什么推荐用```epoll``` ? 因为它只是将就绪的 ```fd``` 通知给用户程序（回调）
```epoll``` 是通过内核与用户空间 ```mmap``` 同一块内存，避免了无谓的内存拷贝。

### 函数原型
```c
int select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
```
**@param1：要监听的fd的最大值。（一般是当前fd + 1）** fd是int型的正整数，并且是往上增长的。假如你当前最大的```fd = 5```，那么第一个参数就是``` 5 + 1 ```。
**@param2：关心读的集合**
**@param3：关心写的集合**
**@param4：关心出错的集合** （先讲第5个参数，再讲第2、3、4个参数）

**@param5：超时时间** 1. 若传入NULL，将select设为阻塞状态，等到监视的某个fd集合发生变化为止。 2. 若设为0秒0毫秒，就是非阻塞，无论监视的fd是否有变化，都立即返回，无变化返回0，有变化返回一个正整数。 3. 若这个值大于0，则select在时间里阻塞，在时间内有事件到来就返回，否则超时后不管怎样都返回。
*  **@param5 结构体**
```c
struct timeval
{
    time_t tv_sec;  // 秒
    time_t tv_usec; // 毫秒
};
```

现在回过头来讲第2、3、4个参数
```fd_set  set```  先声明 fd 集合，再用```FD_SET（int fd, fd_set*set）``` 设置关心的 fd ，再传入 ```select``` 的第2、3 或 4个参数。当 ```select``` 返回的时候，再用 ```FD_ISSET(int fd,fd_set *set)``` 作判断（是否设置了关心的fd），再做关心的操作

> * FD_ZERO(fd_set *set)；         清除描述词组set的全部位
> * FD_SET(int fd,fd_set*set)；    设置描述词组set中相关fd的位
> * FD_ISSET(int fd,fd_set *set)；判断描述词组set中相关fd的位是否为真
> * FD_CLR(int fd,fd_set* set)；   清除描述词组set中相关fd的位

> **```fd_set``` 集合是一个数组，数组下标是 ```fd``` ，当某个 ```fd``` 就绪，就把值置为 ```1```**

下面是完成的服务器代码，用```select```实现多路复用
客户端可以使用 **NetAssist**，自行百度
```c
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
```
---
---
## poll
### poll优势（相对于select）
```poll``` 将 ```select``` 的三个 ```fd_set``` 合并为一个。函数原型：
```c
int poll(struct poll_fd *fds, nfds_t  nfds, int timeout)
```
```fds``` 就是三个 ```fd_set``` 合并后的。
```nfds``` 是最大fd + 1，同 ```select``` 的第一个参数。
```timeout``` 超时时间。在时间内阻塞，超时或者在时间内有就绪，就返回。

返回值：
（1）小于0，出错
（2）等于0，等待超时
（3）大于0，监听的fd就绪返回，并且返回结果就绪的fd的个数

### 结构体 poll_fd
```c
struct pollfd
{
    int fd;
    short int events; // 关心的事件类型
    short int revents; // 返回的关心的事件的类型(用于判断)
};
```
设置示例：(```events``` 用于设置)
```c
struct pollfd fds[POLL_SIZE] = {0};
fds[0].fd = sockfd;     // 设置sockfd  FD_SET(sockfd, &..);
fds[0].events = POLLIN; // 关心数据可读

for (i = 1; i < POLL_SIZE; i++) { // 类似 FD_ZERO   清零
    fds[i].fd = -1;
}
```
```poll``` 函数返回之后：
```events``` 用于设置
```c
fds[clientfd].fd = clientfd;  // 设置 客户端 连接过来的fd
fds[clientfd].events = POLLIN; // FD_SET(clientfd, &..)   关心数据可读
```
```revents``` 用于判断
```c
if ((fds[0].revents & POLLIN) == POLLIN) { ... }  // 对读关心

if (fds[i].revents & (POLLIN | POLLERR)) { ... }  // 对读和出错关心
```

* **事件定义**

| 事件|描述|是否可作为输入|是否可作为输出|
|:--------:|:--------:|:--------:|:--------:|
|POLLIN|数据可读（包括普通数据&优先数据）|是|是|
|POLLOUT|数据可写（普通数据&优先数据）|是|是|
|POLLRDNORM|普通数据可读|是|是|
|POLLRDBAND|优先级带数据可读（linux不支持）|是|是|
|POLLPRI|高优先级数据可读，比如TCP带外数据|是|是|
|POLLWRNORM|普通数据可写|是|是|
|POLLWRBAND|优先级带数据可写|是|是|
|POLLRDHUP|TCP连接被对端关闭，或者关闭了写操作，由GNU引入|是|是|
|POPPHUP|挂起|否|是|
|POLLERR|错误|否|是|
|POLLNVAL|文件描述符没有打开|否|是|

(转自csdn Shining-LY)

下面是完成的服务器代码，用```poll```实现多路复用
客户端可以使用 **NetAssist**，自行百度
```c
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
```
---
---
## epoll
### epoll优势（相对于select）
上面讲select的时候已经说过了
```select``` 将 **整个** ```fd_set``` 集合从用户态拷贝到内核态，再从内核态拷贝回用户态，这是相当耗性能的。

而为什么推荐用```epoll``` ? 因为它只是将就绪的 ```fd``` 通知给用户程序（回调）
```epoll``` 是通过内核与用户空间 ```mmap``` 同一块内存，避免了无谓的内存拷贝。

**```epoll```最大的好处在于它不会随着监听fd数目的增长而降低效率**

> * ```epoll``` 的写法和 上面的 ```select``` ```poll``` 稍微有点不同

```epoll``` 用到三个函数
```c
int epoll_create(int size)
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
int epoll_wait(int epid, struct epoll_event *events, int maxevents, int timeout)
```
1. ```int epoll_create(int size)``` 创建一个epoll对象，返回 ```epoll_fd```
这个参数有点意思。在之前旧版本中 ```size``` 用来告诉内核这个监听的数目一共有多大。在新版本中，```size``` 只需填一个大于0的数即可。填1或者填10000，效果是一样的。
2. ```int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)```
从 ```epfd``` 中添加或删除(```op```) ```fd``` 的 ```event``` 事件
第一个参数是```epoll_create``` 返回的 ```epoll_fd```
第二个参数是你需要对 ```epoll_fd``` 做的操作（例如：```EPOLL_CTL_ADD``` 和  ```EPOLL_CTL_DEL``` 等）
第三个参数是 ```socket_fd```（用 ```socket``` 函数创建的 ```fd```  ）
第四个参数是一个结构体
```c
struct epoll_event
{
    uint32_t events;
    epoll_data_t data;
}
```
比如将服务器的 ```server_fd``` 添加到 ```epoll``` 监听中：
```c
struct epoll_event ev = {0};
ev.events = EPOLLIN;  // 关心 in 事件
ev.data.fd = server_fd;  // ev 服务器 server_fd
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev); // 类似 FD_SET
```
比如将客户器的 ```client_fd``` 添加到 ```epoll``` 监听中：
```c
struct epoll_event ev = {0};
ev.events = EPOLLIN | EPOLLET;  // 关心 in 事件    使用边缘触发    水平触发（EPOLLLT）
ev.data.fd = client_fd;  // ev 服务器 client_fd
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev); // 类似 FD_SET
```
3. ```int epoll_wait(int epid, struct epoll_event *events, int maxevents, int timeout)```
等待系统调用的返回
第一个参数是```epoll_create``` 返回的 ```epoll_fd```
第二个参数是 ```events```：存放就绪的事件集合，传出参数
第三个参数是 ```maxevents```：可以存放的事件个数，```events``` 数组的大小
第四个参数是 ```timeout```：阻塞等待的时间长短(毫秒)，如果传入 -1 代表阻塞等待

下面是完成的服务器代码，用```epoll```实现多路复用
客户端可以使用 **NetAssist**，自行百度
```c
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
```
---
select: 2020.3.15  22:50  广州
poll: 2020.3.16  23:01  广州
epoll: 2020.3.20  23:17  广州
