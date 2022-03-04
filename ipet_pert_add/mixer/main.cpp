#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "mixer.hpp"

#define INTERNAL_PORT 8000
#define EXTERNAL_PORT 8001
#define EXTERNAL_ADDR "127.0.0.1"

void setup_internal_server(int &server_fd, struct sockaddr_in &address) {
if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)	{
		perror("socket() failed");
		exit(EXIT_FAILURE);
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(INTERNAL_PORT);

	if (bind(server_fd, (struct sockaddr *)&address,
								sizeof(address))<0) {
		perror("bind() failed");
		exit(EXIT_FAILURE);
	}
	if (listen(server_fd, 3) < 0) {
		perror("listen() failed");
		exit(EXIT_FAILURE);
	}

    std::cout << "Listening on port: " << INTERNAL_PORT << std::endl;
}

void setup_external_server(int &server_fd, struct sockaddr_in &address) {
    // note that we act as client in this case
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)	{
		perror("socket() failed");
		exit(EXIT_FAILURE);
	}

    address.sin_family = AF_INET;
	address.sin_port = htons(EXTERNAL_PORT);

    if(inet_pton(AF_INET, EXTERNAL_ADDR, &address.sin_addr) <= 0) {
        perror("\nInvalid address/Address not supported \n");
        exit(EXIT_FAILURE);
    }

    if (connect(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Connection Failed");
        exit(EXIT_FAILURE);
    }
}

void run_server(int internal_server_fd, int external_server_fd, struct sockaddr_in int_server_address) {
    while(1) {
        int new_device_fd;
        int addrlen = sizeof(int_server_address);
        if ((new_device_fd = accept(internal_server_fd, (struct sockaddr *)&int_server_address,
                        (socklen_t*)&addrlen))<0) {
            perror("accept() failure");
            exit(EXIT_FAILURE);
        }

        char buff[256];
        recv(new_device_fd, buff, 256, 0);
        std::string dev_name(buff);
        std::cout << "New device: " << dev_name << std::endl;

        // pass both to device
        Device *new_device = new Device(dev_name, new_device_fd, external_server_fd);
        new std::thread(&Device::relay_thread_loop, new_device); // this will leak
        new std::thread(&Device::cover_traffic_gen, new_device);
    }
}

int main() {
    // set up interal interface server (to recv traffic from devices)
    // and external server (to send the traffic)
    int internal_server_fd, external_server_fd;
    struct sockaddr_in int_serv_address, ext_serv_address;
    setup_internal_server(internal_server_fd, int_serv_address);
    setup_external_server(external_server_fd, ext_serv_address);

    std::thread internal_server_t(run_server, internal_server_fd, external_server_fd, int_serv_address);

    internal_server_t.join();
}