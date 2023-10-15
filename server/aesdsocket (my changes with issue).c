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

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024
#define ERROR_MEMORY_ALLOC   -6
#define FILE_PERMISSIONS 0644

int server_socket;
int client_socket;
int daemon_mode = 0;
int option_value = 1;

int file_fd = 0;
int data_count = 0;	

bool sig_handler_hit = false;

void *thread_handler(void *thread_parameter);
pthread_t timer_thread = (pthread_t)NULL;

// Function to handle signals
static void signal_handler(int signo);

// Function to daemonize the process
void daemonize();

void socket_connect(void);

void exit_safely();

int main(int argc, char **argv);

void exit_func(void);

//  Thread parameter structure
typedef struct
{
	bool thread_complete;
	pthread_t thread_id;
	int client_fd;
	
} thread_ipc;

// Linked list node
struct slist_data_s
{
	thread_ipc thread_socket;
	SLIST_ENTRY(slist_data_s)
	entries;
};
typedef struct slist_data_s slist_data_t;
slist_data_t *datap = NULL;
pthread_mutex_t mutex_lock = PTHREAD_MUTEX_INITIALIZER;

SLIST_HEAD(slisthead, slist_data_s)
head; // Assigning head for struct


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
    // if (signo == SIGINT || signo == SIGTERM) {

    //     sig_handler_hit = true;

    //     // called here to avoid failures in full-test
    //     exit_safely();
            
    //     syslog(LOG_INFO, "Caught signal, exiting"); // Log that a signal was caught
    // }

	if (signo == SIGINT || signo == SIGTERM || signo == SIGKILL)
	{
		printf("signal detected to exit\n");
		syslog(LOG_DEBUG, "Caught the signal, exiting...");
		shutdown(server_socket, SHUT_RDWR);
		sig_handler_hit = true;
		unlink(DATA_FILE);
		close(server_socket);
		close(client_socket);
	}
	// _exit(0);
	 exit_safely();
}

/*TIMER HANDLER*/
/*
 * @function	:  TIMER handler function for handling time and printing time
 *
 * @param		:  int signal_no : signal number
 * @return		:  NULL
 *
 */
static void *timer_handler(void *signalno)
{
	while (1)
	{
		sleep(10);
		/*first store the local time in a buffer*/
		char time_stamp[200];
		time_t timer_init;
		struct tm *tm_info;
		int timer_len = 0;

		timer_init = time(NULL);
		tm_info = localtime(&timer_init);
		if (tm_info == NULL)
		{
			perror("Local timer error!");
			exit(EXIT_FAILURE);
		}

		timer_len = strftime(time_stamp, sizeof(time_stamp), "timestamp:%d.%b.%y - %k:%M:%S\n", tm_info);
		if (timer_len == 0)
		{
			perror("strftimer returned 0!");
			exit(EXIT_FAILURE);
		}

		printf("timestamp:%s\n", time_stamp);

		// writing to file

		file_fd = open(DATA_FILE, O_APPEND | O_WRONLY);
		if (file_fd == -1)
		{
			printf("Error opening\n");
			exit(EXIT_FAILURE);
		}

		int ret = pthread_mutex_lock(&mutex_lock);
		if (ret)
		{
			printf("Mutex lock error before write\n");
			exit(EXIT_FAILURE);
		}

		int write_ret = write(file_fd, time_stamp, timer_len);
		ret = pthread_mutex_unlock(&mutex_lock);
		if (ret)
		{
			printf("Mutex unlock error after write\n");
			exit(EXIT_FAILURE);
		}
		if (write_ret == -1)
		{
			printf("Error write\n");
			exit(EXIT_FAILURE);
		}
		/*update the global packet size variable*/
		data_count += timer_len;

		close(file_fd);
	}
	pthread_exit(NULL);
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
    signal(SIGKILL, signal_handler);
    
    pthread_mutex_init(&mutex_lock, NULL);

    // Daemon mode
    if (daemon_mode) 
    {
        syslog(LOG_DEBUG, "Daemon created!"); // Log that a daemon was created

        daemonize(); // Call the daemonize function to daemonize the process
    }

    socket_connect();

    // closelog();

	exit_safely();

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
    SLIST_INIT(&head);

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0); // Create a socket
    if (server_socket == -1) {
        perror("Socket creation failed"); // Print an error message if socket creation fails
        syslog(LOG_ERR, "Error creating socket: %m"); // Log socket creation error
        // return -1; // Return an error status
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value)) == -1) 
    {
        perror("setsockopt"); // Print an error message if setsockopt fails
        syslog(LOG_ERR, "Error setting socket options: %m"); // Log setsockopt error
        close(server_socket); // Close the server socket
        // return -1; // Return an error status
        exit(EXIT_FAILURE);
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
        // return -1; // Return an error status
        exit(EXIT_FAILURE);
    }

    bool timer_thread_flag = false;
    while (!sig_handler_hit) 
    {
        // if (sig_handler_hit) {
        //     exit_safely();
        // }

        if (!timer_thread_flag)
		{
			pthread_create(&timer_thread, NULL, timer_handler, NULL);
			timer_thread_flag = true;
		}
        
        // Listen for incoming connections
        if (listen(server_socket, 10) == -1) {
            perror("Listen failed"); // Print an error message if listen fails
            syslog(LOG_ERR, "Listen failed: %m"); // Log listen error
            close(server_socket); // Close the server socket
            // return -1; // Return an error status
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Accept incoming connection
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

            // char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
            // if (buffer == NULL) {
            //     syslog(LOG_ERR, "Memory allocation failed!"); // Log memory allocation error
            //     // return -1; // Return an error status
            //     exit(EXIT_FAILURE);
            // }

            // allocating new node for the data
		    datap = (slist_data_t *)malloc(sizeof(slist_data_t));

            SLIST_INSERT_HEAD(&head, datap, entries);

            // Inserting thread parameters now
            datap->thread_socket.client_fd = client_socket;
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

            syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
		    printf("Closed connection from %s\n", client_ip);

            // // Receive and append data
            // int data_file = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, FILE_PERMISSIONS);
            // if (data_file == -1) {
            //     perror("File open failed"); // Print an error message if file open fails
            //     syslog(LOG_ERR, "File open failed: %m"); // Log file open error
            //     free(buffer);
            //     close(client_socket);
            // }
            // else {

            //     ssize_t bytes_received;
            //     while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
            //         write(data_file, buffer, bytes_received);
            //         if (memchr(buffer, '\n', bytes_received) != NULL) {
            //             break;
            //         }
            //     }

            //     buffer[bytes_received+1] = '\0';

            //     syslog(LOG_INFO, "Received data from %s: %s, %d", client_ip, buffer, (int)bytes_received);

            //     close(data_file);

            //     lseek(data_file, 0, SEEK_SET);

            //     // Send data back to the client
            //     data_file = open(DATA_FILE, O_RDONLY);
            //     if (data_file == -1) {
            //         perror("file open failed"); // Print an error message if file open fails
            //         syslog(LOG_ERR, "File open failed: %m"); // Log file open error
            //         free(buffer);
            //         close(client_socket);
            //     }
            //     else {
            //         ssize_t bytes_read;
            //         memset(buffer, 0, sizeof(char) * BUFFER_SIZE);

            //         while ((bytes_read = read(data_file, buffer, sizeof(char) * BUFFER_SIZE)) > 0) {
            //             send(client_socket, buffer, bytes_read, 0);
            //         }

            //         free(buffer);
            //         close(data_file);
            //         close(client_socket);

            //         syslog(LOG_INFO, "Closed connection from %s", client_ip);
            //     }
            // }
        }
    }

    close(server_socket);
    if (unlink(DATA_FILE) == -1) {
        syslog(LOG_ERR, "Error removing data file: %m"); // Log error when removing data file
    }
    close(client_socket);
}

/*THREAD HANDLER*/
/*
 * @function	:  Thread handler function for receiving and sending data
 *
 * @param		:  void *thread_parameters: thread parameters
 * @return		:   void *thread_parameters
 *
 */
void *thread_handler(void *thread_parameter)
{

	// Package storage related variables
	bool packet_comp = false;
	int i, j = 0;
	int ret_recv = 0;
	int ret = 0;
	char buff[BUFFER_SIZE] = {0};
	char *output_buffer = NULL;
	char *send_buffer = NULL;

	// get the parameter of the thread
	thread_ipc *params = (thread_ipc *)thread_parameter;

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

		strncat(output_buffer, buff, i + 1);

		memset(buff, 0, BUFFER_SIZE);
	}

	// Step-6 Write the data received from client to the server

	int file_fd = open(DATA_FILE, O_APPEND | O_WRONLY);
	if (file_fd == -1)
	{
		printf("File open error for appending\n");
		exit(1);
	}
	ret = pthread_mutex_lock(&mutex_lock);
	if (ret)
	{
		printf("Mutex lock error before write\n");
		exit(1);
	}

	int writeret = write(file_fd, output_buffer, strlen(output_buffer));

	if (writeret == -1)
	{
		printf("Error write\n");
		exit(1);
	}
	close(file_fd);

	file_fd = open(DATA_FILE, O_RDONLY);
	if (file_fd == -1)
	{
		printf("File open error for reading\n");
		exit(1);
	}
	// Step-7 Reading from the file
	send_buffer = (char *)malloc(sizeof(char) * (data_count + 1));
	memset(send_buffer, 0, data_count + 1);

	int temp_read = read(file_fd, send_buffer, data_count + 1);

	if (temp_read == -1) // generating errors
	{
		printf("Error: reading failed\n");
		syslog(LOG_ERR, "Error:  read from file failed = %s. Exiting ", strerror(errno));
		exit(EXIT_FAILURE);
	}
	ret = pthread_mutex_unlock(&mutex_lock);
	if (ret)
	{
		printf("Mutex unlock error after read/send\n");
		exit(1);
	}

	// printf("%s\n",send_buffer);
	//  Step-7 Sending to the client with the accept fd
	
	printf("sending\n");
	int temp_send = send(params->client_fd, send_buffer, strlen(send_buffer), 0);
	
	if (temp_send == -1) // generating errors
	{
		printf("Error: sending failed\n");
		syslog(LOG_ERR, "Error:  sending to client failed= %s. Exiting ", strerror(errno));
		exit(EXIT_FAILURE);
	}
	close(file_fd);


	params->thread_complete = true;

	close(params->client_fd);

	// Free the allocated buffer
	free(output_buffer);
	free(send_buffer);

	return params;
}

/*Exit Fucntion*/
/*
 * @function	: Exit function for gracefule exit
 *
 * @param		:  NULL
 * @return		:  NULL
 *
 */
void exit_func(void)
{

	unlink(DATA_FILE);
	close(file_fd);
	close(server_socket);
	close(client_socket);
	while (SLIST_FIRST(&head) != NULL)
	{
		SLIST_FOREACH(datap, &head, entries)
		{
			close(datap->thread_socket.client_fd);
			pthread_join(datap->thread_socket.thread_id, NULL);
			SLIST_REMOVE(&head, datap, slist_data_s, entries);
			free(datap);
			break;
		}
	}
	if (timer_thread)
	{
		pthread_join(timer_thread, NULL);
	}


	exit(EXIT_SUCCESS);
}