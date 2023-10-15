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
#include <stdbool.h>
#include "queue.h"
#include <pthread.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024
#define ERROR_MEMORY_ALLOC   -6
#define FILE_PERMISSIONS 0644
#define TIMESTAMP_INTERVAL 10 // 10 seconds

int server_socket;
int client_socket;
int daemon_mode = 0;
int option_value = 1;

bool sig_handler_hit = false;

// Mutex for file write synchronization
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Define your custom singly linked list structure
STAILQ_HEAD(thread_list, thread_info) threads_head;

struct thread_info {
    pthread_t thread_id;
    STAILQ_ENTRY(thread_info) entries;
};

// Function to handle signals
static void signal_handler(int signo);

// Function to daemonize the process
void daemonize();

// Function to append a timestamp to the file
void append_timestamp();

// Function to clean up threads
void cleanup_threads();

// Thread function to handle client connections
void *client_handler(void *arg);

int main(int argc, char **argv);

void exit_safely() {
    close(server_socket); // Close the server socket
    close(client_socket); // Close the client socket
    closelog(); // Close syslog
    remove(DATA_FILE); // Remove the data file
    exit(0); // Exit the program
}


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

        exit_safely();
            
        syslog(LOG_INFO, "Caught signal, exiting"); // Log that a signal was caught
    }
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

void append_timestamp() {
    while (!sig_handler_hit) {
        time_t current_time;
        struct tm *tm_info;
        char timestamp[64];

        time(&current_time);
        tm_info = localtime(&current_time);
        strftime(timestamp, sizeof(timestamp), "timestamp:%Y-%m-%d %H:%M:%S\n", tm_info);

        pthread_mutex_lock(&file_mutex);

        FILE *file = fopen(DATA_FILE, "a");
        if (file) {
            fputs(timestamp, file);
            fclose(file);
        } else {
            syslog(LOG_ERR, "Error opening data file for timestamp: %m");
        }

        pthread_mutex_unlock(&file_mutex);

        sleep(10);
    }
}

void cleanup_threads() {
    struct thread_info *thread, *tmp;
    pthread_mutex_lock(&file_mutex);

    STAILQ_FOREACH_SAFE(thread, &threads_head, entries, tmp) {
        pthread_join(thread->thread_id, NULL);
        STAILQ_REMOVE(&threads_head, thread, thread_info, entries);
        free(thread);
    }

    pthread_mutex_unlock(&file_mutex);
}

void *client_handler(void *arg) {
    int client_socket = *(int *)arg;
    char client_ip[INET_ADDRSTRLEN];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    getpeername(client_socket, (struct sockaddr *)&client_addr, &client_len);
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
    if (buffer == NULL) {
        syslog(LOG_ERR, "Memory allocation failed!");
        close(client_socket);
        return NULL;
    }

    ssize_t bytes_received;
    int data_file = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, FILE_PERMISSIONS);
    if (data_file == -1) {
        perror("File open failed");
        syslog(LOG_ERR, "File open failed: %m");
        free(buffer);
        close(client_socket);
        return NULL;
    }

    // while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0) > 0)) {
    //     pthread_mutex_lock(&file_mutex);
    //     write(data_file, buffer, bytes_received);
    //     pthread_mutex_unlock(&file_mutex);

    //     if (memchr(buffer, '\n', bytes_received) != NULL) {
    //         break;
    //     }
    // }
    while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        // Search for a newline character in the received data
        char *newline_ptr = memchr(buffer, '\n', bytes_received);

        if (newline_ptr != NULL) {
            // Calculate the length of the data up to the newline character
            ssize_t newline_index = newline_ptr - buffer + 1;
            // Lock the mutex and write this part of the data
            pthread_mutex_lock(&file_mutex);
            write(data_file, buffer, newline_index);
            pthread_mutex_unlock(&file_mutex);
            // Move the remaining data (after newline) to the beginning of the buffer
            memmove(buffer, newline_ptr + 1, bytes_received - newline_index);
            bytes_received -= newline_index;
        } else {
            // Lock the mutex and write the entire received data
            pthread_mutex_lock(&file_mutex);
            write(data_file, buffer, bytes_received);
            pthread_mutex_unlock(&file_mutex);
        }
    }



    buffer[bytes_received + 1] = '\0';
    syslog(LOG_INFO, "Received data from %s: %s, %d", client_ip, buffer, (int)bytes_received);
    
    close(data_file);

    close(client_socket);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    free(buffer);
    return NULL;
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

    // Initialize thread management list
    STAILQ_INIT(&threads_head);

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

    pthread_t timestamp_thread;
    pthread_create(&timestamp_thread, NULL, (void *(*)(void *))append_timestamp, NULL);

    while (1) 
    {
        // if (sig_handler_hit) {
        //     exit_safely();
        // }
        
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) {
            perror("Accept failed"); // Print an error message if accept fails
            syslog(LOG_ERR, "Accept failed: %m"); // Log accept error
        }
        else {
            pthread_t thread;
            int *client_socket_arg = (int *)malloc(sizeof(int));
            if (client_socket_arg != NULL) {
                *client_socket_arg = client_socket;
                pthread_create(&thread, NULL, client_handler, client_socket_arg);

                struct thread_info *new_thread = (struct thread_info *)malloc(sizeof(struct thread_info));
                if (new_thread != NULL) {
                    new_thread->thread_id = thread;
                    pthread_mutex_lock(&file_mutex);
                    STAILQ_INSERT_TAIL(&threads_head, new_thread, entries);
                    pthread_mutex_unlock(&file_mutex);
                } else {
                    syslog(LOG_ERR, "Memory allocation failed for thread_info");
                    free(client_socket_arg);
                    close(client_socket);
                }
            } else {
                syslog(LOG_ERR, "Memory allocation failed for client_socket_arg");
                close(client_socket);
            }
        }

        if (sig_handler_hit) {
            sig_handler_hit = false;
            exit_safely();
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
