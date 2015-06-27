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
#define HASHNUM_MAX 4

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
void send_getPacket(struct sockaddr_in from);
int my_sock,my_index,my_number,my_count;
char my_chunk_list[HASHNUM_MAX][HASH_SIZE];

int main(int argc, char **argv) {
  bt_config_t config;
  my_count=0;
  bt_init(&config, argc, argv);
	
  my_config = &config;
  
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

  	fromlen = sizeof(from);

  	//Get the received packet
  	spiffy_recvfrom(sock, buf, BUFLEN, 0, (struct sockaddr *) &from, &fromlen);
  	data_packet_t recv_packet; 
  	recv_packet = *(data_packet_t *)buf;
  
  	/* Deal with different kinds of received packet */
  
  	//1. Handle the WHOHAS packet
	if (recv_packet.header.packet_type==WHOHAS){
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
		int id[n-1];
		i = 0;
		
		while(!feof(fp)){
		    fscanf(fp,"%d %s\n",&id[i],chunks_hash[i]);
		    i++;
		}
		fclose(fp);
		
		char new_chunks_hash[i][HASH_SIZE];
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
			spiffy_sendto(my_sock, &packet, sizeof(data_packet_t ), 0, (struct sockaddr *) 								&from, sizeof(struct sockaddr));
		}
	 
	}
  
	//2. Handle the IHAVE packet
	else if(recv_packet.header.packet_type==IHAVE){
    	//发送IHAVE包中包含的chunk的请求 --> GET packet
    	
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
  
	//3. Handle the GET packet
	else if(recv_packet.header.packet_type==GET){
    	//发送get包中请求的chunk -->  DATA packet
    	
    
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
		
/*		if (seq == 375) {
			printf("finished!\n");
			fflush(stdin);		
		}
*/	

		//printf("the length is %d\npacket_len is %d\nheader_len is %d\ndata_length is %d\n", length, ntohs(recv_packet.header.packet_len), ntohs(recv_packet.header.header_len), strlen(recv_packet.data));
		//strcpy(my_outputfile,"test1.txt");
    	
    	//printf("my_outputfile name is %s\n",my_outputfile);

    
    	
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
			send_getPacket(from);
		}
	}
	
	//5. Handle the ACK packet
	else if(recv_packet.header.packet_type==ACK){
    	//继续发送
    
	}
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
		
	//printf("The get packet contains\n%s\n",toCheck);
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
 
void process_get(char *chunkfile, char *outputfile) {
    //printf("output file name is %s\n", outputfile);
	strcpy(my_outputfile, outputfile);
	/*	FILE *fp = fopen(my_outputfile,"a");
		fputc('|',fp);
		fclose(fp);
	*/
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
  
  	while (1) {
    	int nfds;
    	FD_SET(STDIN_FILENO, &readfds);
    	FD_SET(sock, &readfds);
    
    	nfds = select(sock+1, &readfds, NULL, NULL, NULL);
    
    	if (nfds > 0) {
      		if (FD_ISSET(sock, &readfds)) {
				process_inbound_udp(sock);
      		}
      
      		if (FD_ISSET(STDIN_FILENO, &readfds)) {
				process_user_input(STDIN_FILENO, userbuf, handle_user_input,
			   		"Currently unused");
      		}
    	}
  	}
}
