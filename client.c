#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <math.h>
#include <fcntl.h>

/* simple client, takes two parameters, the server domain name,
   and the server port number */

int main(int argc, char** argv) {

  struct timeval current_time;
  struct timezone current_zone;
  /* our client socket */
  int sock;

  /* address structure for identifying the server */
  struct sockaddr_in sin;

  /* convert server domain name to IP address */
  struct hostent *host = gethostbyname(argv[1]);
  unsigned int server_addr = *(unsigned int *) host->h_addr_list[0];

  /* server port number */
  unsigned short server_port = atoi (argv[2]);

  char *buffer, *sendbuffer;
  int size = 65535;
  int count;
  int received_timestamp_sec, received_timestamp_usec, data_bytes;
  int ii,total_num_times = 0;
  
  float latency = 0;

  /* The size in bytes of each message to send (10 <= size <= 65,535)  */
  data_bytes = atoi(argv[3])-10;
  if(data_bytes > 65535){
	  printf("Message size beyond 65535 \n");
	  return 0;
  }
 
  /* The number of message exchanges to perform */
  int Count_for_iterations = atoi(argv[4]);
  
  if(Count_for_iterations > 10000){
	  printf("Count value should be between 1 and 10000 \n");
	  return 0;
  }

  /* allocate a memory buffer in the heap */
  /* putting a buffer on the stack like:

         char buffer[500];

     leaves the potential for
     buffer overflow vulnerability */
  buffer = (char *) malloc(size);
  if (!buffer)
    {
      perror("failed to allocated buffer");
      abort();
    }

  sendbuffer = (char *) malloc(size);
  if (!sendbuffer)
    {
      perror("failed to allocated sendbuffer");
      abort();
    }


  /* create a socket */
  if ((sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      perror ("opening TCP socket");
      abort ();
    }

  /* fill in the server's address */
  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = server_addr;
  sin.sin_port = htons(server_port);

  /* connect to the server */
  if (connect(sock, (struct sockaddr *) &sin, sizeof (sin)) < 0)
    {
      perror("connect to server failed");
      abort();
    }
	if (fcntl (sock, F_SETFL, O_NONBLOCK) < 0)
		{
		  perror ("making socket non-blocking");
		  abort ();
		}

  for(ii=0; ii< Count_for_iterations; ii=ii+1){      
     gettimeofday(&current_time, &current_zone);
     int timestamp_sec = current_time.tv_sec;
     int timestamp_usec = current_time.tv_usec;
    *(unsigned short *) (sendbuffer) = htons((unsigned short)(10+data_bytes));
    *(int *) (sendbuffer+2) = (int) htonl(timestamp_sec);
    *(int *) (sendbuffer+6) = (int) htonl(timestamp_usec);
    int message_size = data_bytes+10;
    int total_sent_bytes = 0;
	
	while(total_sent_bytes < (10+data_bytes)){
		int current_sent_bytes = send(sock, sendbuffer+total_sent_bytes, message_size-total_sent_bytes, 0);
		if(current_sent_bytes > 0){
			total_sent_bytes += current_sent_bytes;
		}
	}

    count =0;
	int total_received = 0;
    while(total_received < message_size){
		count = recv(sock, buffer+total_received, size-total_received, 0);
		if(count >0){
			total_received+=count;
		}
    }
	
		received_timestamp_sec = (int) ntohl(*(int *)(buffer+2));
        received_timestamp_usec = (int) ntohl(*(int *)(buffer+6));    
        gettimeofday(&current_time, &current_zone);
		/* Current time */
        int current_time_sec = current_time.tv_sec;
        int current_time_usec = current_time.tv_usec;
        float current_latency = (float)current_time_sec+ (float)current_time_usec/1000000.0 -((float)received_timestamp_sec + (float)received_timestamp_usec/1000000.0);
        if(current_latency>0){
    		total_num_times++;
            latency += current_latency;
        }
  }

  printf("Latency: %f millisec\n", latency*1000/(float)total_num_times);
  return 0;
}
