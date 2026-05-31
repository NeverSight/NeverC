#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 0  /* Let the OS pick an available port */
#define BACKLOG 5

static int run_server(int *out_port) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    return -1;
  }

  int opt = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(PORT);

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(sockfd);
    return -1;
  }

  socklen_t addrlen = sizeof(addr);
  getsockname(sockfd, (struct sockaddr *)&addr, &addrlen);
  *out_port = ntohs(addr.sin_port);

  if (listen(sockfd, BACKLOG) < 0) {
    perror("listen");
    close(sockfd);
    return -1;
  }

  return sockfd;
}

static int connect_to(int port) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket (client)");
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((uint16_t)port);

  if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    close(sockfd);
    return -1;
  }

  return sockfd;
}

int main(void) {
  printf("NeverC Linux network demo\n");
  printf("=========================\n\n");

  int server_port;
  int server_fd = run_server(&server_port);
  if (server_fd < 0)
    return 1;
  printf("Server listening on 127.0.0.1:%d\n", server_port);

  int client_fd = connect_to(server_port);
  if (client_fd < 0) {
    close(server_fd);
    return 1;
  }
  printf("Client connected\n");

  struct sockaddr_in peer_addr;
  socklen_t peer_len = sizeof(peer_addr);
  int accepted_fd = accept(server_fd, (struct sockaddr *)&peer_addr, &peer_len);
  if (accepted_fd < 0) {
    perror("accept");
    close(client_fd);
    close(server_fd);
    return 1;
  }
  printf("Server accepted connection from port %d\n\n", ntohs(peer_addr.sin_port));

  const char *messages[] = {
    "Hello from NeverC!",
    "Cross-compiled socket demo",
    "TCP echo test",
  };

  for (int i = 0; i < 3; ++i) {
    ssize_t sent = send(client_fd, messages[i], strlen(messages[i]), 0);
    printf("  Client sent: \"%s\" (%zd bytes)\n", messages[i], sent);

    char buf[256];
    ssize_t recvd = recv(accepted_fd, buf, sizeof(buf) - 1, 0);
    if (recvd > 0) {
      buf[recvd] = '\0';
      printf("  Server recv: \"%s\" (%zd bytes)\n\n", buf, recvd);
    }
  }

  close(accepted_fd);
  close(client_fd);
  close(server_fd);

  printf("Done.\n");
  return 0;
}
