#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>        // für close()
#include <arpa/inet.h>     // für inet_aton(), inet_ntoa()
#include <sys/socket.h>    // für socket(), connect(), etc.
#include <netinet/in.h>    // für struct sockaddr_in

#define BUF 1024

int main(int argc, char *argv[]) {
    if(argc!=3){
        fprintf(stderr, "Wrong arguments %s\n", argv[0]);
        return EXIT_FAILURE;
    }
    char* ip = argv[1];
    int port = atoi(argv[2]);

    int create_socket;
    char buffer[BUF];
    struct sockaddr_in address;
    int size;

    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Socket error");
        return EXIT_FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;         
    address.sin_port = htons(port);

    inet_aton(ip, &address.sin_addr);
    if (connect(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
    {
        perror("Connect error - no server available");
        return EXIT_FAILURE;
    }
    printf("Connection with server (%s) established\n", inet_ntoa(address.sin_addr));
    size = recv(create_socket, buffer, BUF - 1, 0);

    if (size == -1)
    {
        perror("recv error");
    }
    else if (size == 0)
    {
        printf("Server closed remote socket\n");
    }
    else
    {
        buffer[size] = '\0';
        printf("%s", buffer);
    }

    int isQuit=0;
    while(!isQuit)
    {
        printf(">>");
        if (fgets(buffer, BUF - 1, stdin) != NULL)
        {
            int size = strlen(buffer);

            // remove new-line signs from string at the end
            if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n')
            {
                size -= 2;
                buffer[size] = 0;
            }
            else if (buffer[size - 1] == '\n')
            {
                --size;
                buffer[size] = 0;
            }
            isQuit = strcmp(buffer, "quit") == 0;

            if ((send(create_socket, buffer, size + 1, 0)) == -1) 
            {
                perror("send error");
                break;
            }

            size = recv(create_socket, buffer, BUF - 1, 0);
            if (size == -1)
            {
                perror("recv error");
                break;
            }
            else if (size == 0)
            {
                printf("Server closed remote socket\n"); // ignore error
                break;
            }
            else
            {
                buffer[size] = '\0';
                printf("<< %s\n", buffer);
            }
        }
    }
    if (create_socket != -1)
    {
        if (shutdown(create_socket, SHUT_RDWR) == -1)
        {
            // invalid in case the server is gone already
            perror("shutdown create_socket"); 
        }
        if (close(create_socket) == -1)
        {
            perror("close create_socket");
        }
        create_socket = -1;
    }

   return EXIT_SUCCESS;
}