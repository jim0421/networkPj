/*
 * peer.c
 * 
 * Author: Tianhao Wang <thwang11@fudan.edu.cn>,
 *
 * Modified from CMU 15-441,
 * Original Authors: Ed Bardsley <ebardsle+441@andrew.cmu.edu>,
 *                   Dave Andersen
 * 
 * Class: Networks (Spring 2015)
 *
 */

#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "debug.h"
#include "spiffy.h"
#include "bt_parse.h"
#include "input_buffer.h"

#define HASH_SIZE 20
#define BUFLEN 1500
#define MAGICNUM 15441
#define VERSION 1
#define HEADER_LEN 16
#define WHOHAS 0
#define IHAVE 1
#define GET 2
#define DATA 3
#define ACK 4
#define DENIED 5
#define HASHNUM_MAX 4 //for hash num in *.chunk
#define PACKETNUM 375
#define ORIGINAL_SSTHRESH 64
#define MAXLEN 4096
#define MAX(x,y) (x>y)?x:y
#define MIN(x,y) (x<y)?x:y
/*copy from server.c*/
typedef struct header_s {
    short magicnum;
    char version;
    char packet_type;
    short header_len;
    short packet_len;
    u_int seq_num;
    u_int ack_num;
} header_t;

typedef struct data_packet {
    header_t header;
    char data[BUFLEN-16];
} data_packet_t;


void peer_run(bt_config_t *config);
char hash2Format(char a,char b);
int hash_include(char son[], char father[][HASH_SIZE], int my_hash_num);
int hash_match(char hash1[], char hash2[]);	
bt_config_t *my_config;

char my_outputfile[128];
u_int cur_seq;
int hash_id;  
char filename[128];
u_int my_seq_pro;
u_int my_ack;
struct sockaddr_in my_from; 	
int peer_status;
void send_getPacket(struct sockaddr_in from);
int my_sock,my_index,my_number,my_count;
char my_chunk_list[HASHNUM_MAX][HASH_SIZE];
void resend();
void time_log(int win_size);
/*void sigalrm_handler(){
	printf("hello1\n");
    resend();
}
*/
//slow start and congestion avoidance status
int window_size, ssthresh, lastPacketAcked, lastPacketSent, congestion_mode;
int changeTime;
int dataTime_log[PACKETNUM];
int ackNum_log[PACKETNUM];
clock_t start_time;

int main(int argc, char **argv) {
	bt_config_t config;
	
	bt_init(&config, argc, argv);
	//my configure
	my_count=0;		
	my_config = &config;
	peer_status = 0;
	start_time = clock();

    //signal(SIGALRM,sigalrm_handler);    
    
	DPRINTF(DEBUG_INIT, "peer.c main beginning\n");

#ifdef TESTING
	config.identity = 1; // your group number here
	strcpy(config.chunk_file, "chunkfile");
	strcpy(config.has_chunk_file, "haschunks");
#endif

	bt_parse_command_line(&config);

#ifdef DEBUG
	if (debug & DEBUG_INIT) {
		bt_dump_config(&config);
	}
#endif
  
	peer_run(&config);
	return 0;
}


void process_inbound_udp(int sock) {
  	struct sockaddr_in from;
  	socklen_t fromlen;
  	char buf[BUFLEN];
	my_from = from;	
  	fromlen = sizeof(from);

  	//Get the received packet
  	spiffy_recvfrom(sock, buf, BUFLEN, 0, (struct sockaddr *) &from, &fromlen);
  	data_packet_t recv_packet; 
  	recv_packet = *(data_packet_t *)buf;
  	char new_chunks_hash[HASHNUM_MAX][HASH_SIZE];
  	int id[HASHNUM_MAX];
  	int chunk_num = 0;
  	/* Deal with different kinds of received packet */
  
  	//1. Handle the WHOHAS packet
	if (recv_packet.header.packet_type==WHOHAS){
		peer_status = 0;
		int num = (int)(recv_packet.data[0]);
		char chunk_list[num][HASH_SIZE];
		int i,j;
	 	
	  	for (i=0;i<num;i++){
	  		for (j=0;j<HASH_SIZE;j++){
	  			chunk_list[i][j] = recv_packet.data[4+i*HASH_SIZE+j];
	  		}
	 	}
		printf("has_chunks:%s\n",my_config->has_chunk_file);
		printf("num is %d\n",num);	
		
		//Read hasChunks file	
		FILE *fp = fopen(my_config->has_chunk_file,"r");
		int n = 0;
		char tmp[64];
		while(!feof(fp)){
		    fgets(tmp,64,fp);
		    n++;
		}
		fclose(fp);
		fp = fopen(my_config->has_chunk_file,"r");
		
		char chunks_hash[n-1][HASH_SIZE*2];
		chunk_num = n-1;
		i = 0;
		
		while(!feof(fp)){
		    fscanf(fp,"%d %s\n",&id[i],chunks_hash[i]);
		    i++;
		}
		fclose(fp);
		
		int k = 0;
		
		for(j = 0;j<i;j++){
		    for(k=0;k<HASH_SIZE;k++){
		        new_chunks_hash[j][k] = hash2Format(chunks_hash[j][2*k],chunks_hash[j][2*k+1]);
		    }  
		}
		
		//Determine whether I have chunks listed in the WHOHAS packet
		int match[num];
		int m = 0;
		for (j = 0;j<num;j++){
			if(hash_include(chunk_list[j],new_chunks_hash,i))
				match[m++] = j;
		}
		
		if (m!=0){
		
			//Construct the IHAVE packet
			data_packet_t packet;
			packet.header.magicnum = htons(MAGICNUM);
			packet.header.version = VERSION;
			packet.header.packet_type = IHAVE;
			packet.header.header_len = htons(HEADER_LEN);
			packet.header.packet_len = htons(HEADER_LEN + 4 + HASH_SIZE * m);
			packet.data[0] = (char)m;
			for(j = 0; j < m;j++){
				int match_line = match[j];
				for(k = 0; k<HASH_SIZE;k++){
					packet.data[4+j*HASH_SIZE+k] = chunk_list[match_line][k];
				}
			}
			
			//Send out the IHAVE packet				
			spiffy_sendto(my_sock, &packet, sizeof(data_packet_t ), 0, 
				(struct sockaddr *)&from, sizeof(struct sockaddr));
		}
	 
	}
  
	//2. Handle the IHAVE packet
	else if(recv_packet.header.packet_type==IHAVE){
    	//发送IHAVE包中包含的chunk的请求 --> GET packet
    	peer_status = 0;
    	/* Get the needed information from the received packet,
			include number of chunk and the chunks_hash */
    	my_number = (int)(recv_packet.data[0]);//Number of chunk in the IHAVE packet
    	//char chunk_list[num][HASH_SIZE];
	 	int i,j;
	  	for (i=0;i<my_number;i++){
	  		for (j=0;j<HASH_SIZE;j++){
	  			my_chunk_list[i][j] = recv_packet.data[4+i*HASH_SIZE+j];
				printf("%d ",my_chunk_list[i][j]);
	  		}
			printf("\n");
	 	}
	 	my_index = 0;
	 	send_getPacket(from);
	}
    
	//4. Handle the DATA packet
	else if(recv_packet.header.packet_type==DATA){
    	//收到发送方的数据，存储，并返回ACK -->  ACK packet
    	/* Store the information */
		/* Get the seq number from the received packet */
    	u_int seq;
		u_int my_seq = ntohl(recv_packet.header.seq_num);
		printf("seq is %d\n",my_seq);  
		if (my_seq == (cur_seq+1)) {
			int length = ntohs(recv_packet.header.packet_len) - 
				ntohs(recv_packet.header.header_len);//length of the data
			FILE *fp = fopen(my_outputfile,"a+");
			int i;
    		for(i=0;i<length;i++){
      			fputc(recv_packet.data[i],fp);
    		}
    		fclose(fp);
			cur_seq++;
			my_count++;
			seq = my_seq;
		}
		else {
			seq = cur_seq;
		}
		  
		//Construct the ACK packet
		data_packet_t ack_packet;
		ack_packet.header.magicnum = htons(MAGICNUM);
		ack_packet.header.version = VERSION;
		ack_packet.header.packet_type = ACK;
		ack_packet.header.ack_num = htonl(seq);
		ack_packet.header.header_len = htons(HEADER_LEN);
		ack_packet.header.packet_len = htons(HEADER_LEN);
		spiffy_sendto(my_sock, &ack_packet, sizeof(data_packet_t ), 
			0, (struct sockaddr *) &from, sizeof(struct sockaddr));
		if(my_count%375==0){
			my_index++;
			if (my_index < my_number) {
				send_getPacket(from);
			}
		}
	}
	//3. Handle the GET packet
	else if(recv_packet.header.packet_type==GET){
    	//发送get包中请求的chunk -->  DATA packet
    	peer_status = 1;
    	//initialize congestion control state
		window_size = 1;
		ssthresh = ORIGINAL_SSTHRESH;
		lastPacketAcked = 0;
		lastPacketSent = 0;
		congestion_mode = 0; // 0 for slow start and 1 for congestion avoidance
		changeTime = time((time_t*)NULL);
		time_log(window_size);
		int k;
		for (k=0; k<PACKETNUM; k++){
			dataTime_log[k] = 0;
			ackNum_log[k] = 0;
		}
    	char chunk[HASH_SIZE];
		//strcpy(chunk,recv_packet.data);
		int i;
		for ( i = 0; i < HASH_SIZE; i++ ){			
			chunk[i] = recv_packet.data[i];
		}
		hash_id = 0;
		for (i = 0; i < chunk_num; i++){
			if (hash_match(chunk, new_chunks_hash[i]))
				hash_id = id[i];
		}
		//fflush(stdin);
		printf("chunk id is %d\n", hash_id);
		printf("name is %s\n",my_config->chunk_file);
		FILE *fp = fopen(my_config->chunk_file,"r");
		fscanf(fp,"File: %s\n",filename);
		fclose(fp);
		printf("filename is %s\n",filename);
		//Read the data and send the data packet
		fp =  fopen(filename,"r");
		fseek(fp,512*1024*hash_id,SEEK_SET);
		//initialize my_seq_pro for 1
		my_seq_pro = 1;
    	//Construct the DATA packet
		data_packet_t data_packet;
		data_packet.header.magicnum = htons(MAGICNUM);
		data_packet.header.version = VERSION;
		data_packet.header.packet_type = DATA;
		data_packet.header.seq_num = htonl(my_seq_pro);
		data_packet.header.header_len = htons(HEADER_LEN);
		data_packet.header.packet_len = htons(HEADER_LEN+1400);
		int j;
		for (j = 0; j < 1400; j++) {
			data_packet.data[j] = fgetc(fp);
		}
		fclose(fp);
		//modify congestion control state
		lastPacketSent = 1;
		dataTime_log[0] = time((time_t*)NULL);
		
		spiffy_sendto(my_sock, &data_packet, sizeof(data_packet_t ), 
			0, (struct sockaddr *) &from, sizeof(struct sockaddr));		
	}	
	//5. Handle the ACK packet
	else if(recv_packet.header.packet_type==ACK){
        //继续发送
        //alarm(0);
        my_ack = ntohl(recv_packet.header.ack_num);
    	ackNum_log[my_ack-1]++;
        if (ackNum_log[my_ack-1]==1){
        	if (congestion_mode==0){ //slow start
        		window_size++;
        		changeTime = time((time_t*)NULL);
        		time_log(window_size);
        	}
        	else {
        		int sendTime = dataTime_log[my_ack-1];
        		if (changeTime < sendTime) {
        			window_size++;
        			changeTime = time((time_t*)NULL);
        			time_log(window_size);
        		}
        	}
        } else if (ackNum_log[my_ack-1] >= 3) {//fast retransmit
        	int k;
        	for (k=lastPacketAcked;k<MIN(lastPacketAcked+window_size,PACKETNUM);k++){
        		dataTime_log[k] = 0;
        		ackNum_log[k] = 0;
        	}
        	if (congestion_mode){
        		// 1 for congestion avoidance
        		congestion_mode = 0;      		
        	}
        	ssthresh = MAX(window_size/2,2);
        	window_size = 1;  
        	time_log(window_size); 	
        }
        int isAllAcked = 1;
        int i;
        for (i=0;i<my_ack-1;i++) {
        	if (ackNum_log[i]==0){
        		isAllAcked = 0;
        		break;
        	}      		
        }
        if (isAllAcked) {
        	lastPacketAcked = my_ack;
        }
        int toSendNum = MIN(lastPacketAcked + window_size,PACKETNUM) - lastPacketSent;
        for (i = 0; i < toSendNum && (lastPacketSent+i) < PACKETNUM; i++) {
 			//Read the data and send the data packet
			FILE* fp =  fopen(filename,"r");
			fseek(fp,512*1024*hash_id + (lastPacketSent+i)*1400,SEEK_SET);
			my_seq_pro=lastPacketSent+i+1;	
        	//Construct the DATA packet
			data_packet_t data_packet;
			data_packet.header.magicnum = htons(MAGICNUM);
			data_packet.header.version = VERSION;
			data_packet.header.packet_type = DATA;
			data_packet.header.seq_num = htonl(my_seq_pro);
			data_packet.header.header_len = htons(HEADER_LEN);
			if (lastPacketSent+i == PACKETNUM-1) {
	        	data_packet.header.packet_len = htons(HEADER_LEN+688);
	        	int j;
		    	for (j = 0; j < 688; j++) {
			    	data_packet.data[j] = fgetc(fp);
		    	}
			} else {
		    	data_packet.header.packet_len = htons(HEADER_LEN+1400);
		    	int j;
		    	for (j = 0; j < 1400; j++) {
			    	data_packet.data[j] = fgetc(fp);
		    	}
			}			
			fclose(fp);
			//alarm(10);
			dataTime_log[lastPacketSent+i] = time((time_t*)NULL);
			spiffy_sendto(my_sock, &data_packet, sizeof(data_packet_t ), 
				0, (struct sockaddr *) &from, sizeof(struct sockaddr));	       
        }
        lastPacketSent = MIN(lastPacketAcked + window_size,PACKETNUM);  	
	}
}
void resend(){
	printf("Time out, call resend\n");
	int k;
	for (k=lastPacketAcked;k<MIN(lastPacketAcked+window_size,PACKETNUM);k++){
		dataTime_log[k] = 0;
		ackNum_log[k] = 0;
	}
	if (congestion_mode){
		// 1 for congestion avoidance
		congestion_mode = 0;      		
	}
	ssthresh = MAX(window_size/2,2);
	window_size = 1; 
	changeTime = time((time_t*)NULL);
    time_log(window_size);
    //Read the data and send the data packet
	FILE* fp =  fopen(filename,"r");
	fseek(fp,512*1024*hash_id + lastPacketAcked * 1400,SEEK_SET);
	my_seq_pro=lastPacketAcked+1;	
    //Construct the DATA packet
	data_packet_t data_packet;
	data_packet.header.magicnum = htons(MAGICNUM);
	data_packet.header.version = VERSION;
	data_packet.header.packet_type = DATA;
	data_packet.header.seq_num = htonl(my_seq_pro);
	data_packet.header.header_len = htons(HEADER_LEN);
	if (lastPacketAcked == 374) {
		data_packet.header.packet_len = htons(HEADER_LEN+688);
		int j;
		for (j = 0; j < 688; j++) {
			data_packet.data[j] = fgetc(fp);
		}
	} else {
		data_packet.header.packet_len = htons(HEADER_LEN+1400);
		int j;
		for (j = 0; j < 1400; j++) {
			data_packet.data[j] = fgetc(fp);
		}
	}
	fclose(fp);
	dataTime_log[lastPacketAcked] = time((time_t*)NULL);
	spiffy_sendto(my_sock, &data_packet, sizeof(data_packet_t ), 
		0, (struct sockaddr *) &my_from, sizeof(struct sockaddr));	 
			
}
void send_getPacket(struct sockaddr_in from){
	cur_seq = 0;
	//Construct the GET packet
	data_packet_t get_packet;
	get_packet.header.magicnum = htons(MAGICNUM);
	get_packet.header.version = VERSION;
	get_packet.header.packet_type = GET;
	get_packet.header.header_len = htons(HEADER_LEN);
	get_packet.header.packet_len = htons(HEADER_LEN + HASH_SIZE);
		
	int i;
	//Fill the payload 
	for(i = 0; i < HASH_SIZE; i++){
		get_packet.data[i] = my_chunk_list[my_index][i];
	}
		
	//Send out the GET packet				
	spiffy_sendto(my_sock, &get_packet, sizeof(data_packet_t ), 
		0, (struct sockaddr *) &from, sizeof(struct sockaddr));
}

int hash_include(char son[], char father[][HASH_SIZE], int my_hash_num){
	int i;
	for (i = 0;i<my_hash_num;i++){		
		if(hash_match(son,father[i]))
			return 1;
	}	
	return 0;
}

int hash_match(char hash1[], char hash2[]) {
	int j;
	for(j = 0; j<HASH_SIZE;j++){
		if(hash1[j] != hash2[j])
			return 0;
	}
	return 1;
}

char hash2Format(char a,char b){

	int high = 0;
	int low = 0;	

	if((a -'0')>=10)
		high = 10 + a-'a';
	else
		high = a-'0';
	if((b -'0')>=10)
		low = 10 + b-'a';
	else
		low = b -'0';

	return (char)(high*16+low);
}
void time_log(int win_size) {
	FILE* fp = fopen("problem2-peer.txt","a+");
	clock_t cTime = clock();
	int time_seg = (int)(cTime-start_time);
	fprintf(fp,"f%d\t%d\t%d\n",hash_id,time_seg,win_size);
	fclose(fp);
}
void process_get(char *chunkfile, char *outputfile) {
	strcpy(my_outputfile, outputfile);
    /*Read chunk file into two array(id and hash)*/
    FILE *fp = fopen(chunkfile,"r");
    int n = 0;
    char tmp[64];
    while(!feof(fp)){
        fgets(tmp,64,fp);
        n++;
    }
    fclose(fp);
    fp = fopen(chunkfile,"r");
    char hash[n-1][HASH_SIZE*2];
    int id[n-1];
    int i = 0;	
    while(!feof(fp)){
        fscanf(fp,"%d %s\n",&id[i],hash[i]);
        i++;
    }
    fclose(fp);
    
    /*construct the WHOHAS packet*/
    data_packet_t packet;
    packet.header.magicnum = htons(MAGICNUM);
    packet.header.version = VERSION;
    packet.header.packet_type = WHOHAS;
    packet.header.header_len = htons(HEADER_LEN);
    packet.header.packet_len = htons(HEADER_LEN + 4 + HASH_SIZE * i);
    packet.data[0] = (char)i;
    int j = 0;
    int k = 0;
    for(;j<i;j++){
        for(k=0;k<HASH_SIZE;k++){
            packet.data[4+HASH_SIZE*j+k] = hash2Format(hash[j][2*k],hash[j][2*k+1]);
        }
    }
    
    printf("0x%x %d %d\n",(packet.header.magicnum),(packet.header.version),(packet.header.packet_type));
    printf("0x%x 0x%x\n",(packet.header.header_len),(packet.header.packet_len) );
    
    
    /*Establish network udp socket */
    bt_peer_t* peer = my_config->peers;
    while(peer!=NULL){
    	if(my_config->identity != peer->id){
    		spiffy_sendto(my_sock, &packet, sizeof(data_packet_t ), 
				0, (struct sockaddr *)(&(peer->addr)), sizeof(struct sockaddr));
    	}
    	peer = peer->next;
	}
    
}


void handle_user_input(char *line, void *cbdata) {
  	char chunkf[128], outf[128];

  	bzero(chunkf, sizeof(chunkf));
  	bzero(outf, sizeof(outf));

  	if (sscanf(line, "GET %120s %120s", chunkf, outf)) {
    	if (strlen(outf) > 0) {
      		process_get(chunkf, outf);
    	}
  	}
}


void peer_run(bt_config_t *config) {
  	int sock;
  	struct sockaddr_in myaddr;
  	fd_set readfds;
  	struct user_iobuf *userbuf;
    int oldTime, currentTime;
  	if ((userbuf = create_userbuf()) == NULL) {
    	perror("peer_run could not allocate userbuf");
    	exit(-1);
  	}
  
  	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)) == -1) {
    	perror("peer_run could not create socket");
    	exit(-1);
  	}
  	my_sock = sock;
  
  	bzero(&myaddr, sizeof(myaddr));
  	myaddr.sin_family = AF_INET;
  	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  	myaddr.sin_port = htons(config->myport);
  
  	if (bind(sock, (struct sockaddr *) &myaddr, sizeof(myaddr)) == -1) {
    	perror("peer_run could not bind socket");
    	exit(-1);
  	}
  
  	spiffy_init(config->identity, (struct sockaddr *)&myaddr, sizeof(myaddr));
    oldTime = time((time_t*)NULL);
  	while (1) {
    	int nfds;
    	FD_SET(STDIN_FILENO, &readfds);
    	FD_SET(sock, &readfds);
    	nfds = select(sock+1, &readfds, NULL, NULL, NULL);
    	if (nfds > 0) {
      		if (FD_ISSET(sock, &readfds)) {
      			if (window_size == ssthresh) {
      				congestion_mode = 1;
      			}
				process_inbound_udp(sock);
      		} else if (FD_ISSET(STDIN_FILENO, &readfds)) {
				process_user_input(STDIN_FILENO, userbuf, handle_user_input,
			   		"Currently unused");
      		} else {
      			if (peer_status == 1) {
    				currentTime = time((time_t*)NULL);
    				if (currentTime-oldTime>1) {
    	    			oldTime = currentTime;
     					int lossNum = -1;
    					int i;
    					for (i = lastPacketAcked; i < MIN(lastPacketAcked + window_size,PACKETNUM); i++){
							if (ackNum_log[i]==0) {
								lossNum = i;
								break;
    						}
						}
						if (lossNum > -1) {
							int t1 = dataTime_log[i];
							int t2 = time((time_t*)NULL);
							if ( (t2-t1) > 5 ){
								resend();
							}
						}   	    					    			
    				}
    			}
    		}    		
    	} 
 	}
}
