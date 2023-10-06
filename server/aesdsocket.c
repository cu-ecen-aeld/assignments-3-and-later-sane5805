#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // Include this header for inet_ntop

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

int main(int argc, char* argv[]) {
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

    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        return -1;
    }

    // Bind socket to port 9000
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Socket bind failed");
        close(server_socket);
        return -1;
    }

    // Listen for incoming connections
    if (listen(server_socket, 1) == -1) {
        perror("Listen failed");
        close(server_socket);
        return -1;
    }

    while (1) {
        // Accept a connection
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket == -1) {
            perror("Accept failed");
            close(server_socket);
            return -1;
        }

        // Convert integer IP address to string format
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);


        // Log accepted connection
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Receive and append data
        char buffer[1024];
        FILE* data_file = fopen(DATA_FILE, "a");
        if (data_file == NULL) {
            perror("File open failed");
            close(client_socket);
            close(server_socket);
            return -1;
        }

        ssize_t bytes_received;
        while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
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
            close(client_socket);
            close(server_socket);
            return -1;
        }

        fseek(data_file, 0, SEEK_END);
        long file_size = ftell(data_file);
        fseek(data_file, 0, SEEK_SET);

        char* file_data = malloc(file_size);
        if (file_data == NULL) {
            perror("Memory allocation failed");
            fclose(data_file);
            close(client_socket);
            close(server_socket);
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

