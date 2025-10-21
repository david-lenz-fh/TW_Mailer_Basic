#include "twmailer-server.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define BACKLOG 5
#define SUBJECT_MAXLEN 80

int listen_fd = -1;
int client_fd = -1;
int abortRequested = 0;

static const char *g_spoolDir = NULL;

void signalHandler(int sig);

static int send_all(int s, const void *buf, size_t len) {
  const char *p = (const char *)buf;
  size_t off = 0;
  while (off < len) {
    ssize_t n = send(s, p + off, len - off, 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    off += (size_t)n;
  }
  return 0;
}

static int recv_line(int s, char *out, size_t cap) {

  size_t i = 0;
  if (cap == 0) {
    return -1;
  }

  while (i + 1 < cap) {
    char c;
    ssize_t n = recv(s, &c, 1, 0);

    if (n == 0) {
      break;
    }

    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }

    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      break;
    }

    out[i++] = c;
  }

  out[i] = '\0';
  return (int)i;
}

static void free_split(char **arr) {
  if (!arr) {
    return;
  }

  for (int i = 0; arr[i]; i++) {
    free(arr[i]);
  }

  free(arr);
}

static int append_capped(char *dst, size_t cap, const char *src) {
  size_t have = strlen(dst);
  size_t need = strlen(src);
  if (have + need >= cap) {
    return -1;
  }

  memcpy(dst + have, src, need + 1);
  return 0;
}

static int validUser(const char *u) {
  if (!u || !*u || strlen(u) > 8)
    return 0;
  for (size_t i = 0; u[i]; i++) {
    if (!isalnum((unsigned char)u[i]) || isupper(u[i]))
      return 0;
  }
  return 1;
}

static void close_socket(int *fd) {
  if (*fd != -1) {
    shutdown(*fd, SHUT_RDWR);
    close(*fd);
    *fd = -1;
  }
}

char *listMessages(char *username);
int persistMessage(char **messageLines);
char *readSingleMessage(char *username, char *messageId);
int deleteMessage(char *username, char *messageId);
char *readMessage(char *username);
char **str_split(char *a_str, const char a_delim);
int countUserMessages(char *username);

int main(int argc, char *argv[]) {
  struct sockaddr_in serverInfo = {0};
  struct sockaddr_in clientInfo = {0};
  socklen_t clientSize = sizeof(clientInfo);

  if (argc != 3) {
    fprintf(stderr, "Usage: %s <port> <mail-spool-dir>\n", argv[0]);
    return EXIT_FAILURE;
  }

  int port = atoi(argv[1]);
  const char *mailSpoolDir = argv[2];
  g_spoolDir = mailSpoolDir;

  struct stat st;
  if (stat(g_spoolDir, &st) == -1) {
    if (mkdir(g_spoolDir, 0775) == -1 && errno != EEXIST) {
      perror("mkdir mail spool dir");
      return EXIT_FAILURE;
    }
  }

  signal(SIGINT, signalHandler);

  serverInfo.sin_family = AF_INET;
  serverInfo.sin_addr.s_addr = htonl(INADDR_ANY);
  serverInfo.sin_port = htons(port);

  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }

  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

  if (bind(listen_fd, (struct sockaddr *)&serverInfo, sizeof(serverInfo)) ==
      -1) {
    perror("bind");
    close_socket(&listen_fd);
    return EXIT_FAILURE;
  }

  if (listen(listen_fd, BACKLOG) == -1) {
    perror("listen");
    close_socket(&listen_fd);
    return EXIT_FAILURE;
  }

  printf("Server running on port %d...\n", port);

  while (!abortRequested) {
    client_fd = accept(listen_fd, (struct sockaddr *)&clientInfo, &clientSize);
    if (client_fd == -1) {
      if (errno == EINTR) {
        continue;
      }
      perror("accept");
      continue;
    }

    printf("Client connected\n");
    send_all(client_fd, "Connected\n", 10);

    char line[1024];
    while (1) {
      int n = recv_line(client_fd, line, sizeof(line));
      if (n <= 0) {
        break;
      }

      if (strcmp(line, "SEND") == 0) {
        char *sender = NULL, *receiver = NULL, *subject = NULL;
        char *body = NULL;
        size_t body_cap = 0, body_len = 0;
        int err = 0;
        // sender
        n = recv_line(client_fd, line, sizeof(line));
        if (n <= 0) {
          send_all(client_fd, "ERR no username provided\n", 26);
        } else if (!validUser(line)) {
          send_all(client_fd, "ERR invalid username\n", 21);
        } else {
          sender = strdup(line);
        }

        // receiver
        if (!err) {
          n = recv_line(client_fd, line, sizeof(line));
          if (n <= 0 || !validUser(line)) {
            err = 1;
          } else {
            receiver = strdup(line);
          }
        }

        // subject
        if (!err) {
          n = recv_line(client_fd, line, sizeof(line));
          if (n <= 0) {
            err = 1;
          } else {
            line[SUBJECT_MAXLEN] = '\0';
            subject = strdup(line);
          }
        }
        fflush(stdout);

        while (!err) {
          n = recv_line(client_fd, line, sizeof(line));
          if (n <= 0) {
            err = 1;
            break;
          }
          fflush(stdout);
          if (line[0] == '.' && line[1] == '\0') {
            break;
          }

          size_t line_len = strlen(line);
          if (body_len + line_len + 2 > body_cap) {
            size_t new_cap = body_cap ? body_cap * 2 : 1024;
            while (new_cap < body_len + line_len + 2)
              new_cap *= 2;
            char *tmp = realloc(body, new_cap);
            if (!tmp) {
              err = 1;
              break;
            }
            body = tmp;
            body_cap = new_cap;
          }
          if (body_len > 0) {
            body[body_len++] = ' '; // turn newlines into spaces
          }
          memcpy(body + body_len, line, line_len);
          body_len += line_len;
          body[body_len] = '\0';
        }

        if (!err) {
          char *messageLines[7] = {"SEND", sender, receiver, subject,
                                   body,   ".",    NULL};
          if (persistMessage(messageLines) == -1) {
            send_all(client_fd, "ERR persist failed\n", 19);
          } else {
            send_all(client_fd, "OK\n", 3);
          }
        } else {
          send_all(client_fd, "ERR invalid input\n", 18);
        }

        free(sender);
        free(receiver);
        free(subject);
        free(body);
      }

      else if (strcmp(line, "LIST") == 0) {
        n = recv_line(client_fd, line, sizeof(line));
        if (n <= 0 || !validUser(line)) {
          send_all(client_fd, "0\n", 2);
        } else {
          char *all = listMessages(line);
          if (all != NULL) {
            send_all(client_fd, all, strlen(all));
            free(all);
          } else {
            send_all(client_fd, "0\n", 2);
          }
        }
      } else if (strcmp(line, "READ") == 0) {
        char user[1024];
        char num[1024];
        n = recv_line(client_fd, user, sizeof(user));
        if (n <= 0) {
          send_all(client_fd, "ERR missing username\n", 22);
          continue;
        } else if (!validUser(user)) {
          send_all(client_fd, "ERR invalid username\n", 21);
          continue;
        } else {
          n = recv_line(client_fd, num, sizeof(num));
          if (n <= 0) {
            send_all(client_fd, "ERR missing message id\n", 24);
            continue;
          } else {
            char *msg = readSingleMessage(user, num);
            if (msg != NULL) {
              send_all(client_fd, "OK\n", 3);
              send_all(client_fd, msg, strlen(msg));
              free(msg);
            } else {
              send_all(client_fd, "ERR message not found\n", 23);
            }
          }
        }
      } else if (strcmp(line, "DEL") == 0) {
        char user[1024];
        char num[1024];
        n = recv_line(client_fd, user, sizeof(user));
        if (n <= 0) {
          send_all(client_fd, "ERR missing username\n", 22);
        } else if (!validUser(user)) {
          send_all(client_fd, "ERR invalid username\n", 21);
        } else {
          n = recv_line(client_fd, num, sizeof(num));
          if (n <= 0) {
            send_all(client_fd, "ERR missing message id\n", 24);
          } else {
            int deleted = deleteMessage(user, num);
            if (deleted == 0) {
              send_all(client_fd, "OK\n", 3);
            } else {
              send_all(client_fd, "ERR message not found\n", 23);
            }
          }
        }
      } else if (strcmp(line, "QUIT") == 0) {
        break;
      } else {
        send_all(client_fd, "ERR unknown command\n", 21);
      }
    }

    if (client_fd != -1) {
      shutdown(client_fd, SHUT_RDWR);
      close(client_fd);
      client_fd = -1;
    }

    printf("Client disconnected\n");
  }

  if (listen_fd != -1) {
    shutdown(listen_fd, SHUT_RDWR);
    close(listen_fd);
    listen_fd = -1;
  }

  return EXIT_SUCCESS;
}
char *listMessages(char *username) {
  char *msgs = readMessage(username);
  if (!msgs) {
    char *zero = malloc(2);
    if (zero) {
      zero[0] = '0';
      zero[1] = '\n';
    }
    return zero;
  }

  char **lines = str_split(msgs, '\n');
  free(msgs);

  int cnt = 0;
  for (; lines && lines[cnt]; cnt++)
    ;

  size_t cap = 20000;
  char *buffer = malloc(cap);
  if (!buffer) {
    free_split(lines);
    return NULL;
  }
  buffer[0] = '\0';

  char head[32];
  snprintf(head, sizeof(head), "%d\n", cnt);
  append_capped(buffer, cap, head);

  for (int i = 0; lines[i]; i++) {
    char **parts = str_split(lines[i], ';');
    const char *subj = (parts && parts[3]) ? parts[3] : "";
    append_capped(buffer, cap, subj);
    append_capped(buffer, cap, "\n");
    free_split(parts);
  }

  free_split(lines);
  return buffer;
}

int persistMessage(char **messageLines) {
  if (!messageLines[1] || !messageLines[2] || !messageLines[3] ||
      !messageLines[4])
    return -1;

  char filename[256];
  snprintf(filename, sizeof(filename), "%s/%s.txt", g_spoolDir,
           messageLines[2]);

  FILE *file = fopen(filename, "a");
  if (!file) {
    perror("fopen");
    return -1;
  }

  int id = countUserMessages(messageLines[2]) + 1;

  size_t len = 16 + strlen(messageLines[1]) + strlen(messageLines[2]) +
               strlen(messageLines[3]) + strlen(messageLines[4]) + 8;

  char *newLine = malloc(len);
  if (!newLine) {
    fclose(file);
    return -1;
  }
  newLine[0] = '\0';

  char id_str[32];
  snprintf(id_str, sizeof(id_str), "%d", id);

  strcat(newLine, id_str);
  strcat(newLine, ";");
  strcat(newLine, messageLines[1]);
  strcat(newLine, ";");
  strcat(newLine, messageLines[2]);
  strcat(newLine, ";");
  strcat(newLine, messageLines[3]);
  strcat(newLine, ";");
  strcat(newLine, messageLines[4]);

  fprintf(file, "%s", newLine);
  if (newLine[strlen(newLine) - 1] != '\n')
    fprintf(file, "\n");

  free(newLine);
  fclose(file);
  return 0;
}

char *readSingleMessage(char *username, char *messageId) {
  char *filestr = readMessage(username);
  if (!filestr)
    return NULL;

  char **savedMessages = str_split(filestr, '\n');
  free(filestr);
  if (!savedMessages)
    return NULL;

  char *result = NULL;
  for (int i = 0; savedMessages[i]; i++) {
    char **infos = str_split(savedMessages[i], ';');
    if (infos && infos[0] && strcmp(infos[0], messageId) == 0) {
      size_t need = 1;
      for (int j = 1; infos[j]; j++)
        need += strlen(infos[j]) + 1;
      result = malloc(need);
      if (!result) {
        free_split(infos);
        break;
      }
      result[0] = '\0';
      for (int j = 1; infos[j]; j++) {
        strcat(result, infos[j]);
        strcat(result, "\n");
      }
      free_split(infos);
      break;
    }
    free_split(infos);
  }

  free_split(savedMessages);
  return result;
}

int deleteMessage(char *username, char *messageId) {
  int re = -1;
  char *message = readMessage(username);
  if (!message)
    return -1;

  size_t cap = strlen(message) + 1;
  char *copyMessage = calloc(cap, 1);
  if (!copyMessage) {
    free(message);
    return -1;
  }

  char *msgCopy = strdup(message);
  if (!msgCopy) {
    free(message);
    free(copyMessage);
    return -1;
  }

  char **lines = str_split(msgCopy, '\n');
  if (!lines) {
    free(msgCopy);
    free(message);
    free(copyMessage);
    return -1;
  }

  for (int i = 0; lines[i]; i++) {
    char *lineDup = strdup(lines[i]);
    if (!lineDup) {
      free_split(lines);
      free(msgCopy);
      free(message);
      free(copyMessage);
      return -1;
    }

    char **infos = str_split(lineDup, ';');

    if (infos && infos[0] && strcmp(infos[0], messageId) != 0) {
      append_capped(copyMessage, cap, lines[i]);
      append_capped(copyMessage, cap, "\n");
    } else if (infos && infos[0] && strcmp(infos[0], messageId) == 0) {
      re = 0;
    }

    free_split(infos);
    free(lineDup);
  }

  if (re == 0) {
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/%s.txt", g_spoolDir, username);
    FILE *file = fopen(filename, "w");
    if (!file) {
      perror("fopen");
      free_split(lines);
      free(msgCopy);
      free(message);
      free(copyMessage);
      return -1;
    }
    fprintf(file, "%s", copyMessage);
    fclose(file);
  }

  free_split(lines);
  free(msgCopy);
  free(message);
  free(copyMessage);
  return re;
}

char *readMessage(char *username) {
  char filename[256];
  snprintf(filename, sizeof(filename), "%s/%s.txt", g_spoolDir, username);

  FILE *file = fopen(filename, "rb");
  if (!file) {
    return NULL;
  }

  char *buffer = NULL;
  size_t size = 0;
  char line[1024];

  while (fgets(line, sizeof(line), file)) {
    size_t len = strlen(line);
    char *new_buffer = realloc(buffer, size + len + 1);
    if (!new_buffer) {
      free(buffer);
      fclose(file);
      return NULL;
    }
    buffer = new_buffer;
    memcpy(buffer + size, line, len);
    size += len;
    buffer[size] = '\0';
  }

  fclose(file);
  if (!buffer) {
    buffer = malloc(1);
    if (buffer)
      buffer[0] = '\0';
  }
  return buffer;
}

void signalHandler(int sig) {
  if (sig == SIGINT) {
    printf("\nAbort requested, shutting down...\n");
    abortRequested = 1;
    close_socket(&client_fd);
    close_socket(&listen_fd);
    exit(0);
  }
}

char **str_split(char *a_str, const char a_delim) {
  char **result = 0;
  size_t count = 0;
  char *tmp = a_str;
  char *last_comma = 0;
  char delim[2];
  delim[0] = a_delim;
  delim[1] = 0;

  while (*tmp) {
    if (a_delim == *tmp) {
      count++;
      last_comma = tmp;
    }
    tmp++;
  }

  count += last_comma < (a_str + strlen(a_str) - 1);
  count++;

  result = malloc(sizeof(char *) * count);

  if (result) {
    size_t idx = 0;
    char *token = strtok(a_str, delim);

    while (token) {
      *(result + idx++) = strdup(token);
      token = strtok(0, delim);
    }
    *(result + idx) = 0;
  }

  return result;
}

int countUserMessages(char *username) {
  int re = 0;
  char *userMesssages = readMessage(username);
  if (!userMesssages) {
    return 0;
  }

  char **lines = str_split(userMesssages, '\n');
  free(userMesssages);
  if (!lines) {
    return 0;
  }

  while (lines[re]) {
    re++;
  }

  free_split(lines);
  return re;
}
