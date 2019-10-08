#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
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
#define TCP_PORT 51615
#define SecToMsec 1000000
#define SecToMisec 1000
#define DELAY 130

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

char filename[20],*data;
int fp,error,sockfd,total_packets,portno,off=0,seqNum=0;
struct stat statbuf;
size_t filesize;

//Socket variables
struct sockaddr_in serv_addr;
socklen_t fromlen;
struct hostent *host;

//Function declaration
void errorlog(const char *msg);
void tcp_send_data();
void mapfile_to_memory();
void udp_setup();
void* resend_pkt(void* a);

//threads
pthread_t th_resend;

typedef struct packet_t{
	int seq_num;
	char payload[PAYLOAD_SIZE+1];
}packet;

struct timeval Start_Time, End_Time;

double get_time(struct timeval End_Time)
{
    double timeu = SecToMisec*(End_Time.tv_sec - Start_Time.tv_sec) + 
    ((double)(End_Time.tv_usec - Start_Time.tv_usec))/SecToMisec;
    return timeu;
}

int main(int argc, char *argv[])
{
    if (argc<4) 
    {
       printf("Format: ./sender [hostname] [portno] [filename]\n");
       exit(0);
    }
    host = gethostbyname(argv[1]);
    portno = atoi(argv[2]);
    if (host == NULL) {
        printf("ERROR!! No such host Found!\n");
        exit(0);
    }
    strcpy(filename,argv[3]);
   
    udp_setup();
    mapfile_to_memory();
    tcp_send_data();
    packet packet1;
    memset(packet1.payload,'\0',PAYLOAD_SIZE+1);
    if((error=pthread_create(&th_resend,NULL,resend_pkt,NULL))){
        printf("Error while creating pthread!!\n");
        exit(1);
    }   
     
    if((filesize % PAYLOAD_SIZE)!=0)
        total_packets = (filesize/PAYLOAD_SIZE)+1;
    else
        total_packets = (filesize/PAYLOAD_SIZE);
    gettimeofday(&Start_Time, 0);
    fprintf(stdout, "%012.3fms: File sending Begins \n",get_time(Start_Time));
    while(seqNum < total_packets){
        packet1.seq_num = seqNum;
        if((seqNum == (total_packets-1)) && ((filesize % PAYLOAD_SIZE) != 0))
        	memcpy(packet1.payload,data+off,(filesize % PAYLOAD_SIZE));       	
        else
        	memcpy(packet1.payload,data+off,PAYLOAD_SIZE);
        seqNum++;
        off = off + PAYLOAD_SIZE;
        usleep(DELAY);
        if(sendto(sockfd,&packet1,PACKET_SIZE, 0,(struct sockaddr *) &serv_addr,sizeof(serv_addr))<0)
    	errorlog("sendto");
    }
    pthread_join(th_resend,NULL);
    munmap(data, filesize);
    close(fp);
    close(sockfd);
	return 1;
}
void* resend_pkt(void* a)
{
    double end_t=0,thr=0;
    long fz=(long)(filesize);
    while(1){
        int n,seq,size=PAYLOAD_SIZE;
        n = recvfrom(sockfd,&seq,sizeof(int),0,(struct sockaddr *)&serv_addr,&fromlen);
        if (n < 0) 
            printf("Error in recvfrom!!");          
        if(seq == -1){
            
            gettimeofday(&End_Time, 0);
            end_t=get_time(End_Time);
            fprintf(stdout, "%012.3fms: File Finished Sending \n",end_t);
            printf("\nEntire file transmitted\n");
            thr=(fz*8)/(end_t*1000);
            printf("\n---Results---\nDelay:%.3fs\nThroughput: %.3fMbps\n",end_t/1000,thr);
            exit(0);
        }
        if((seq == (total_packets-1)) && (0 != filesize % PAYLOAD_SIZE))
            size = filesize % PAYLOAD_SIZE;
        packet packet2;
        memset(packet2.payload,'\0',PAYLOAD_SIZE+1);
        packet2.seq_num = seq;
        memcpy(packet2.payload,data+(seq*PAYLOAD_SIZE),size);
        if(sendto(sockfd,&packet2,PACKET_SIZE, 0,(struct sockaddr *) &serv_addr,sizeof(serv_addr))<0)
    	errorlog("sendto");
    }
}

void udp_setup()
{
    //create a udp socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    int sock_options;
    if (sockfd < 0)
        errorlog("ERROR opening socket");
    uint64_t sock_buffer_size = 1000000000;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)host->h_addr, (char *)&serv_addr.sin_addr.s_addr, host->h_length);
    serv_addr.sin_port = htons(portno);
    fromlen=(sizeof(serv_addr));
    if((setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&sock_options,sizeof (int))) == -1)
        errorlog("ERROR setting socket opt");
    if((setsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,&sock_buffer_size, sizeof(uint64_t))) == -1)
        errorlog("ERROR setting socket opt");
    if((setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,&sock_buffer_size, sizeof(uint64_t))) == -1)
        errorlog("ERROR setting socket opt"); 
}

void tcp_send_data(){
    int sockfd_t, n_t;
    int tcp_port = TCP_PORT;
    struct sockaddr_in serv_addr_t;
    sockfd_t = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_t < 0)
        errorlog("ERROR opening socket");
    
    bzero((char *) &serv_addr_t, sizeof(serv_addr_t));
    serv_addr_t.sin_family = AF_INET;
    bcopy((char *)host->h_addr,(char *)&serv_addr_t.sin_addr.s_addr,host->h_length);
    serv_addr_t.sin_port = htons(tcp_port);
    if (connect(sockfd_t,(struct sockaddr *) &serv_addr_t,sizeof(serv_addr_t)) < 0)
        errorlog("ERROR connecting");
    n_t = write(sockfd_t,(void *)&filesize,sizeof(filesize));
    if (n_t < 0)
         errorlog("ERROR writing to TCP socket");

    if (write(sockfd_t,(char *)filename,sizeof(filename)) < 0)
         errorlog("ERROR writing to TCP socket");
     //printf("fn: %s fz: %zu\n",filename,filesize);
    close(sockfd_t);
}

void mapfile_to_memory(){
    pthread_mutex_lock(&lock);
    if ((fp = open (filename, O_RDONLY)) < 0){
        fprintf(stderr,"can't open %s for reading", filename);
        pthread_mutex_unlock(&lock);
        exit(0);
    }
    filesize = lseek(fp, 0, SEEK_END);
    printf("Filesize is %zu Byte\n\n",filesize);
    data = mmap((caddr_t)0, filesize, PROT_READ, MAP_SHARED, fp, 0);
    if (data == (caddr_t)(-1)) {
        perror("mmap");
        pthread_mutex_unlock(&lock);
        exit(0);
    }
    pthread_mutex_unlock(&lock);
}

void errorlog(const char *msg)
{
    perror(msg);
    exit(0);
}

