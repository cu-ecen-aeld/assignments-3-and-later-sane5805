/**
 * @name: aesdsocket.c
 * @brief: A socket program for a server in stream mode.
 * @author: Saurav Negi
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024
#define ERROR_MEMORY_ALLOC   -6
#define FILE_PERMISSIONS 0644

int server_socket;
int client_socket;
int daemon_mode = 0;
int option_value = 1;

bool sig_handler_hit = false;

// Function to handle signals
static void signal_handler(int signo);

// Function to daemonize the process
void daemonize();

int main(int argc, char **argv);

/**
 * @function: signal_handler
 * @brief: Handles signals like SIGINT and SIGTERM.
 * @params:
 *    - signo: The signal number.
 * @return: None
 */
static void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {

        sig_handler_hit = true;
            
        syslog(LOG_INFO, "Caught signal, exiting"); // Log that a signal was caught
    }
}

void exit_safely() {
    close(server_socket); // Close the server socket
    close(client_socket); // Close the client socket
    closelog(); // Close syslog
    remove(DATA_FILE); // Remove the data file
    exit(0); // Exit the program
}

/**
 * @name: daemonize
 * @brief: Daemonizes the process.
 * @param: None
 * @return: None
 */
void daemonize() {
    pid_t pid = fork();  // Fork the parent process
    if (pid < 0) {
        perror("Fork failed"); // Print an error message if fork fails
        exit(EXIT_FAILURE); // Exit with failure status
    }
    if (pid > 0) {
        // Parent process, exit
        exit(EXIT_SUCCESS); // Exit with success status
    }

    if (setsid() < 0) {  // Child process: create a new session and become the session leader
        perror("setsid failed"); // Print an error message if setsid fails
        exit(EXIT_FAILURE); // Exit with failure status
    }

    pid = fork();  // Fork again to prevent the process from acquiring a controlling terminal
    if (pid < 0) {
        perror("Second fork failed"); // Print an error message if the second fork fails
        exit(EXIT_FAILURE); // Exit with failure status
    }
    if (pid > 0) {
        // Parent of the second fork, exit
        exit(EXIT_SUCCESS); // Exit with success status
    }

    if (chdir("/") < 0) {  // Change the working directory to a safe location
        perror("chdir failed"); // Print an error message if chdir fails
        exit(EXIT_FAILURE); // Exit with failure status
    }

    // Close standard file descriptors (stdin, stdout, stderr)
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

/**
 * @name: main
 * @brief: The main function of the server.
 * @params:
 *    - argc: The number of command-line arguments.
 *    - argv: An array of command-line argument strings.
 * @return: 0 on success, -1 on error
 */
int main(int argc, char **argv) 
{
    // Parse command-line arguments
    if (argc > 1 && strcmp(argv[1], "-d") == 0) 
    {
        daemon_mode = 1; // Set daemon_mode to 1 if "-d" option is provided
    } 

    // Initialize syslog
    openlog(NULL, 0, LOG_USER); // Open syslog with LOG_USER facility

    syslog(LOG_INFO, "Socket started!"); // Log that the socket has started

    // Signal handling
    signal(SIGINT, signal_handler); // Register signal_handler for SIGINT
    signal(SIGTERM, signal_handler); // Register signal_handler for SIGTERM

    // Daemon mode
    if (daemon_mode) 
    {
        syslog(LOG_DEBUG, "Daemon created!"); // Log that a daemon was created

        daemonize(); // Call the daemonize function to daemonize the process
    }

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0); // Create a socket
    if (server_socket == -1) {
        perror("Socket creation failed"); // Print an error message if socket creation fails
        syslog(LOG_ERR, "Error creating socket: %m"); // Log socket creation error
        return -1; // Return an error status
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value)) == -1) 
    {
        perror("setsockopt"); // Print an error message if setsockopt fails
        syslog(LOG_ERR, "Error setting socket options: %m"); // Log setsockopt error
        close(server_socket); // Close the server socket
        return -1; // Return an error status
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY; 

    // Bind socket to port 9000
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Socket bind failed"); // Print an error message if socket bind fails
        syslog(LOG_ERR, "Socket bind failed: %m"); // Log socket bind error
        close(server_socket); // Close the server socket
        return -1; // Return an error status
    }

    // Listen for incoming connections
    if (listen(server_socket, 1) == -1) {
        perror("Listen failed"); // Print an error message if listen fails
        syslog(LOG_ERR, "Listen failed: %m"); // Log listen error
        close(server_socket); // Close the server socket
        return -1; // Return an error status
    }

    while (1) 
    {
        if (sig_handler_hit) {
            exit_safely();
        }
        
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) {
            perror("Accept failed"); // Print an error message if accept fails
            syslog(LOG_ERR, "Accept failed: %m"); // Log accept error
        }
        else {
            char client_ip[INET_ADDRSTRLEN];

            getpeername(client_socket, (struct sockaddr *)&client_addr, &client_len);

            inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

            // Log accepted connection
            syslog(LOG_INFO, "Accepted connection from %s", client_ip);

            char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
            if (buffer == NULL) {
                syslog(LOG_ERR, "Memory allocation failed!"); // Log memory allocation error
                return -1; // Return an error status
            }

            // Receive and append data
            int data_file = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, FILE_PERMISSIONS);
            if (data_file == -1) {
                perror("File open failed"); // Print an error message if file open fails
                syslog(LOG_ERR, "File open failed: %m"); // Log file open error
                free(buffer);
                close(client_socket);
            }
            else {
                ssize_t bytes_received;
                while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
                    write(data_file, buffer, bytes_received);
                    if (memchr(buffer, '\n', bytes_received) != NULL) {
                        break;
                    }
                }

                syslog(LOG_INFO, "Received data from %s: %s, %d", client_ip, buffer, (int)bytes_received);

                close(data_file);

                lseek(data_file, 0, SEEK_SET);

                // Send data back to the client
                data_file = open(DATA_FILE, O_RDONLY);
                if (data_file == -1) {
                    perror("file open failed"); // Print an error message if file open fails
                    syslog(LOG_ERR, "File open failed: %m"); // Log file open error
                    free(buffer);
                    close(client_socket);
                }
                else {
                    ssize_t bytes_read;
                    memset(buffer, 0, sizeof(char) * BUFFER_SIZE);

                    while ((bytes_read = read(data_file, buffer, sizeof(char) * BUFFER_SIZE)) > 0) {
                        send(client_socket, buffer, bytes_read, 0);
                    }

                    free(buffer);
                    close(data_file);
                    close(client_socket);

                    syslog(LOG_INFO, "Closed connection from %s", client_ip);
                }
            }
        }
    }

    close(server_socket);
    if (unlink(DATA_FILE) == -1) {
        syslog(LOG_ERR, "Error removing data file: %m"); // Log error when removing data file
    }
    close(client_socket);
    closelog();

    return 0;
}
