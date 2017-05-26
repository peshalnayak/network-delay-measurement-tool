#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>


/**************************************************/
/* a few simple linked list functions             */
/**************************************************/


/* A linked list node data structure to maintain application
   information related to a connected socket */
struct node {
	int socket;
	struct sockaddr_in client_addr;

	/* flag to indicate whether there is more data to send */
	int pending_data;

	/* current length of the message*/
	int curLen;

	/* size of the complete message */
	int size;

    /* http response code*/
    int http_code;

	/* pointer to the file path of the HTTP request */
	char *filePath;

	/* a holder for the data that needs to be sent back to the client*/
	char *data_to_send;

    /* pointer to the next node */
	struct node *next;
};

/* remove the data structure associated with a connected socket
   used when tearing down the connection */
void dump(struct node *head, int socket) {
	struct node *current, *temp;

	current = head;

	while (current->next) {
		if (current->next->socket == socket) {
			/* remove */
			temp = current->next;
			current->next = temp->next;

			memset(temp->filePath, '\0', 65535);
			memset(temp->data_to_send, '\0', 65535);

			/* Free memory */
			free(temp->filePath);
			free(temp->data_to_send);
			free(temp);

			return;
		}
		else {
			current = current->next;
		}
	}
}

/* create the data structure associated with a connected socket */
void add(struct node *head, int socket, struct sockaddr_in addr) {
	struct node *new_node;

	new_node = (struct node *)malloc(sizeof(struct node));
	new_node->socket = socket;
	new_node->client_addr = addr;

	/* initialize the value*/
	new_node->pending_data = 0;
	new_node->curLen = 0;
	new_node->size = 0;
	new_node->http_code = 0;
	new_node->filePath = malloc(65535);
	new_node->data_to_send = malloc(65535);

	new_node->next = head->next;
	head->next = new_node;
}

/* parse the http request to get the response code */
void getHTTPResponseCode(struct node *httpNode, char *buffer, char *root) {
      
  	  // char *filePath = malloc(65535);
	  char *relativePath = malloc(2048);

	  // we only support text for this simple app
	  char *content_type = "Content-Type: text/html \r\n\r\n";

	  int start = 0;
	  int end = 0;

	  while(buffer[start] != ' ') start++;
	  end = ++start;

	  while(buffer[end] != ' ') end++;

	  // get the path of HTTP GET request
	  strncpy(relativePath, buffer + start, end-start);

	  // get the absolute file path
	  strcpy(httpNode->filePath, root);
	  strcat(httpNode->filePath, relativePath);

	  free(relativePath);

      // copy the HTTP version to the head of the response buffer
	  start = ++end;
	  while(buffer[end] != ' ' && buffer[end] != '\r') end++;
	  strncpy(httpNode->data_to_send, buffer + start, end-start);

	  
	  // check if the request is supported; we only support GET requests with this server
	  if(strncmp(buffer, "GET", 3) != 0){
	  	httpNode->http_code = 501;
	  	strcat(httpNode->data_to_send, " 501 Not Implemented \r\n");
	  	strcat(httpNode->data_to_send, content_type);
	  	return;
	  }
      
      // deny invalid string
	  char *invalid_str = "../";
	  if( strstr( relativePath, invalid_str ) ){
	  	httpNode->http_code = 400;
	  	strcat(httpNode->data_to_send, " 400 Bad Request \r\n");
	  	strcat(httpNode->data_to_send, content_type);
	  	return;
	  }

      // handling other 400 errors
      struct stat s;
	  if( stat( httpNode->filePath,&s ) == 0 ) {	  			   
		 if( s.st_mode & S_IFDIR ) {//if it's a directory
		 	httpNode->http_code = 400;
			strcat(httpNode->data_to_send, " 400 Bad Request \r\n");
		  	strcat(httpNode->data_to_send, content_type);
		 } else if( s.st_mode & S_IFREG ) {//if it's a file
		    httpNode->http_code = 200;
	  	    strcat(httpNode->data_to_send, " 200 OK \r\n");
	  	    strcat(httpNode->data_to_send, content_type);
		 } else { // if can not open
		 	httpNode->http_code = 400;
		    strcat(httpNode->data_to_send, " 400 Bad Request \r\n");
		  	strcat(httpNode->data_to_send, content_type);   
		 }
	  } else { // not found
		  	httpNode->http_code = 404;
		  	strcat(httpNode->data_to_send, " 404 Not Found \r\n");
		  	strcat(httpNode->data_to_send, content_type);
	  }
}

/* receive the message from HTTP request */
void handleHTTPRequest(struct node *httpNode, char *buffer, char *root) {
 
  if(httpNode->http_code == 0) getHTTPResponseCode(httpNode, buffer, root);

  switch(httpNode->http_code){

  	case 200:
  	     if(httpNode->size == 0) {
  	     	struct stat st;
	        stat(httpNode->filePath, &st);
	        httpNode->size = st.st_size;
		 }

		 if(httpNode->curLen < httpNode->size){

	   	   FILE *file = fopen(httpNode->filePath, "r");

	   	   fseek(file, httpNode->curLen * sizeof(char), SEEK_SET);

           // for the first message, we should avoid overwriting the header information
	   	   if(httpNode->curLen == 0) fread(httpNode->data_to_send + strlen(httpNode->data_to_send), sizeof(char), 65535, file);
	   	   else fread(httpNode->data_to_send, sizeof(char), 65535, file);

	   	   httpNode->curLen += 65535;
	   	   fclose(file);
		 }

		 break;

  	case 400: 
	  	if(access("400 Bad Request.html", F_OK) != -1) { // file exists

	    	FILE *file = fopen("400 Bad Request.html", "r");
            fread(httpNode->data_to_send + strlen(httpNode->data_to_send), sizeof(char), 65535, file);
            fclose(file);
	    } else {
	    	strcat(httpNode->data_to_send, "The request contains illegal strings or characters.\n");
	    }

  	    break;

  	case 404:
  		if(access("404 Not Found.html", F_OK) != -1) { // file exists

	    	FILE *file = fopen("404 Not Found.html", "r");
            fread(httpNode->data_to_send + strlen(httpNode->data_to_send), sizeof(char), 65535, file);
            fclose(file);
	    } else {
	    	strcat(httpNode->data_to_send, "The requested document was not found on this server.\n");
	    }

  	    break;

  	case 501:
  		if(access("501 Not Implemented.html", F_OK) != -1) { // file exists

	    	FILE *file = fopen("501 Not Implemented.html", "r");
            fread(httpNode->data_to_send + strlen(httpNode->data_to_send), sizeof(char), 65535, file);
            fclose(file);
	    } else {
	    	strcat(httpNode->data_to_send, "This server only supports GET requests.\n");
	    }

  	    break;

  	default: 
  	    httpNode->http_code = 500;
	  	strcat(httpNode->data_to_send, " 500 Internal Server Error \r\n");
	  	strcat(httpNode->data_to_send, "Content-Type: text/html \r\n\r\n");

  		if(access("500 Internal Server Error.html", F_OK) != -1) { // file exists

	    	FILE *file = fopen("500 Internal Server Error.html", "r");
            fread(httpNode->data_to_send + strlen(httpNode->data_to_send), sizeof(char), 65535, file);
            fclose(file);
	    } else {
	    	strcat(httpNode->data_to_send, "Error occurs while hanlding the request :(\n");
	    }

  	    break;
  }
  
  httpNode->pending_data = 1;

}

/*check the running mode
  1: default mode; 2: HTTP mode */
int verify_mode(int argc, char **argv)
{
    if(argc<2)
        return -1;
    else if( argc < 3 || strcmp(argv[2],"www") != 0 ){
        printf("\nStart server with Ping Pong mode...\r\n");
        return 1;
    }
    else{
    	printf("\nStart server with HTTP mode...\r\n");
    	return 2;
    }
        
}

/* validate the root directory */
void validate_root_directory(char *root, int argc, char **argv)
{
    if(argc < 4)
    {
        printf("Error: missng the root_directory for 'www' mode.\n");
        free(root);
        abort();
    }
    else if(access(argv[3], F_OK) == -1) { // file does not exist

    	printf("The input root directory is neither not found or non accessible.\n");
    	free(root);
		abort();
    }
    else
    {
    	char *rt = argv[3];

    	// erasing extra '/'
    	while( rt[strlen(rt)-1] == '/') rt[strlen(rt)-1] = '\0';
        strcpy(root, rt);
    }
    
}

/*****************************************/
/* main program                          */
/*****************************************/

/* simple server, takes one parameter, the server port number */
int main(int argc, char **argv) {
    
    if(argc == 1) {
        printf("Error: no input found.\n");
        printf("Valid input should be: \"%s port\" OR \"%s port www root_directory\"\n", argv[0], argv[0]);
        abort();
    }

    unsigned short server_port = atoi(argv[1]);
    
    /* Validate the port number */
	if (server_port < 18000 || server_port > 18200)
	{
		printf("Error: The input server port is not available. The usable range is: 18000 <= port <= 18200\n");
		abort();
	}

	/* 1: default (ping-pong) mode; 2: HTTP mode */
    int server_mode = 0;

    /* the root directory for HTTP request */
    char *root = NULL;
    
    /* verify the mode */
    server_mode = verify_mode(argc, argv);
    if(server_mode <0)
    {
        printf("Error: Invalid parameters!\n");
        printf("Valid input should he: \"%s port\" OR \"%s port www root_directory\"\n", argv[0], argv[0]);
        printf("Note: for 'www' mode, 'root_directory' is required.\n");
        abort();
    }

    /* validate the root directory for HTTP mode*/
    if(server_mode == 2){
    	root = malloc(2048);
    	validate_root_directory(root, argc, argv);
    }


	/* socket and option variables */
	int sock, new_sock, max;
	int optval = 1;

	/* server socket address variables */
	struct sockaddr_in sin, addr;

	/* socket address variables for a connected client */
	socklen_t addr_len = sizeof(struct sockaddr_in);

	/* maximum number of pending connection requests */
	int BACKLOG = 5;

	/* variables for select */
	fd_set read_set, write_set;
	struct timeval time_out;
	int select_retval;

	/* a welocme message for test */
///	char *message = "Welcome to the PingPong Server!\n";

	/* number of bytes sent/received */
	int count;

	/* linked list for keeping track of connected sockets */
	struct node head;
	struct node *current, *next;

	/* initialize dummy head node of linked list */
	head.socket = -1;
	head.next = 0;

	/* initialize dummy head node of linked list */
	head.socket = -1;
	head.next = 0;
       
	/* create a server socket to listen for TCP connection requests */
	if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		perror("opening TCP socket");
		abort();
	}

	/* set option so we can reuse the port number quickly after a restart */
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof (optval)) < 0)
	{
		perror("setting TCP socket option");
		abort();
	}

	/* fill in the address of the server socket */
	memset(&sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(server_port);

	/* bind server socket to the address */
	if (bind(sock, (struct sockaddr *) &sin, sizeof (sin)) < 0)
	{
		perror("binding socket to address");
		abort();
	}

	/* put the server socket in listen mode */
	if (listen(sock, BACKLOG) < 0)
	{
		perror("listen on socket failed");
		abort();
	}

	/* now we keep waiting for incoming connections,
	   check for incoming data to receive,
	   check for ready socket to send more data */
	   int total_received = 0;
	 int number_of_packets = 0;
	while (1)
	{

		/* set up the file descriptor bit map that select should be watching */
		FD_ZERO(&read_set); /* clear everything */
		FD_ZERO(&write_set); /* clear everything */

		FD_SET(sock, &read_set); /* put the listening socket in */
		max = sock; /* initialize max */

		/* put connected sockets into the read and write sets to monitor them */
		for (current = head.next; current; current = current->next) {
			FD_SET(current->socket, &read_set);

			if (current->pending_data) {
				/* there is data pending to be sent, monitor the socket
					   in the write set so we know when it is ready to take more
					   data */
				FD_SET(current->socket, &write_set);
			}

			if (current->socket > max) {
				/* update max if necessary */
				max = current->socket;
			}
		}

		time_out.tv_usec = 100000; /* 1-tenth of a second timeout */
		time_out.tv_sec = 0;

		/* invoke select, make sure to pass max+1 !!! */
		select_retval = select(max + 1, &read_set, &write_set, NULL, &time_out);
		if (select_retval < 0)
		{
			perror("select failed");
			abort();
		}

		if (select_retval == 0)
		{
			/* no descriptor ready, timeout happened */
			continue;
		}

		if (select_retval > 0) /* at least one file descriptor is ready */
		{
			if (FD_ISSET(sock, &read_set)) /* check the server socket */
			{
				/* there is an incoming connection, try to accept it */
				new_sock = accept(sock, (struct sockaddr *) &addr, &addr_len);

				if (new_sock < 0)
				{
					perror("error accepting connection");
					abort();
				}

				/* make the socket non-blocking so send and recv will
					   return immediately if the socket is not ready.
					   this is important to ensure the server does not get
					   stuck when trying to send data to a socket that
					   has too much data to send already.
					   */
				if (fcntl(new_sock, F_SETFL, O_NONBLOCK) < 0)
				{
					perror("making socket non-blocking");
					abort();
				}

				/* the connection is made, everything is ready */
				/* let's see who's connecting to us */
				printf("Accepted connection. Client IP address is: %s\n",
					inet_ntoa(addr.sin_addr));

				/* remember this client connection in our linked list */
				add(&head, new_sock, addr);

				/* send a welcome message to the client for pingpong mode */
				/*
				if(server_mode == 1) {
					count = send(new_sock, message, strlen(message) + 1, 0);
					if (count < 0)
					{
						perror("error sending welcome message to client");
						abort();
					}
				}
				*/

			}

			/* check other connected sockets, see if there is
				   anything to read or some socket is ready to send
				   more pending data */
			for (current = head.next; current; current = next) {
				next = current->next;
                
                int PING_HEADER = 10;

				/* a buffer to read data */
				char *buf;

				/* This is the limit of the packet size */
				int BUF_LEN = 65535;

				buf = (char *)malloc(BUF_LEN);

				/* see if we can now do some previously unsuccessful writes */
				if (FD_ISSET(current->socket, &write_set)) {
					/* the socket is now ready to take more data */
	
					if(server_mode == 2) {// for http request, the message length depends
				        
				        // printf("Response to the HTTP Request:\n%s\n", current->data_to_send);

						if(current->http_code == 200) {
							if(current->curLen < current->size) 
								count = send(current->socket, current->data_to_send, BUF_LEN, MSG_DONTWAIT);
                            else 
                            	count = send(current->socket, current->data_to_send, BUF_LEN + current->size - current->curLen, MSG_DONTWAIT);
						} else {
							count = send(current->socket, current->data_to_send, strlen(current->data_to_send), MSG_DONTWAIT);
						}


					} else { // for ping-pong mode, we only send back message once when complete

						count = send(current->socket, current->data_to_send, current->size, MSG_DONTWAIT);
					}						
					
					if (count < 0) {
						if (errno == EAGAIN) {
							/* we are trying to dump too much data down the socket,
							   it cannot take more for the time being
							   will have to go back to select and wait til select
							   tells us the socket is ready for writing
							   */
							continue;
						}
						else {
							printf("Error: failed to send the data to client.\n");
						}
					}
					else {

						if(server_mode == 2) {
							/* if the requested file is too large to send within one package;
							we go through the loop again */
							if( current->http_code == 200 && current->curLen < current->size) {
							    handleHTTPRequest(current, buf, root);
							} else {
								printf("\nRequest answered. Closing the connection...\n");
					    	    close(current->socket);
						        dump(&head, current->socket);
							}
							
					    } else {

					    	printf("Clearing node for next ping message...\n");

						    current->curLen = 0;
						    current->size = 0;
						    current->pending_data = 0;
					    }

					}
					/* note that it is important to check count for exactly
						   how many bytes were actually sent even when there are
						   no error. send() may send only a portion of the buffer
						   to be sent.
						   */
				}

				/* we have data from a client */
				if (FD_ISSET(current->socket, &read_set)) {
					if(server_mode == 2){
						count = recv(current->socket, buf, BUF_LEN, 0);

						///if(server_mode == 2) printf("\nIncoming HTTP request: %s\n", buf);
						printf("\nIncoming HTTP request: %s\n", buf);

						if (count <= 0) {
							/* something is wrong */
							if (count == 0) {
								printf("\nClient closed connection. Client IP address is: %s\n", inet_ntoa(current->client_addr.sin_addr));
							}
							else {
								perror("error receiving from a client");
							}

							/* connection is closed, clean up */
							close(current->socket);
							dump(&head, current->socket);
						}
						else {
							
							///if(server_mode == 2) handleHTTPRequest(current, buf, root);
							handleHTTPRequest(current, buf, root);

///							else handlePingPongRequest(current, buf, count, PING_HEADER);

						}
					}
					else{
						count = 0;
						  count = recv(current->socket, buf+total_received, BUF_LEN-total_received, 0);
						  if(count>0){
							total_received += count;  
						  }
						  /* note that it is important to check total_sent_bytes for exactly
								 how many bytes were actually sent even when there are
								 no error. send() may send only a portion of the buffer
								 to be sent.
						  */
						  if((unsigned short)total_received == ntohs(*(unsigned short *)buf)){
							 int total_sent_bytes = 0;
							  while(total_sent_bytes < total_received){
								int current_sent_bytes = send(current->socket, buf+total_sent_bytes, total_received-total_sent_bytes, 0);
								if(current_sent_bytes > 0){
									total_sent_bytes += current_sent_bytes;
								}
							  }
							  number_of_packets++;
							  total_received = 0;
						  }
						  
						  if (count <= 0) {
							/* something is wrong */
							if (count == 0) {
							  printf("Client closed connection. Client IP address is: %s\n", inet_ntoa(current->client_addr.sin_addr));
							} else {
							  perror("error receiving from a client");
							}

							/* connection is closed, clean up */
							close(current->socket);
							dump(&head, current->socket);
							}
					}
					
				}

				free(buf);
			}
		}
	}
}
