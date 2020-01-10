#include <iostream>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <sys/epoll.h>
#include <vector>
#include <map>
#include <stdlib.h>
/*local client*/
#define PROXY_PORT 	22
#define PROXY_IP   	"192.168.0.148"
/*
 *local server:
 *local ssh -> local server
 *local server rev data -> local client
 *local client -> remoter server
*/
#define LOCAL_SSH_SERVER_PORT	10416
#define LOCAL_SSH_SERVER_IP	"192.168.0.148"

#define PACKET_BUFFER_SIZE 8192
#define EPOLL_BUFFER_SIZE 256
#define SOMAXCONN 128

typedef struct proxyLink
{
	int src_fd;
	int tar_fd;
}proxyLink;


char buf[PACKET_BUFFER_SIZE];

int main(void)
{
	std::map<int,struct proxyLink *>m_link;
	std::vector<proxyLink>m_proxyLink;
	struct sockaddr_in src_addr,cli_addr;
	const int flag = 1;
	int sockfd;
	int tar_sock_fd;

	src_addr.sin_family = AF_INET;
    src_addr.sin_port = htons(LOCAL_SSH_SERVER_PORT);
	src_addr.sin_addr.s_addr = inet_addr(LOCAL_SSH_SERVER_IP);

	sockfd = socket(AF_INET,SOCK_STREAM,0);

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0) {
		return -1; 
	}
 	if (bind(sockfd,(const struct sockaddr*)&src_addr, sizeof(struct sockaddr_in)) < 0) { 
        std::cout << "bind fault : " << strerror(errno) <<  std::endl;
		return -1; 
	}
    if (listen(sockfd, SOMAXCONN) < 0) { 
		return -1; 
	}

	struct epoll_event ev, events[20];
	int epollfd = epoll_create(5);
	ev.data.fd = sockfd;
	ev.events = EPOLLIN;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev);

	while (1) {
		int connfd;
		int count = epoll_wait(epollfd, events, 20, -1);
		for (int i=0;i<count;i++) {
			if (events[i].data.fd == sockfd) {
				int len = sizeof(struct sockaddr_in);
				connfd = accept(sockfd, (struct sockaddr *)&cli_addr,(socklen_t*)&len);

				proxyLink *proxyLink ;
				proxyLink = (struct proxyLink *)malloc(sizeof(struct proxyLink));
				if(connfd < 0)
				{
					perror("accept error");
					continue;
				}
				proxyLink->src_fd = connfd;
 
				printf("connection from %s, port is %d\n", inet_ntop(AF_INET,&cli_addr.sin_addr,buf,sizeof(buf)),ntohs(cli_addr.sin_port));
				ev.events = EPOLLIN;
				ev.data.fd = connfd;
				if(epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &ev) < 0)
				{
					fprintf(stderr, "epoll set insertion error: fd = %d\n", connfd);
					return -1;
				}

				tar_sock_fd = socket(AF_INET,SOCK_STREAM,0);
				struct sockaddr_in tar_addr;
				tar_addr.sin_family = AF_INET;
    			tar_addr.sin_port = htons(PROXY_PORT);
    			if (inet_pton(AF_INET, PROXY_IP, &tar_addr.sin_addr) <= 0) { 
					return false; 
				}
				if(connect(tar_sock_fd,(const struct sockaddr*)&tar_addr, sizeof(struct sockaddr_in)) < 0) { 
					return -1; 
				}
				proxyLink->tar_fd = tar_sock_fd;
				ev.data.fd = tar_sock_fd;
				ev.events = EPOLLIN;
				epoll_ctl(epollfd, EPOLL_CTL_ADD, tar_sock_fd, &ev);
				
				m_link.insert(std::pair<int,struct proxyLink *>{connfd,proxyLink});
			}else {
				int ret;
				int sock_fd = events[i].data.fd;
				if (events[i].events & EPOLLIN) {
					if (sock_fd < 0) continue;
					if ((ret = recv(sock_fd,buf,sizeof(buf),0)) < 0) {
						printf("terminated break from port %d\n", ntohs(cli_addr.sin_port));
						close(sock_fd);
						sock_fd = -1;
						return false;
					}else if (ret == 0) {
						printf("terminated close from port %d\n", ntohs(cli_addr.sin_port));
						if (m_link.find(sock_fd)!=m_link.end()) {
							printf("m_link delete behind size =%d\n",m_link.size());
							free(m_link.find(sock_fd)->second);
							m_link.erase(sock_fd);
							printf("m_link delete fd=%d\n",sock_fd);
							printf("m_link delete before size =%d\n",m_link.size());						
						}
						close(sock_fd);
						sock_fd = -1;
					}else {
						// printf("sockfd=%d,read %d characters:", sock_fd,ret - 1);
						// for (int i=0;i<ret;i++) {
						// 	printf("%c",buf[i]);
						// }
						// printf("\n");
						ev.data.fd = sock_fd;
						ev.events = EPOLLOUT;
						epoll_ctl(epollfd, EPOLL_CTL_MOD, sock_fd, &ev);
					}
				}
				if (events[i].events & EPOLLOUT) {
					std::map<int,struct proxyLink *>::iterator iter;
					// std::cout << "m_link size = " << m_link.size() << std::endl;
					for (iter = m_link.begin();iter!=m_link.end();iter++) {
						if (iter->second->src_fd == sock_fd) {
							send(iter->second->tar_fd,buf,ret,0);
							ret = 0;
							bzero(buf,sizeof(buf));
							struct epoll_event ev;
							ev.data.fd = sock_fd;
							ev.events = EPOLLIN;
							epoll_ctl(epollfd, EPOLL_CTL_MOD, sock_fd, &ev);
						}
						else if (iter->second->tar_fd == sock_fd) {
							send(iter->second->src_fd,buf,ret,0);
							ret = 0;
							bzero(buf,sizeof(buf));
							struct epoll_event ev;
							ev.data.fd = sock_fd;
							ev.events = EPOLLIN;
							epoll_ctl(epollfd, EPOLL_CTL_MOD, sock_fd, &ev);
						}
					}
				}
			}
		}
	}
}




