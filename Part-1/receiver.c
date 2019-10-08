#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sched.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>

#define PAYLOAD_SIZE 1450
#define HEADER_SIZE 4
#define PACKET_SIZE 1454
#define MAX_BUFFER_SIZE 500000000
#define TCPPORT 51615
#define SecToMsec 1000000
#define SecToMisec 1000
#define DELAY 130

typedef struct packet_t{
    int seq_num;
    char payload[PAYLOAD_SIZE+1];
}packet;


char filename[20];
size_t filesize;
int start_pointer=0, end_pointer=0;
int *ACK_Track;
int total_packets=0;
unsigned int last_packet_size;
int startNACK=0;
int track_pointer=0;
int pkt_count=0;
int error;
FILE* fp_s;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// UDP socket variables
socklen_t fromlen;
struct sockaddr_in serv_addr, from;
int sockfd_s, portno;
int sock_options_s, sock_options_s_t;
pthread_t nack_thread_s[10];
//methods
void errorlog(const char *msg);
void tcp_file_info();
void receive_packets();
void* handleFailures(void *);
void udp_setup(int UDPPORT);
int getNACKNum();
int pkt_count;
struct timeval Start_Time, End_time;

void errorlog(const char *msg)
{
    perror(msg);
    exit(1);
}
double get_time(struct timeval End_Time)
{
    double timeu = SecToMisec*(End_Time.tv_sec - Start_Time.tv_sec) + 
    ((double)(End_Time.tv_usec - Start_Time.tv_usec))/SecToMisec;
    return timeu;
}

int main(int argc, char *argv[])
{

    if (argc < 3) {
         printf("Format: ./receiver [portno] [filename]\n");
         exit(1);
    }
    strcpy(filename,argv[2]);
    int i;
    udp_setup(atoi(argv[1]));
    tcp_file_info();
    fp_s = fopen(filename, "w+");
    
    if(filesize%PAYLOAD_SIZE!=0){
        total_packets = filesize/PAYLOAD_SIZE + 1;
        last_packet_size = filesize%PAYLOAD_SIZE;
    }
    else{
        total_packets = filesize/PAYLOAD_SIZE;
        last_packet_size = PAYLOAD_SIZE;
    }
     
    for(i=0;i<10;i++){
    if((errno = pthread_create(&nack_thread_s[i], NULL, handleFailures, NULL ))){
        fprintf(stderr, "pthread_create[0] %s\n",strerror(errno));
        pthread_exit(0);
    }
    ACK_Track = (int *)calloc(total_packets, sizeof (int));
    receive_packets();

    for(i=0;i<10;i++)
        pthread_join(nack_thread_s[i], NULL);
    
    fclose(fp_s);
    close(sockfd_s);
    return 0;
}
}

void tcp_file_info()
{
    int sockfd_t, newsockfd_t, portno_t;
     socklen_t clilen_t;
     struct sockaddr_in serv_addr_t, cli_addr_t;

     sockfd_t = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd_t < 0)
        errorlog("ERROR opening sTCP ocket");
    if((setsockopt(sockfd_t,SOL_SOCKET,SO_REUSEADDR,&sock_options_s_t,sizeof (int))) == -1)
        errorlog("ERROR setting TCP socket option");
    
     bzero((char *) &serv_addr_t, sizeof(serv_addr_t));
     portno_t = TCPPORT;
     serv_addr_t.sin_family = AF_INET;
     serv_addr_t.sin_addr.s_addr = INADDR_ANY;
     serv_addr_t.sin_port = htons(portno_t);
     if (bind(sockfd_t, (struct sockaddr *) &serv_addr_t,
              sizeof(serv_addr_t)) < 0)
              errorlog("ERROR on TCP binding");
     listen(sockfd_t,5);
     clilen_t = sizeof(cli_addr_t);
     printf("Waiting for Client to send file....\n");
     newsockfd_t = accept(sockfd_t,
                 (struct sockaddr *) &cli_addr_t,
                 &clilen_t);
    if (newsockfd_t < 0)
          errorlog("ERROR on accept");
    
    if (read(newsockfd_t,&filesize,sizeof(filesize))< 0) errorlog("ERROR reading from TCP socket");
    //if (read(newsockfd_t,(char *)filename,sizeof(filename))< 0) errorlog("ERROR reading from TCP socket");

    close(newsockfd_t);
    close(sockfd_t);
}

void udp_setup(int UDPPORT){
    uint64_t sock_buffer_size_s = MAX_BUFFER_SIZE;
    sockfd_s = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_s < 0)
        errorlog("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = UDPPORT;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if((setsockopt(sockfd_s,SOL_SOCKET,SO_REUSEADDR,&sock_options_s,sizeof (int))) == -1){
        errorlog("ERROR setting socket opt");
    }
    if((setsockopt(sockfd_s,SOL_SOCKET,SO_SNDBUF,&sock_buffer_size_s, sizeof(uint64_t))) == -1){
        errorlog("ERROR setting socket opt");
    }
    if((setsockopt(sockfd_s,SOL_SOCKET,SO_RCVBUF,&sock_buffer_size_s, sizeof(uint64_t))) == -1){
        errorlog("ERROR setting socket opt");
    }
    if (bind(sockfd_s, (struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0)
        errorlog("ERROR on binding");
     fromlen = sizeof(from);
}

void receive_packets()
{
    int n = 0;
    packet rcvPacket;
    long write_pos;
    while (1)
    {
        n = recvfrom(sockfd_s,&rcvPacket,1500,0,(struct sockaddr *) &from,&fromlen);
        if (n < 0) errorlog("ERROR in recv ");

    if(rcvPacket.seq_num>= 0 && rcvPacket.seq_num < total_packets)
    {
        if(ACK_Track[rcvPacket.seq_num] == 0)
        {
            ACK_Track[rcvPacket.seq_num] = 1;
                if(rcvPacket.seq_num > end_pointer)
                    end_pointer = rcvPacket.seq_num;
                write_pos = rcvPacket.seq_num * PAYLOAD_SIZE;
                fseek( fp_s , write_pos , SEEK_SET  );
                if(rcvPacket.seq_num == (total_packets - 1) ){
                    fwrite(&rcvPacket.payload , last_packet_size , 1 , fp_s);
                    fflush(fp_s);
                 }
                else{
                    fwrite(&rcvPacket.payload , PAYLOAD_SIZE , 1 , fp_s);
                    fflush(fp_s);
                 }
                 pkt_count ++;
        }}
        if(pkt_count == total_packets){
            printf("Entire file received!\n");
            int end= -1, n,i;
            for (i = 0; i < 10;i++){
            n = sendto(sockfd_s,&end,sizeof(int), 0,(struct sockaddr *) &from,fromlen);
            if (n < 0) errorlog("sendto");
            }
            close(sockfd_s);
            break;
        }
       if(end_pointer >= 0.8 * total_packets)
            startNACK = 1;
       }
}


void* handleFailures(void *a)
{
    int reqSeqNum;
    while(1)
    {
        if(startNACK){
            usleep(DELAY);
            int actual_end_pointer = 0;
            if(end_pointer > 0.7 * total_packets)
                actual_end_pointer = end_pointer + 0.3 * total_packets;
            else
                actual_end_pointer = end_pointer;
            
            if(pkt_count == total_packets){
                pthread_exit(0); 
            }
            int i=0;
            for(i =  start_pointer; i<= actual_end_pointer && i < total_packets ; i++)
                {
                if(ACK_Track[i] == 1){
                start_pointer ++;
                }
                else 
                    break;
            }

            reqSeqNum = getNACKNum();

            if(reqSeqNum >= 0 && reqSeqNum < total_packets){
            if (sendto(sockfd_s, &reqSeqNum, sizeof(int), 0,(struct sockaddr *) &from,fromlen) < 0) 
            errorlog("Error in Sending Request Sequence Number\n");
            }
        }
    }
}

int getNACKNum(){
    if (ACK_Track == NULL) return -1;
    int i;
    for (i = track_pointer; i < total_packets ; i++)
    {
        if(ACK_Track[i] == 0){
            if( i == total_packets - 1) 
                track_pointer = start_pointer;
            else 
                track_pointer = i+1;   
            return i;
        }
    }
    track_pointer = start_pointer;
    return -1;
}

