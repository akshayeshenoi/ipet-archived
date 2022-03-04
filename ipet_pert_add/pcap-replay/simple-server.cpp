#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <arpa/inet.h>
#include <chrono>
#include <vector>

void recv_thread_loop(int sockfd) {

    unsigned char buff[4];
    int bytesRecv;

    printf("proto,size,time_relative\n");

    // set start time
    auto start = std::chrono::high_resolution_clock::now();

    while (1) {
        bytesRecv = read(sockfd, buff, 4);
        if (bytesRecv == 0) {
            // connection closed, return
            printf("Peer closed connection \n");
            break;
        }

        // get current time
        auto now = std::chrono::high_resolution_clock::now();
        auto diff = now - start;
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(diff);

        // get protocol
        char proto = (char)buff[0];

        // get direction
        char direction = (char)buff[1];

        // get actual payload size which was encoded in the 2nd and 3rd byte (little endianness)
        int size = (buff[3] << 8) + buff[2];

        // print packet type (indicated in the first byte), packet length (indicated in the second byte),
        // and packet time
        printf("%c,%c,%d,%ld\n", proto, direction, size, millis.count());
    }
}

void init_server(int serverPort) {
    struct sockaddr_in bindAddress;
	bindAddress.sin_family = AF_INET;
	bindAddress.sin_addr.s_addr = INADDR_ANY;
	bindAddress.sin_port = htons(serverPort);

    int server_fd;
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        printf("server socket creation failed");
        exit(EXIT_FAILURE);
    }

    // reuse socket just in case
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))){
        printf("server setsockopt failed");
        exit(EXIT_FAILURE);
    }

    if (bind(server_fd, (struct sockaddr *)&bindAddress, sizeof(bindAddress))<0) {
        printf("server bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        printf("server listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Listening..\n");

    // accept client connection
    int client_fd; int addrlen;
    if ((client_fd = accept(server_fd, (struct sockaddr *)&bindAddress, (socklen_t*)&addrlen))<0) {
        printf("server accept failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to a client.\n");

    // start send/recv threads
    recv_thread_loop(client_fd);

    close(server_fd);
    close(client_fd);
}

int main(int argc, char** argv) {
    printf("Usage: ./simple-server server-bindport\n");

    if (argc != 2) {
        printf("Invalid number of arguments\n");
        return EXIT_FAILURE;
    }
    int bindPort = atoi(argv[1]);
    init_server(bindPort);
}
