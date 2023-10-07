#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // Include this header for inet_ntop

#include <sys/types.h>
#include <netdb.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"

int daemon_mode = 0;

void sig_handler(int signo) {
    printf("\nCaught \n");
    if (signo == SIGINT || signo == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        closelog();
        remove(DATA_FILE);
        exit(0);
    }
}

void daemonize() {
    // TODO: Implement daemonization
    // ...
}

int main(int argc, char* argv[]) 
{
    struct sockaddr client_addr;
    socklen_t client_addr_len;
    int fd_accepted_soket;

    struct sockaddr_in address_to_print;

    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo; // will point to the results

    // Parse command-line arguments
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    printf("\nHello\n");

    // Initialize syslog
    openlog("aesdsocket", LOG_PID, LOG_DAEMON);

    // Signal handling
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Daemon mode
    if (daemon_mode) {
        daemonize();
    }

    int sockfd;

    sockfd = socket (PF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) 
    {
        perror("Socket creation failed");
        return -1;
    }



    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC; // don't catre IPv4 or IPv6
    hints.ai_flags = AI_PASSIVE; // TCP stream sockets
    hints.ai_flags = AI_PASSIVE; // fill in my IP for me

    status = getaddrinfo(NULL, "9000", &hints, &servinfo);
    if(status != 0) 
    {
        perror("setting up sockaddr failed");
        return -1;
    }

    status = bind(sockfd, (struct sockaddr *)servinfo->ai_addr, servinfo->ai_addrlen);

    if (status == -1) 
    { 
        perror("binding sockaddr failed");
        return -1;
    }


    status = listen(sockfd, 1);
    if (status == -1) 
    { 
        perror("listing for connections on socket failed");
        return -1;
    }

    while (1) {
        // Accept a connection
        fd_accepted_soket = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (fd_accepted_soket == -1) {
            perror("Accept failed");
            //close(fd_accepted_soket);
            return -1;
        }
        
        socklen_t addr_size = sizeof(address_to_print);
        // Convert integer IP address to string format
        // status = getpeername(fd_accepted_soket, (struct sockaddr *)&address_to_print, &addr_size);
        // syslog(LOG_INFO, "Accepted connection from %s\n", address_to_print);
        // printf("Accepted connection from %s\n", address_to_print);

        status = getpeername(fd_accepted_soket, (struct sockaddr *)&address_to_print, &addr_size);
        syslog(LOG_INFO, "Accepted connection from %s\n", inet_ntoa(address_to_print.sin_addr));
        printf("Accepted connection from %s\n", inet_ntoa(address_to_print.sin_addr));

        // Receive and append data
        char buffer[1024];
        FILE* data_file = fopen(DATA_FILE, "a");
        if (data_file == NULL) {
            perror("File open failed");
            close(sockfd);
            close(fd_accepted_soket);
            return -1;
        }

        ssize_t bytes_received;
        while ((bytes_received = recv(fd_accepted_soket, buffer, sizeof(buffer), 0)) > 0) {
            fwrite(buffer, 1, bytes_received, data_file);
            if (strchr(buffer, '\n') != NULL) {
                // Packet complete
                break;
            }
        }

        fclose(data_file);

        // Send data back to the client
        data_file = fopen(DATA_FILE, "r");
        if (data_file == NULL) {
            perror("File open failed");
            // close(client_socket);
            // close(server_socket);
            return -1;
        }

        fseek(data_file, 0, SEEK_END);
        long file_size = ftell(data_file);
        fseek(data_file, 0, SEEK_SET);

        char* file_data = malloc(file_size);
        if (file_data == NULL) {
            perror("Memory allocation failed");
            fclose(data_file);
            // close(client_socket);
            // close(server_socket);
            return -1;
        }

        fread(file_data, 1, file_size, data_file);
        fclose(data_file);

        send(client_socket, file_data, file_size, 0);
        free(file_data);

        // Log closed connection
        syslog(LOG_INFO, "Closed connection from %s", client_ip);

        // Close client socket
        close(client_socket);
    }

    // Cleanup and exit
    closelog();
    remove(DATA_FILE);
    return 0;
}

