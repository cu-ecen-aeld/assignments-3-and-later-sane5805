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

void *thread_func(void *thread_parameter);

// Function to handle signals
static void signal_handler(int signo);

// Function to daemonize the process
void daemonize();

void socket_connect(void);

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
void *thread_func(void *thread_parameter)
{

	// Package storage related variables
	bool packet_comp = false;
	int i, j = 0;
	int ret_recv = 0;
	int ret = 0;
	char buff[BUFFER_SIZE] = {0};
	char *output_buffer = NULL;
	// char *send_buffer = NULL;

	// get the parameter of the thread
	struct thread_data *params = (struct thread_data *)thread_parameter;

	// For test
	output_buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
	if (output_buffer == NULL)
	{
		printf("Malloc failed!\n");
		exit(1);
	}
	memset(output_buffer, 0, BUFFER_SIZE);
	// For test

	/*Packet reception, detection and storage logic*/
	while (packet_comp == false)
	{

		// printf("Receiving data from descriptor:%d.\n",sfd);

		ret_recv = recv(params->client_fd, buff, BUFFER_SIZE, 0); //**!check the flag
		if (ret_recv < 0)
		{
			printf("Error while receving data packets\n");
			syslog(LOG_ERR, "Error: Receiving failed =%s. Exiting ", strerror(errno));
			exit(EXIT_FAILURE);
		}
		else if (ret_recv == 0)
		{
			break;
		}

		/*Detect '\n' */
		for (i = 0; i < BUFFER_SIZE; i++)
		{

			if (buff[i] == '\n')
			{
				packet_comp = true;
				i++;
				printf("data packet receiving completed\n");
				syslog(LOG_DEBUG, "data packet received");
				break;
			}
		}
		j += i; // to get packet size of till null character received
		data_count += i;

		/*reallocate to a larger buffer now as static buffer can
			only accomodate upto fixed size*/
		output_buffer = (char *)realloc(output_buffer, (j + 1));
		if (output_buffer == NULL)
		{
			printf("Realloc failed\n");
			exit(1);
		}

		strncat(output_buffer, buff, j + 1);

		memset(buff, 0, BUFFER_SIZE);
	}
	int file_fd = open(file_path, O_CREAT | O_APPEND | O_RDWR); //opening file path 
	if (file_fd == -1)
	{
		printf("File open error for appending\n");
		exit(1);
	}

	while (1)
	{

		if (strncmp(output_buffer, "AESDCHAR_IOCSEEKTO:", strlen("AESDCHAR_IOCSEEKTO:")) == 0) // checking for command
		{
			printf("seekto command found \n");

			struct aesd_seekto seekto;
			char *token = strtok(output_buffer + strlen("AESDCHAR_IOCSEEKTO:"), ",");
			if (token == NULL)
			{
				syslog(LOG_DEBUG, "Error: Invalid write command\n");
				exit_func();
			}
			// extracting write command and write command offset
			seekto.write_cmd = strtoul(token, NULL, 10);
			token = strtok(NULL, ",");
			if (token == NULL)
			{
				syslog(LOG_DEBUG, "Error: Invalid write command\n");
				exit_func();
			}
			seekto.write_cmd_offset = strtoul(token, NULL, 10);

			syslog(LOG_DEBUG, "Command found:%s :%u, %u\n", "AESDCHAR_IOCSEEKTO", seekto.write_cmd, seekto.write_cmd_offset);
			// check for successful ioctl command
			if (ioctl(file_fd, AESDCHAR_IOCSEEKTO, &seekto) != 0)
			{
				syslog(LOG_DEBUG, "ioctl failed\n");
				exit_func();
			}
			else
			{
				syslog(LOG_DEBUG, "ioctl successful\n");
				printf("ioctl successful\n");
			}
		}

		// Step-6 Write the data received from client to the server if its not AESDCHAR_IOCSEEKTO command
		else
		{
#ifndef USE_AESD_CHAR_DEVICE
			ret = pthread_mutex_lock(&mutex_lock);

			if (ret)
			{
				printf("Mutex lock error before write\n");
				exit(1);
			}

#endif
			syslog(LOG_DEBUG, "writing to file \n");
			// printf("output buffer is %s\n", output_buffer);
			int writeret = write(file_fd, output_buffer, strlen(output_buffer));

			if (writeret == -1)
			{
				printf("Error write\n");
				exit(1);
			}
#ifndef USE_AESD_CHAR_DEVICE
			ret = pthread_mutex_unlock(&mutex_lock);

			if (ret)
			{
				printf("Mutex unlock error after read/send\n");
				exit(1);
			}
#endif
		}
		break;
	}
	// Step-7 Reading from the file & Sending to the client with the accept fd
	char send_buffer[BUFFER_SIZE];
	memset(&send_buffer[0], 0, BUFFER_SIZE);
	syslog(LOG_DEBUG, "reading from file n");
	while (1)
	{ // for reading and writing to socket

		ret = read(file_fd, send_buffer, BUFFER_SIZE);
		// read until no characters left
		if (ret <= 0)
			break;

		send(params->client_fd, send_buffer, strlen(send_buffer), 0); // send back to socket
	}
	// printf("send buffer is %s\n", send_buffer);

	// exit_thread:
	close(file_fd);
	params->thread_complete = true;

	close(params->client_fd);
	// Free the allocated buffer
	free(output_buffer);

	return params;
}
// void* thread_func(void *thread_param) {

//     struct thread_data *thread_func_args = (struct thread_data *)thread_param;

//     char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
//     if (buffer == NULL) {
//         syslog(LOG_ERR, "Memory allocation failed"); // Log memory allocation error
//         exit(EXIT_FAILURE); // Return an error status
//     }
//     memset(buffer, 0, BUFFER_SIZE);

// 	// Receive and append data
//     int data_file = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, FILE_PERMISSIONS);
//     if (data_file == -1) {
//         perror("File open failed"); // Print an error message if file open fails
// 		syslog(LOG_ERR, "File open failed: %m"); // Log file open error
//         free(buffer);
//         exit(EXIT_FAILURE);
//     }

// 	ssize_t bytes_received;
//     while ((bytes_received = recv(thread_func_args->client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
//         write(data_file, buffer, bytes_received);
//         if (memchr(buffer, '\n', bytes_received) != NULL) {
//             break;
//         }
//     }

//     buffer[bytes_received+1] = '\0';

//     syslog(LOG_INFO, "Received data from client: %s, %d", buffer, (int)bytes_received);

//     close(data_file);

//     lseek(data_file, 0, SEEK_SET);


// #ifdef USE_AESD_CHAR_DEVICE

//     char* output_buffer = NULL;
// 	while (1)
// 	{
// 		if (strncmp(output_buffer, "AESDCHAR_IOCSEEKTO:", strlen("AESDCHAR_IOCSEEKTO:")) == 0) // checking for command
// 		{
// 			printf("seekto command found \n");

// 			struct aesd_seekto seekto;
// 			char *token = strtok(output_buffer + strlen("AESDCHAR_IOCSEEKTO:"), ",");
// 			if (token == NULL)
// 			{
// 				syslog(LOG_DEBUG, "Error: Invalid write command\n");
// 				exit_safely();
// 			}

//             // extracting write command and write command offset
// 			seekto.write_cmd = strtoul(token, NULL, 10);
// 			token = strtok(NULL, ",");
// 			if (token == NULL)
// 			{
// 				syslog(LOG_DEBUG, "Error: Invalid write command\n");
// 				exit_safely();
// 			}
// 			seekto.write_cmd_offset = strtoul(token, NULL, 10);

// 			syslog(LOG_DEBUG, "Command found:%s :%u, %u\n", "AESDCHAR_IOCSEEKTO", seekto.write_cmd, seekto.write_cmd_offset);

//             // check for successful ioctl command
// 			if (ioctl(file_fd, AESDCHAR_IOCSEEKTO, &seekto) != 0)
// 			{
// 				syslog(LOG_DEBUG, "ioctl failed\n");
// 				exit_safely();
// 			}
// 			else
// 			{
// 				syslog(LOG_DEBUG, "ioctl successful\n");
// 				printf("ioctl successful\n");
// 			}
// 		}

// #endif
// // Step-6 Write the data received from client to the server
// 		else
// 		{
// #ifndef USE_AESD_CHAR_DEVICE
// 			ret = pthread_mutex_lock(&mutex_lock);

// 			if (ret)
// 			{
// 				printf("Mutex lock error before write\n");
// 				exit(1);
// 			}
// #endif
// 			syslog(LOG_DEBUG, "writing to file \n");
// 			// printf("output buffer is %s\n", output_buffer);
// 			int writeret = write(file_fd, output_buffer, strlen(output_buffer));

// 			if (writeret == -1)
// 			{
// 				printf("Error write\n");
// 				exit(1);
// 			}

// #ifndef USE_AESD_CHAR_DEVICE
// 			ret = pthread_mutex_unlock(&mutex_lock);

// 			if (ret)
// 			{
// 				printf("Mutex unlock error after read/send\n");
// 				exit(1);
// 			}
// #endif
// 		}
// 		break;
// 	}


// 	// Step-7 Reading from the file & Sending to the client with the accept fd
// 	char send_buffer[BUFFER_SIZE];
// 	memset(&send_buffer[0], 0, BUFFER_SIZE);
// 	syslog(LOG_DEBUG, "reading from file n");
// 	while (1)
// 	{// for reading and writing to socket

// 		int ret = read(file_fd, send_buffer, BUFFER_SIZE);
//         // read until no characters left
// 		if (ret <= 0)
// 			break;

// 		write(thread_func_args->client_socket, send_buffer, ret);// send back to socket
// 	}
// 	printf("send buffer is %s\n", send_buffer);

// 	// exit_thread:
// 	close(file_fd);
// 	thread_func_args->thread_complete = true;

// 	close(thread_func_args->client_socket);
// 	// Free the allocated buffer
// 	free(output_buffer);

//     // else {
// 	// // Send data back to the client
//     // data_file = open(DATA_FILE, O_RDONLY);
//     // if (data_file == -1) {
//     //     perror("file open failed"); // Print an error message if file open fails
// 	// 	syslog(LOG_ERR, "File open failed: %m"); // Log file open error
//     //     free(buffer);
//     //     exit(EXIT_FAILURE);
//     // }

//     // ssize_t bytes_read;
//     // memset(buffer, 0, sizeof(char) * BUFFER_SIZE);

//     // while ((bytes_read = read(data_file, buffer, sizeof(char) * BUFFER_SIZE)) > 0) {
//     //     send(thread_func_args->client_socket, buffer, bytes_read, 0);
//     // }

//     // close(data_file);

// 	// // Thread completed successfully
//     // thread_func_args->thread_complete = true;
//     // // pthread_exit(thread_func_args);

//     // }

//     // free(buffer);
    
//     // close(thread_func_args->client_socket);

//     // syslog(LOG_INFO, "Closed connection from client");

//     return thread_func_args;
// }

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
// int main(int argc, char **argv) {

//     // Parse command-line arguments
//     if (argc > 1 && strcmp(argv[1], "-d") == 0) 
//     {
//         daemon_mode = 1; // Set daemon_mode to 1 if "-d" option is provided
//     } 

//     // Initialize syslog
//     openlog(NULL, 0, LOG_USER); // Open syslog with LOG_USER facility

//     syslog(LOG_INFO, "Socket started!"); // Log that the socket has started

//     // Signal handling
//     signal(SIGINT, signal_handler); // Register signal_handler for SIGINT
//     signal(SIGTERM, signal_handler); // Register signal_handler for SIGTERM
    
//     // Daemon mode
//     if (daemon_mode) 
//     {
//         syslog(LOG_DEBUG, "Daemon created!"); // Log that a daemon was created

//         daemonize(); // Call the daemonize function to daemonize the process
//     }

// 	pthread_mutex_init(&mutex_lock, NULL);
	
//     SLIST_INIT(&head);

//     // Create socket
//     server_socket = socket(AF_INET, SOCK_STREAM, 0); // Create a socket
//     if (server_socket == -1) {
//         perror("Socket creation failed"); // Print an error message if socket creation fails
//         syslog(LOG_ERR, "Error creating socket: %m"); // Log socket creation error
//         return -1; // Return an error status
//     }

//     if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value)) == -1) 
//     {
//         perror("setsockopt"); // Print an error message if setsockopt fails
//         syslog(LOG_ERR, "Error setting socket options: %m"); // Log setsockopt error
//         close(server_socket); // Close the server socket
//         return -1; // Return an error status
//     }

//     struct sockaddr_in server_addr;
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(PORT);
//     server_addr.sin_addr.s_addr = INADDR_ANY; 

//     // Bind socket to port 9000
//     if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
//         perror("Socket bind failed"); // Print an error message if socket bind fails
//         syslog(LOG_ERR, "Socket bind failed: %m"); // Log socket bind error
//         close(server_socket); // Close the server socket
//         return -1; // Return an error status
//     }

// 	// Listen for incoming connections
// 	if (listen(server_socket, 10) == -1) {
// 		perror("Listen failed"); // Print an error message if listen fails
// 		syslog(LOG_ERR, "Listen failed: %m"); // Log listen error
// 		close(server_socket); // Close the server socket
// 		return -1; // Return an error status
// 	}

//     while (!sig_handler_hit) 
//     {
//         // if (sig_handler_hit) {
//         //     exit_safely();
//         // }

// 		// timer thread should run  once in parent!
//         #ifndef USE_AESD_CHAR_DEVICE
//         if (!timer_thread_flag) {
// 			pthread_create(&timer_thread, NULL, timer_handler, NULL);
// 			timer_thread_flag = true;
// 		}
//         #endif

//         struct sockaddr_in client_addr;
//         socklen_t client_len = sizeof(client_addr);
        
//         // Accept incoming connection
//         client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
//         if (client_socket == -1) {
//             perror("Accept failed"); // Print an error message if accept fails
//             syslog(LOG_ERR, "Accept failed: %m"); // Log accept error
//         }
//         else {
//             char client_ip[INET_ADDRSTRLEN];

//             getpeername(client_socket, (struct sockaddr *)&client_addr, &client_len);

//             inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

//             // Log accepted connection
//             syslog(LOG_INFO, "Accepted connection from %s", client_ip);

// 			/* rest is moved inside the thread handler */ 
            
// 			/* NODE CREATION FOR CURRENT CONNECTION */ 
// 			// Allocating memory for the connection (List's node)
// 		    datap = (slist_data_t *)malloc(sizeof(slist_data_t));

// 			// Initializing the node with current connection's data
//             datap->connection_data_node.client_socket = client_socket;

// 			// Initially thread complete flag will be false
//             datap->connection_data_node.thread_complete = false;

// 			// Insert node in the list
//             SLIST_INSERT_HEAD(&head, datap, entries);

// 			// Spawning a new thread for current connection
//             pthread_create(&(datap->connection_data_node.thread_id), // ID returned by pthread_create()
//                             NULL,
//                             thread_func,
//                             &datap->connection_data_node
// 		    );

// 			// Joining all the completed connection threads 
//             SLIST_FOREACH(datap, &head, entries) {
//                 if (datap->connection_data_node.thread_complete) {
//                     pthread_join(datap->connection_data_node.thread_id, NULL);

// 					syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
// 		    		printf("Closed connection from %s\n", client_ip);

// 					// removing the node from list and freeing it
//                     SLIST_REMOVE(&head, datap, slist_data_s, entries);
//                     free(datap);

//                     break;
//                 }
//             }

//             // syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
// 		    // printf("Closed connection from %s\n", client_ip);
//         }
//     }

//     close(server_socket);
//     if (unlink(DATA_FILE) == -1) {
//         syslog(LOG_ERR, "Error removing data file: %m"); // Log error when removing data file
//     }
//     close(client_socket);
// 	closelog();

// #ifndef USE_AESD_CHAR_DEVICE
// 	if (timer_thread) {
// 		pthread_join(timer_thread, NULL);
// 	}
// #endif

//     return 0;
// }
int main(int argc, char *argv[])
{

	// open the log file
	openlog("A6P1", LOG_PID, LOG_USER);

	syslog(LOG_DEBUG, "syslog opened."); // indicating logging
	// to associate signal handler with corresponding signals using signal() API
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGKILL, signal_handler);

	pthread_mutex_init(&mutex_lock, NULL);

	// Check the actual value of argv here:
	if ((argc > 1) && (!strcmp("-d", (char *)argv[1])))
	{

		printf("Running in daemon mode!\n");
		syslog(LOG_DEBUG, "aesdsocket entering daemon mode");

		deamon_flag = 1;
	}

	socket_connect();

	// closing syslog
	closelog();

	return 0;
}

/*SOCKET COMMUNICATION FUNCTION*/
/*
 * @function	:  To handle socket communication
 *
 * @param		:   struct addrinfo *res holds the local address for binding,
 * 					int socket_fd- socket file descriptor and int accept_fd -file descriptor of client
 * @return		:  NULL
 *
 */
void socket_connect()
{

	// setting the initial paramters
	struct addrinfo hints;		// for getaddrinfo parameters
	struct addrinfo *res;		// to get the address
	struct sockaddr client_add; // to get client address
	socklen_t client_size;		// size of sockaddr
	// new variables for A6-P1
	SLIST_INIT(&head);

	// 1. Set the sockaddr using getaddrinfo

	// clear the hints first
	memset(&hints, 0, sizeof(hints));

	// set all the hint parameters then
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	// store the result
	int getret = getaddrinfo(NULL, server_port, &hints, &res);
	if (getret != 0)
	{
		printf("Error while allocating address for socket\n");
		syslog(LOG_ERR, "Error while setting socket address= %s. Exiting.", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Step-2 Opening socket
	printf("Opening socket\n");
	socket_fd = socket(PF_INET, SOCK_STREAM, 0); // IP family with type SOCK_STREAM and 0 protocol
	if (socket_fd == -1)						 // generating error
	{
		printf("Error: Socket file descriptor not created\n");
		syslog(LOG_ERR, "Error while setting socket= %s. Exiting.", strerror(errno));
		freeaddrinfo(res);
		exit(EXIT_FAILURE);
	}

	int socket_ret = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
	if (socket_ret < 0)
	{
		printf("Error: setsockopt failed\n");
		syslog(LOG_ERR, "Error: setsockopt failed");
		printf("setsockopt error: %s\n", strerror(errno));
		freeaddrinfo(res);
		exit(1);
	}

	// Step-3 Binding to address
	printf("Binding socket descriptor to address\n");
	int temp2 = bind(socket_fd, res->ai_addr, res->ai_addrlen);
	if (temp2 == -1) // generating error
	{
		printf("Error: Binding with address failed\n");
		syslog(LOG_ERR, "Error while binding socket= %s. Exiting.", strerror(errno));
		freeaddrinfo(res);
		exit(EXIT_FAILURE);
	}

	// Create file
	file_fd = creat(file_path, 0644);
	if (file_fd == -1)
	{
		printf("Error while creating file \n");
		syslog(LOG_ERR, "Error: File could not be created!= %s. Exiting...", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// close fd after creating
	close(file_fd);

	// free after use
	freeaddrinfo(res);

	if (deamon_flag == 1)
	{
		int temp_daemon = daemon(0, 0);
		if (temp_daemon == -1)
		{
			printf("Couldn't process into deamon mode\n");
			syslog(LOG_ERR, "failed to enter deamon mode %s", strerror(errno));
		}
	}
#ifndef USE_AESD_CHAR_DEVICE
	bool timer_thread_flag = false;
#endif
	while (process_flag == false)
	{
#ifndef USE_AESD_CHAR_DEVICE
		if (!timer_thread_flag)
		{
			pthread_create(&timer_thread, NULL, timer_handler, NULL);
			timer_thread_flag = true;
		}
#endif
		// step-4 Listening for client
		int temp_listen = listen(socket_fd, MAX_BACKLOG);
		if (temp_listen == -1) // generating error
		{
			printf("Error while listening \n");
			syslog(LOG_ERR, "Error: Listening failed =%s. Exiting ", strerror(errno));
			freeaddrinfo(res);
			exit(EXIT_FAILURE);
		}

		client_size = sizeof(struct sockaddr);

		// step -5 Accepting connection
		accept_fd = accept(socket_fd, (struct sockaddr *)&client_add, &client_size);
		if (accept_fd == -1) // generating error
		{
			printf("Error while accepting \n");
			syslog(LOG_ERR, "Error: Accepting failed =%s. Exiting ", strerror(errno));
			exit(EXIT_FAILURE);
		}
		// to get the client address in a readable format
		struct sockaddr_in *addr_in = (struct sockaddr_in *)&client_add;
		char *addr_ip = inet_ntoa(addr_in->sin_addr); // using inet_ntoa function

		syslog(LOG_DEBUG, "Connection succesful. Accepting connection from %s", addr_ip);
		printf("Connection succesful.Accepting connection from %s\n", addr_ip);

		/*Adding below part for A6-P1*/

		// allocating new node for the data
		datap = (slist_data_t *)malloc(sizeof(slist_data_t));

		SLIST_INSERT_HEAD(&head, datap, entries);

		// Inserting thread parameters now
		datap->thread_socket.client_fd = accept_fd;
		datap->thread_socket.thread_complete = false;

		pthread_create(&(datap->thread_socket.thread_id), // the thread id to be created
					   NULL,							  // the thread attribute to be passed
					   thread_handler,					  // the thread handler to be executed
					   &datap->thread_socket			  // the thread parameter to be passed
		);

		printf("Threads created now waiting to exit\n");

		SLIST_FOREACH(datap, &head, entries)
		{

			if (datap->thread_socket.thread_complete == true)
			{
				pthread_join(datap->thread_socket.thread_id, NULL);
				SLIST_REMOVE(&head, datap, slist_data_s, entries);
				free(datap);
				break;
			}
		}

		printf("All thread exited!\n");

		syslog(LOG_DEBUG, "Closed connection from %s", addr_ip);
		printf("Closed connection from %s\n", addr_ip);
	}

	// 9. Close sfd, accept_fd
	close(accept_fd);
	close(socket_fd);
}