#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>

#define BUF 1024
#define PORT 6543

int main(int argc, char **argv ){
    struct sockaddr_in address;
    int size;
    int isQuit;
    int create_socket = -1;

    if((create_socket=socket(AF_INET, SOCK_STREAM, 0))==-1){
        perror("Socket error"); // errno set by socket()
        return EXIT_FAILURE;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
    {
        perror("bind error");
        return EXIT_FAILURE;
    }

    
}
