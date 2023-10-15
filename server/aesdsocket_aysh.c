/**********************************************************************************************************************************
 * @File name (aesdsocket.c)
 * @File Description: (implementation of IPC communication using sockets for Assignment 6 P1)
 * @Author Name (AYSWARIYA KANNAN)
 * @Date (02/24/2023)
 * @Attributions :https://www.binarytides.com/socket-programming-c-linux-tutorial/
 * 				  https://www.tutorialspoint.com/c_standard_library/c_function_strerror.htm
 *                https://stackoverflow.com/questions/1276294/getting-ipv4-address-from-a-sockaddr-structure
 * 				  https://www.geeksforgeeks.org/socket-programming-cc/
 **************************************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include "queue.h"

#define MAX_BACKLOG (10)
#define BUFFER_SIZE (100)

/*** GLOBALS *********************************************/
char *server_port = "9000";					 // given port for communication
char *file_data = "/var/tmp/aesdsocketdata"; // file to save input string
int socket_fd = 0;							 // socket file descriptor
int accept_fd = 0;							 // client accept file descriptor
int data_count = 0;							 // for counting the data packet bytes
int file_fd = 0;							 // file as defined in path to be created
bool process_flag = false;
int deamon_flag = 0;
//  Function prototypes
void socket_connect(void);
void *thread_handler(void *thread_parameter);
pthread_t timer_thread = (pthread_t)NULL;
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

/*SIGNAL HANDLER*/
/*
 * @function	:  Signal handler function for handling SIGINT,SIGTERM and SIGKILL
 *
 * @param		:  int signal_no : signal number
 * @return		:  NULL
 *
 */
void signal_handler(int signal_no)
{

	if (signal_no == SIGINT || signal_no == SIGTERM || signal_no == SIGKILL)
	{
		printf("signal detected to exit\n");
		syslog(LOG_DEBUG, "Caught the signal, exiting...");
		shutdown(socket_fd, SHUT_RDWR);
		process_flag = true;
		unlink(file_data);
		close(accept_fd);
		close(socket_fd);
	}
	_exit(0);
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

		file_fd = open(file_data, O_APPEND | O_WRONLY);
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
/*
 * @function	: main fucntion for Socket based communication
 *
 * @param		: argc -refers to the number of arguments passed, and argv[] is a pointer array which points to each argument passed
 * @return		: return 0 on success
 *
 */
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
	file_fd = creat(file_data, 0644);
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
	bool timer_thread_flag = false;
	while (process_flag==false)
	{
		
		if (!timer_thread_flag)
		{
			pthread_create(&timer_thread, NULL, timer_handler, NULL);
			timer_thread_flag = true;
		}
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

	int file_fd = open(file_data, O_APPEND | O_WRONLY);
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

	file_fd = open(file_data, O_RDONLY);
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

	unlink(file_data);
	close(file_fd);
	close(accept_fd);
	close(socket_fd);
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
