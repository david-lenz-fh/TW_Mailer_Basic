#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 5050
#define BACKLOG 5

int create_socket = -1;
int new_socket = -1;
int abortRequested = 0;

void signalHandler(int sig);

int main() {
  struct sockaddr_in serverInfo = {0}; // gegen bind fail
  struct sockaddr_in clientInfo = {0};
  socklen_t clientSize = sizeof(clientInfo);

  serverInfo.sin_family = AF_INET;
  serverInfo.sin_addr.s_addr = 0; // bind to every ip the pc owns
  serverInfo.sin_port = htons(PORT);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    perror("socket failed");
    return -1;
  }

  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

  if (bind(fd, (struct sockaddr *)&serverInfo, sizeof(serverInfo)) == -1) {
    perror("bind");
    close(fd);
    return -1;
  }

  if (listen(fd, BACKLOG) == -1) {
    perror("listen");
    close(fd);
    return -1;
  }
  while (1) {
    int cfd =
        accept(fd, (struct sockaddr *)&clientInfo, &clientSize); // client fd
    if (cfd == -1) {
      perror("accept");
      close(cfd);
      return -1;
    }
    printf("Client connected\n");

    char buffer[1024];

    int size = recv(cfd, buffer, sizeof(buffer) - 1, 0);
    if (size > 0) {
      buffer[size] = '\0';
      printf("%s \n", buffer);
      send(cfd, "OK\n", 3, 0);
    }
    close(cfd);
  }
}

void signalHandler(int sig) {
  if (sig == SIGINT) {
    printf("Abort requested\n");
    abortRequested = 1;

    if (new_socket != -1) {
      if (shutdown(new_socket, SHUT_RDWR) == -1) {
        perror("shutdown new_socket");
      }
      if (close(new_socket) == -1) {
        perror("close new_socket");
      }
      new_socket = -1;
    }

    if (create_socket != -1) {
      if (shutdown(create_socket, SHUT_RDWR) == -1) {
        perror("shutdown create_socket");
      }
      if (close(create_socket) == -1) {
        perror("close create_socket");
      }
      create_socket = -1;
    }

    exit(0);
  } else {
    exit(sig);
  }
}
