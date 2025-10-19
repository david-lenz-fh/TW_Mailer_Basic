#include <arpa/inet.h> // für inet_aton(), inet_ntoa()
#include <errno.h>
#include <netinet/in.h> // für struct sockaddr_in
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h> // für socket(), connect(), etc.
#include <unistd.h>     // für close()

#define BUF 1024

static void die(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

static ssize_t send_all(int sock, const void *buf, size_t len) {
  const char *p = buf;
  size_t total = 0;

  while (total < len) {
    ssize_t n = send(sock, p + total, len - total, 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    total += (size_t)n;
  }
  return (ssize_t)total;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Wrong arguments %s\n", argv[0]);
    return EXIT_FAILURE;
  }
  char *ip = argv[1];
  int port = atoi(argv[2]);

  int sockfd;
  char buffer[BUF];
  struct sockaddr_in server;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    die("socket");
  }

  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_port = htons(port);

  if (!inet_aton(ip, &server.sin_addr)) {
    die("invalid IP address");
  }

  if (connect(sockfd, (struct sockaddr *)&server, sizeof(server)) == -1) {
    die("connect");
  }

  printf("Connection with server (%s) established\n",
         inet_ntoa(server.sin_addr));

  ssize_t size = recv(sockfd, buffer, BUF - 1, 0);
  if (size > 0) {
    buffer[size] = '\0';
    printf("%s", buffer);
  }

  int isQuit = 0;

  while (!isQuit) {
    printf(">>");
    fflush(stdout);

    if (!fgets(buffer, BUF, stdin))
      break;
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
      buffer[len - 1] = '\0';

    if (strcasecmp(buffer, "quit") == 0) {
      send_all(sockfd, "QUIT\n", 5);
      break;
    }

    if (strcasecmp(buffer, "SEND") == 0) {
      send_all(sockfd, "SEND\n", 5);

      while (1) {
        if (!fgets(buffer, BUF, stdin))
          break;
        if (strcmp(buffer, ".\n") == 0 || strcmp(buffer, ".\r\n") == 0) {
          send_all(sockfd, ".\n", 2);
          break;
        }
        send_all(sockfd, buffer, strlen(buffer));
      }
    } else {
      send_all(sockfd, buffer, strlen(buffer));
      send_all(sockfd, "\n", 1);
    }

    ssize_t size = recv(sockfd, buffer, BUF - 1, 0);
    if (size <= 0) {
      printf("Server closed connection.\n");
      break;
    }

    buffer[size] = '\0';
    printf("<< %s", buffer);
    if (buffer[size - 1] != '\n')
      printf("\n");
  }
  if (shutdown(sockfd, SHUT_RDWR) == -1 && errno != ENOTCONN) {
    perror("shutdown");
  }

  close(sockfd);
  printf("Disconnected.\n");

  return EXIT_SUCCESS;
}
