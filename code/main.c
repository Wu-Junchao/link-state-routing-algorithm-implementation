#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include<sys/time.h>
#include <stdbool.h>

//#include "monitor_neighbors.c"


void listenForNeighbors();
void* announceToNeighbors(void* unusedParam);

#define NodeNum 256
int globalMyID;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[NodeNum];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[NodeNum];

int ** cost_graph;

int* forwarding_table;

char* logFileName;

struct lsa
{
	uint8_t v;  // id
	int seq_num;
	int content[NodeNum]; //index i (node) cost; -1 if not neighbor
};

struct lsa* local_lsa;

int main(int argc, char** argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s mynodeid initialcostsfile logfile\n\n", argv[0]);
		exit(1);
	}
	
	//initialization: get this process's node ID, record what time it is, 
	//and set up our sockaddr_in's for sending to the other nodes.
	globalMyID = atoi(argv[1]);
	int i,j;
	for(i=0;i<NodeNum;i++)
	{
		gettimeofday(&globalLastHeartbeat[i], 0);
		char tempaddr[100];
		sprintf(tempaddr, "10.1.1.%d", i);
		memset(&globalNodeAddrs[i], 0, sizeof(globalNodeAddrs[i]));
		globalNodeAddrs[i].sin_family = AF_INET;
		globalNodeAddrs[i].sin_port = htons(7777);
		inet_pton(AF_INET, tempaddr, &globalNodeAddrs[i].sin_addr);
	}
	
	// initialize a cost graph
	cost_graph = malloc(NodeNum * sizeof(int*));
	
	for (i=0;i<NodeNum;i++){
		cost_graph[i] = malloc(NodeNum * sizeof(int));
		for (j=0;j<NodeNum;j++){
			cost_graph[i][j] = (i==j)? 0 :-1;
		}
	}

	// initialize a forwarding table
	forwarding_table = malloc(NodeNum*sizeof(int));
	for (i=0;i<NodeNum;i++){
		forwarding_table[i] = -1;
	}
	
	// initialize a local lsa
	local_lsa=malloc(NodeNum*sizeof(struct lsa));
	for (i=0;i<NodeNum;i++){
		local_lsa[i].v = i;
		local_lsa[i].seq_num = -1;
		for (j = 0;j<NodeNum;j++){
			local_lsa[i].content[j] = (i==j) ? 0 :-1;

		}
	}
	
	//TODO: read and parse initial costs file. default to cost 1 if no entry for a node. file may be empty.
	char* costFileName = argv[2];
	logFileName = argv[3];
	FILE* fp;
    char* line = NULL;
    size_t len = 128;

    fp = fopen(costFileName, "r");
    if (fp == NULL){
		printf("Cannot open cost file.\n");
        exit(EXIT_FAILURE);}

    while ((getline(&line, &len, fp)) != -1) {
		cost_graph[globalMyID][atoi(line)] = atoi(strchr(line,' ')+1);
        //cost_graph[atoi(line)][globalMyID] = cost_graph[globalMyID][atoi(line)];
        local_lsa[globalMyID].content[atoi(line)] = cost_graph[globalMyID][atoi(line)];
    }

    fclose(fp);
    if (line)
        free(line);

    fp = fopen(logFileName,"w");
    fclose(fp);
	
	//socket() and bind() our socket. We will do all sendto()ing and recvfrom()ing on this one.
	if((globalSocketUDP=socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}
	char myAddr[100];
	struct sockaddr_in bindAddr;
	sprintf(myAddr, "10.1.1.%d", globalMyID);	
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(7777);
	inet_pton(AF_INET, myAddr, &bindAddr.sin_addr);
	if(bind(globalSocketUDP, (struct sockaddr*)&bindAddr, sizeof(struct sockaddr_in)) < 0)
	{
		perror("bind");
		close(globalSocketUDP);
		exit(1);
	}
	
	//start threads... feel free to add your own, and to remove the provided ones.
	pthread_t announcerThread;
	pthread_create(&announcerThread, 0, announceToNeighbors, (void*)0);
	
	//good luck, have fun!
	listenForNeighbors();
}
