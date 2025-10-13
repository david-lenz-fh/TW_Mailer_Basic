#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include "twmailer-server.h"

#define BACKLOG 5

int fd = -1;
int cfd = -1;
int abortRequested = 0;

void signalHandler(int sig);

int main(int argc, char *argv[]) {
  struct sockaddr_in serverInfo = {0}; // gegen bind fail
  struct sockaddr_in clientInfo = {0};
  socklen_t clientSize = sizeof(clientInfo);

  if (argc != 3) {
    fprintf(stderr, "Wrong arguments %s\n", argv[0]);
    return EXIT_FAILURE;
  }
  int port = atoi(argv[1]);
  const char *mailSpoolDir = argv[2];
  (void)mailSpoolDir;
  
  serverInfo.sin_family = AF_INET;
  serverInfo.sin_addr.s_addr = 0; // bind to every ip the pc owns
  serverInfo.sin_port = htons(port);

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
    int cfd = accept(fd, (struct sockaddr *)&clientInfo, &clientSize);
    if (cfd == -1) {
      perror("accept");
      close(cfd);
      return -1;
    }
    if(send(cfd, "Connected\n", 11, 0)==-1){
      perror("Error sending client conformation");
      continue;
    }
    printf("Client connected\n");

    char message[8192]="";
    char buffer[1024];
    int size;
    while((size = recv(cfd, buffer, sizeof(buffer) - 1, 0))>0){
      buffer[size]='\0';
      strcat(buffer,"\n");
      strcat(message, buffer);
      if(strstr(message,".")==NULL){
        if(send(cfd, "OK\n", 3, 0)==-1){
          break;
        }
        continue;
      }
      char** splitMessage=str_split(message,'\n');
      char* firstLine=splitMessage[0];

      if(strcmp(firstLine, "SEND")==0){
        if(persistMessage(splitMessage)==-1)
        {
          if(send(cfd, "FAILURE\n", 8, 0)==-1){
            perror("send answer failed");
          }
        }
        else if(send(cfd, "OK\n", 3, 0)==-1){
          perror("send answer failed");
        }
      }
      else if(strcmp(firstLine, "LIST")==0){
        char* allMessages;
        allMessages=listMessages(splitMessage[1]);
        if(send(cfd, allMessages, strlen(allMessages),0)==-1){
          perror("send answer failed");
        }        
        free(allMessages);
      }
      else if(strcmp(firstLine, "READ")==0){
        char* message=readSingleMessage(splitMessage[1],splitMessage[2]);
        printf("%s\n",message);
        fflush(stdout);
        if(send(cfd, message, strlen(message)+1,0)==-1){
          perror("send answer failed");
        }    
        free(message);
      }
      else if(strcmp(firstLine, "DEL")==0){
        int deleted=deleteMessage(splitMessage[1],splitMessage[2]);
        if(deleted==0){
          if(send(cfd, "DELETED\n",8,0)==-1){
          perror("send answer failed");
          }
        }
        else{
          if(send(cfd, "NOT FOUND\n",10,0)==-1){
          perror("send answer failed");
          }
        }
      }
      else if(strcmp(firstLine, "QUIT")==0){
        break;
      }
      message[0] = '\0';
    }
    close(cfd);
  }
}
char* listMessages(char* username){
  char* buffer = malloc(20000);
  buffer[0]='\0';
  sprintf(buffer, "%d", countUserMessages(username));
  strcat(buffer,"\n");
  char* msgs=readMessage(username);
  char** lines=str_split(msgs,'\n');
  for(int i=0;lines[i];i++){
    char* subj=str_split(lines[i],';')[3];
    strcat(buffer,subj);
    strcat(buffer,"\n");
  }
  return buffer;
}
int persistMessage(char** messageLines){

  if(messageLines[5]==NULL||messageLines[6]!=NULL){
    return -1;
  }
  char filename[256];

  snprintf(filename, sizeof(filename), "%s.txt", messageLines[1]);
  FILE *file = fopen(filename, "a");
  if (!file) {
      printf("The file is not opened.\n");
      return -1;
  }

  fflush(stdout);
  int id=countUserMessages(messageLines[1]);

  fflush(stdout);
  char id_str[12];
  sprintf(id_str, "%d", id);

  size_t len = strlen(messageLines[1]) + strlen(messageLines[2]) + strlen(messageLines[3]) +
  strlen(messageLines[4]) + 17; //16 weil 12 von id + 4 für die 4 ";" und 1 für nullterminate string
  char* newLine=malloc(len);
  if(!newLine){
    fclose(file);
    return -1;
  }
  newLine[0]='\0';
  strcat(newLine, id_str);
  strcat(newLine, ";");
  strcat(newLine, messageLines[1]);
  strcat(newLine, ";");
  strcat(newLine, messageLines[2]);
  strcat(newLine, ";");
  strcat(newLine, messageLines[3]);
  strcat(newLine, ";");
  strcat(newLine, messageLines[4]);
  fprintf(file, "%s\n", newLine);

  fclose(file);
  free(newLine);
  return 1;
}
char* readSingleMessage(char* username, char* messageId) {
    char* filestr = readMessage(username);
    printf("%s",filestr);
    if (!filestr) return NULL;
    char** savedMessages = str_split(filestr, '\n');
    free(filestr);

    char* result = NULL;
    for (int i = 0; savedMessages[i]; i++) {
        size_t line_len=strlen(savedMessages[i]);
        char** infos = str_split(savedMessages[i], ';');
        if (infos && infos[0] && strcmp(infos[0], messageId) == 0) {
          result=malloc(line_len);
          result[0]='\0';
          for(int j=1;infos[j];j++){
            strcat(result, infos[j]);
            strcat(result, "\n");
          }
          free(infos);
          free(savedMessages[i]);
          break;
        }
        free(savedMessages[i]);
    }
    free(savedMessages);
    return result;
}
int deleteMessage(char* username, char* messageId){
  int re=-1;
  char* message=readMessage(username);
  char* copyMessage=malloc(strlen(message)+1);
  copyMessage[0]='\0';
  strcpy(copyMessage, message);
  char** lines=str_split(copyMessage, '\n');
  copyMessage[0]='\0';
  for(int i=0;lines[i];i++){
    char* copyLine=malloc(strlen(lines[i])+1);
    copyLine[0]='\0';
    copyLine=strcpy(copyLine,lines[i]);
    char** infos=str_split(copyLine,';');
    if(strcmp(infos[0],messageId)!=0){
      strcat(copyMessage,lines[i]);
      strcat(copyMessage, "\n");
    }
    else{
      re=0;
    }
  }
  char filename[256];

  snprintf(filename, sizeof(filename), "%s.txt", username);
  FILE *file = fopen(filename, "w");
  if (!file) {
      printf("The file is not opened.\n");
      return -1;
  }
  if(re==0){
    printf("%s",copyMessage);
    fprintf(file, "%s", copyMessage);
  }
  free(message);
  free(copyMessage);
  fclose(file);
  return re;
}
char* readMessage(char* username){
  char filename[256];
  snprintf(filename, sizeof(filename), "%s.txt", username);

  FILE *file = fopen(filename, "rb");
  if (!file) {
    perror("fopen");
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
    if (buffer) buffer[0] = '\0';
  }
  return buffer;
}
void signalHandler(int sig) {
  if (sig == SIGINT) {
    printf("Abort requested\n");
    abortRequested = 1;

    if (cfd != -1) {
      if (shutdown(cfd, SHUT_RDWR) == -1) {
        perror("shutdown new_socket");
      }
      if (close(cfd) == -1) {
        perror("close new_socket");
      }
      cfd = -1;
    }

    if (fd != -1) {
      if (shutdown(fd, SHUT_RDWR) == -1) {
        perror("shutdown create_socket");
      }
      if (close(fd) == -1) {
        perror("close create_socket");
      }
      fd = -1;
    }

    exit(0);
  } else {
    exit(sig);
  }
}

char** str_split(char* a_str, const char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    count += last_comma < (a_str + strlen(a_str) - 1);

    count++;

    result = malloc(sizeof(char*) * count);

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token)
        {
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        *(result + idx) = 0;
    }

    return result;
}
char* joinStrings(char **strings, char sep) {
    size_t totalLen = 1; // für '\0'
    for (int i = 0; strings[i]; i++)
        totalLen += strlen(strings[i]) + 1; // +1 für Separator

    char *result = malloc(totalLen);
    if (!result) return NULL;
    result[0] = '\0';

    for (int i = 0; strings[i]; i++) {
        strcat(result, strings[i]);
        if (strings[i+1]) {
            size_t len = strlen(result);
            result[len] = sep;
            result[len+1] = '\0';
        }
    }

    return result;
}

int countUserMessages(char* username){
  char* userMesssages=readMessage(username);
  char** lines=str_split(userMesssages,'\n');
  int re=0;
  for(;lines[re];re++){
    
  }
  return re;
}