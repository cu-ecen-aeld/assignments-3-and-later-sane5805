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
#include <pthread.h>
#include <sys/time.h>
#include "queue.h"
#include <errno.h>
#include <netdb.h>
#include "./../aesd-char-driver/aesd_ioctl.h"

#define PORT 9000

// #define DATA_FILE "/var/tmp/aesdsocketdata"

#define BUFFER_SIZE 1024
#define ERROR_MEMORY_ALLOC -6
#define FILE_PERMISSIONS 0644

#define USE_AESD_CHAR_DEVICE 1

#ifdef USE_AESD_CHAR_DEVICE
// char *DATA_FILE = "/dev/aesdchar";
char *file_path = "/dev/aesdchar";
#endif

#ifndef USE_AESD_CHAR_DEVICE
// char *DATA_FILE = "/var/tmp/aesdsocketdata";
char *file_path = "/var/tmp/aesdsocketdata";
#endif

#define MAX_BACKLOG (10)

int server_socket;
int client_socket;
int daemon_mode = 0;
int option_value = 1;

int file_fd = 0;
// int data_count = 0;	
// int msg_flag = 0;

char *server_port = "9000"; // given port for communication
int socket_fd = 0;			// socket file descriptor
int accept_fd = 0;			// client accept file descriptor
int data_count = 0;			// for counting the data packet bytes
// int file_fd = 0;			// file as defined in path to be created
bool process_flag = false;
int deamon_flag = 0;


bool sig_handler_hit = false;

#ifndef USE_AESD_CHAR_DEVICE
bool timer_thread_flag = false;
#endif

#ifndef USE_AESD_CHAR_DEVICE
pthread_t timer_thread = (pthread_t)NULL;
#endif

void *thread_func(void *thread_param);

// Function to handle signals
static void signal_handler(int signo);

// Function to daemonize the process
void daemonize();

void socket_connection_operations(void);

void exit_safely();

int main(int argc, char **argv);

//  Thread parameter structure
struct thread_data {
	pthread_t thread_id; // ID returned by pthread_create()
	int client_socket; // holds the current client socket identifier

    /**
     * Set to true if the thread completed with success, false
     * if an error occurred.
     */
	bool thread_complete;
};

// SLIST.
typedef struct slist_data_s slist_data_t;
struct slist_data_s {
	struct thread_data connection_data_node;
	SLIST_ENTRY(slist_data_s) entries;
};

slist_data_t *datap = NULL;

SLIST_HEAD(slisthead, slist_data_s) head;
// SLIST_INIT(&head);

pthread_mutex_t mutex_lock = PTHREAD_MUTEX_INITIALIZER;


void exit_safely() {
	shutdown(server_socket, SHUT_RDWR);
	// unlink(DATA_FILE);
    close(server_socket); // Close the server socket
    close(client_socket); // Close the client socket
    closelog(); // Close syslog

    remove(file_path); // Remove the data file

#ifndef USE_AESD_CHAR_DEVICE
	if (timer_thread) {
		pthread_join(timer_thread, NULL);
	}
#endif

    exit(0); // Exit the program
}

/**
 * @function: signal_handler
 * @brief: Handles signals like SIGINT and SIGTERM.
 * @params:
 *    - signo: The signal number.
 * @return: None
 */
 // done
static void signal_handler(int signo) {
	if (signo == SIGINT || signo == SIGTERM) {

		sig_handler_hit = true;

		syslog(LOG_INFO, "Caught signal, exiting"); // Log that a signal was caught
		
		// called here to avoid failures in full-test
		exit_safely();
	}
}

/**
 * @name: time_handler
 * @brief: TIMER handler function for handling time and printing time
 * @param: int signal_no : signal number
 * @return: NULL
 *
 */
#ifndef USE_AESD_CHAR_DEVICE
static void *timer_handler(void *signalno) {

	while (1)
	{
		char timestampBuffer[100];

		// Get current time
		time_t currentTime = time(NULL);

		// Convert time to local time
		struct tm * time_structure = localtime(&currentTime);
		if (time_structure == NULL) {
			perror("convertion of time to localtime failed");
			exit(EXIT_FAILURE);
		}

		// Format the timestamp
		int timestampLength = strftime(timestampBuffer, sizeof(timestampBuffer), "timestamp:%y %b %d	%k:%M:%S\n", time_structure);
		if (timestampLength == 0) {
			perror("Formating of timestamp failed");
			exit(EXIT_FAILURE);
		}

		// printing timestamp before dumping in file
		printf("timestamp: %s\n", timestampBuffer);

		/* Appending timestamp onto the file */

		// Open or create the data file for writing, and append data
		int data_file = open(file_path, O_WRONLY | O_CREAT | O_APPEND, FILE_PERMISSIONS);
		if (data_file == -1) {
			perror("File open failed"); // Print an error message if file open fails
			syslog(LOG_ERR, "File open failed: %m"); // Log file open error
			exit(EXIT_FAILURE);
		}

		// Obtain the mutex lock to protect shared resources
		if (pthread_mutex_lock(&mutex_lock) != 0) {
			exit(EXIT_FAILURE);
		}

		// Write the timestamp to the data file
		int writeStatus = write(data_file, timestampBuffer, timestampLength);

		// Release the mutex lock
		if (pthread_mutex_unlock(&mutex_lock) != 0) {
			exit(EXIT_FAILURE);
		}

		if (writeStatus == -1) {
			printf("Error writing to the file");
			exit(EXIT_FAILURE);
		}

		// Close the data file
		close(data_file);

		// The string should be appended to the /var/tmp/aesdsocketdata file every 10 seconds.
		// Sleep for 10 seconds before the next iteration
		sleep(10);
	}

	// Thread completed successfully
	pthread_exit(NULL);
}
#endif

/**
 * @name: thread_func
 * @brief: Thread handler function for receiving and sending data
 * @params: void *thread_parameters: thread parameters
 * @return: void *thread_parameters
 */
void *thread_func(void *thread_param)
{
	bool is_packet_complete = false;
	int index, extended_index = 0;

	char buff[BUFFER_SIZE] = {0};


	struct thread_data *thread_func_args = (struct thread_data *)thread_param;

	char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
	if (buffer == NULL)	{
        syslog(LOG_ERR, "Memory allocation failed"); // Log memory allocation error
        exit(EXIT_FAILURE); // Return an error status
	}
	memset(buffer, 0, BUFFER_SIZE);

	while (!is_packet_complete)	{

		int recv_result = recv(thread_func_args->client_socket, buff, BUFFER_SIZE, 0); //**!check the flag
		
		if (recv_result < 0) {

			syslog(LOG_ERR, "Data recieve failed =%s.", strerror(errno));
			exit(EXIT_FAILURE);
		}
		else if (recv_result == 0) {

			break;
		}

		// finding the \n
		for (index = 0; index < BUFFER_SIZE; index++) {
			if (buff[index] == '\n') {
				is_packet_complete = true;
				index++;
				printf("data packet receiving completed\n");
				syslog(LOG_DEBUG, "data packet received");
				break;
			}
		}
		
		extended_index = index;
		data_count += index;

		buffer = (char *)realloc(buffer, (extended_index + 1));

		if (buffer == NULL)	{

			printf("Realloc failed\n");
			exit(1);
		}

		strncat(buffer, buff, extended_index + 1);

		memset(buff, 0, BUFFER_SIZE);
	}

	int data_file = open(file_path, O_CREAT | O_APPEND | O_RDWR);
	if (data_file == -1) {

		printf("file open failed");
		exit(1);
	}

	while (1) {

		if(!strncmp(buffer, "AESDCHAR_IOCSEEKTO:", strlen("AESDCHAR_IOCSEEKTO:")))	{

			// here means command is found

			struct aesd_seekto seekto;

			char *token = strtok(buffer + strlen("AESDCHAR_IOCSEEKTO:"), ",");

			if(token == NULL) {
				syslog(LOG_DEBUG, "Error: Invalid write command\n");
				exit_safely();
			}

			seekto.write_cmd = strtoul(token, NULL, 10);
			token = strtok(NULL, ",");

			if(token == NULL) {

				syslog(LOG_DEBUG, "Error: Invalid write command\n");
				exit_safely();
			}

			seekto.write_cmd_offset = strtoul(token, NULL, 10);

			syslog(LOG_DEBUG, "command seen:%s :%u, %u\n", "AESDCHAR_IOCSEEKTO", seekto.write_cmd, seekto.write_cmd_offset);
			
			if (ioctl(data_file, AESDCHAR_IOCSEEKTO, &seekto) != 0) {

				syslog(LOG_DEBUG, "ioctl failure");
				exit_safely();
			}
			else {
				
				syslog(LOG_DEBUG, "ioctl success\n");
				printf("ioctl success\n");
			}
		}
		else {

#ifndef USE_AESD_CHAR_DEVICE
			int ret = pthread_mutex_lock(&mutex_lock);

			if (ret) {

				printf("Lock mutex");
				exit(1);
			}
#endif
			syslog(LOG_DEBUG, "write to file started");

			int write_result = write(data_file, buffer, strlen(buffer));

			if (write_result == -1)	{

				printf("Error write\n");
				exit(1);
			}

#ifndef USE_AESD_CHAR_DEVICE
			ret = pthread_mutex_unlock(&mutex_lock);

			if (ret) {

				printf("Mutex unlock error after read/send\n");
				exit(1);
			}
#endif
		}
		break;
	}

	
	char send_buffer[BUFFER_SIZE];
	memset(&send_buffer[0], 0, BUFFER_SIZE);
	syslog(LOG_DEBUG, "reading from file n");
	
	while (1) {

		int  read_result = read(data_file, send_buffer, BUFFER_SIZE);
		
		if (read_result <= 0) { break; }

		send(thread_func_args->client_socket, send_buffer, strlen(send_buffer), 0);
	}

	free(buffer);
	close(data_file);
	close(thread_func_args->client_socket);
	syslog(LOG_INFO, "Closed connection from client");
	thread_func_args->thread_complete = true;
	// pthread_exit(thread_func_args);

	return thread_func_args;
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
int main(int argc, char *argv[])
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

	pthread_mutex_init(&mutex_lock, NULL);

	socket_connection_operations();

	// closing syslog
	closelog();

	return 0;
}


// Function to handle socket connection operations
void socket_connection_operations()
{
    // Initialize variables
    struct addrinfo hints;
    struct addrinfo *result;
    struct sockaddr client_address;
    socklen_t client_size;

    // Initialize linked list head
    SLIST_INIT(&head);

    // Set up hints for getaddrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    // Get address information
    int get_ret = getaddrinfo(NULL, server_port, &hints, &result);
    if (get_ret != 0) {
        printf("Error: Failed to allocate address for the server socket\n");
        syslog(LOG_ERR, "Error: Failed to set server socket address: %s. Exiting.", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Create server socket
    printf("Creating the server socket\n");
    int server_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        printf("Error: Failed to create the server socket file descriptor\n");
        syslog(LOG_ERR, "Error: Failed to set server socket: %s. Exiting.", strerror(errno));
        freeaddrinfo(result);
        exit(EXIT_FAILURE);
    }

    // Set server socket option to reuse address
    int socket_ret = setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    if (socket_ret < 0) {
        printf("Error: setsockopt failed for the server socket\n");
        syslog(LOG_ERR, "Error: setsockopt failed for the server socket: %s", strerror(errno));
        freeaddrinfo(result);
        exit(EXIT_FAILURE);
    }

    // Bind server socket to address
    printf("Binding the server socket descriptor to the address\n");
    int bind_ret = bind(server_socket, result->ai_addr, result->ai_addrlen);
    if (bind_ret == -1) {
        printf("Error: Failed to bind the server socket with the address\n");
        syslog(LOG_ERR, "Error: Failed to bind the server socket: %s. Exiting.", strerror(errno));
        freeaddrinfo(result);
        exit(EXIT_FAILURE);
    }

    // Create a file for writing
    int file_descriptor = creat(file_path, 0644);
    if (file_descriptor == -1) {
        printf("Error: Failed to create a file\n");
        syslog(LOG_ERR, "Error: File creation failed: %s. Exiting...", strerror(errno));
        exit(EXIT_FAILURE);
    }
    close(file_descriptor);

    // Free address info
    freeaddrinfo(result);

    // Daemonize the process if the flag is set
    if (daemon_flag == 1) {
        int daemon_ret = daemon(0, 0);
        if (daemon_ret == -1) {
            printf("Error: Unable to enter daemon mode\n");
            syslog(LOG_ERR, "Failed to enter daemon mode: %s", strerror(errno));
        }
    }

#ifndef USE_AESD_CHAR_DEVICE
    // Initialize timer thread flag if not using char device
    bool timer_thread_flag = false;
#endif

    // Main loop for handling connections
    while (!process_flag) {
#ifndef USE_AESD_CHAR_DEVICE
        // Create timer thread if not created yet
        if (!timer_thread_flag) {
            pthread_create(&timer_thread, NULL, timer_handler, NULL);
            timer_thread_flag = true;
        }
#endif

        // Listen for incoming connections
        int listen_ret = listen(server_socket, MAX_BACKLOG);
        if (listen_ret == -1) {
            printf("Error: Failed while listening for incoming connections\n");
            syslog(LOG_ERR, "Error: Listening for incoming connections failed: %s. Exiting", strerror(errno));
            freeaddrinfo(result);
            exit(EXIT_FAILURE);
        }

        // Accept incoming connection
        client_size = sizeof(struct sockaddr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_size);
        if (client_socket == -1) {
            printf("Error: Failed while accepting incoming connection\n");
            syslog(LOG_ERR, "Error: Accepting incoming connection failed: %s. Exiting", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // Get client IP address
        struct sockaddr_in *addr_in = (struct sockaddr_in *)&client_address;
        char *addr_ip = inet_ntoa(addr_in->sin_addr);

        // Log and print successful connection
        syslog(LOG_DEBUG, "Connection successful. Accepted connection from %s", addr_ip);
        printf("Connection successful. Accepted connection from %s\n", addr_ip);

        // Allocate memory for connection data node
        slist_data_t *connection_data = (slist_data_t *)malloc(sizeof(slist_data_t));

        // Insert connection data node into linked list
        SLIST_INSERT_HEAD(&head, connection_data, entries);

        // Initialize connection data node
        connection_data->connection_data_node.client_socket = client_socket;
        connection_data->connection_data_node.thread_complete = false;

        // Create thread for handling the connection
        pthread_create(&(connection_data->connection_data_node.thread_id),
                       NULL,
                       thread_func,
                       &(connection_data->connection_data_node)
        );

        // Log and print thread creation message
        printf("Thread created. Now waiting to exit\n");

        // Iterate through linked list to wait for thread completion
        SLIST_FOREACH(connection_data, &head, entries) {
            if (connection_data->connection_data_node.thread_complete == true) {
                pthread_join(connection_data->connection_data_node.thread_id, NULL);
                SLIST_REMOVE(&head, connection_data, slist_data_s, entries);
                free(connection_data);
                break;
            }
        }

        // Log and print connection closure message
        syslog(LOG_DEBUG, "Closed connection from %s", addr_ip);
        printf("Closed connection from %s\n", addr_ip);
    }

    // Close sockets
    close(client_socket);
    close(server_socket);
}
