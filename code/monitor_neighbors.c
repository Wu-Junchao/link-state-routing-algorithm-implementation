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
#define NodeNum 256

extern int globalMyID;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
extern struct timeval globalLastHeartbeat[NodeNum];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
extern int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
extern struct sockaddr_in globalNodeAddrs[NodeNum];

struct lsa
{
    uint8_t v;  // id
    int seq_num;
    int content[NodeNum]; //index i (node) cost; -1 if not neighbor
};

extern struct lsa* local_lsa;

extern int* forwarding_table;

extern int ** cost_graph;

extern char* logFileName;

void print_graph(){
    int i,j;
    const int t = 9;
    for(i = 0; i < t; i++)
    {
        printf("from %d: ",i);
        for(j = 0; j < t; j++)
        {
            printf("%d\t", cost_graph[i][j]);
        }
        printf("\n");
    }
}

void print_forwardingtable(){
    int i,j;
        printf("Node %d fwt: ",globalMyID);
        for (j=0;j<9;j++){
            printf("%d\t",forwarding_table[j]);
        }
        printf("\n");
    }


void print_lsa(){
    int i;
    for(i=0;i<4;i++){
        printf("to node %d,%d\n",i,local_lsa[globalMyID].content[i]);
    }
}

void calculate_shortest_path()
{
    const int MAXINT = 114514;
    bool S[NodeNum];
    int dist[NodeNum];
    int prev[NodeNum];
    int i,j,t,u,res;
    for(i=0; i<NodeNum; i++)
    {
        dist[i] = cost_graph[globalMyID][i];
        S[i] = false;
        forwarding_table[i]=-1;
        if (dist[i]>0){
            prev[i] = globalMyID;
        }
        else{
            prev[i] = -1;
        }
    }

    dist[globalMyID] = 0;
    S[globalMyID] = true;
    for(i=1; i<NodeNum; i++) {
        int mindist = MAXINT;
        u=-1;
        for (j = NodeNum-1; j >=0; j--) {
            if (!S[j] && dist[j] > 0 && dist[j] <= mindist) {
                u = j;
                mindist = dist[j];
            }
        }
        if (u==-1) {break;}//// no place to go}
        S[u] = true;
        //printf("%d",u);

        for (j = NodeNum-1; j >=0; j--) {
            if (!S[j] && cost_graph[u][j] > 0 ) {
                if (dist[u] + cost_graph[u][j] < dist[j] || dist[j]<0) {
                    dist[j] = dist[u] + cost_graph[u][j];
                    prev[j] = u;
                }
                else if (dist[u] + cost_graph[u][j] == dist[j]){
                    int traceu[20];
                    int tracej[20];
                    int tu = u;
                    int tj = j;
                    int indexu=0,indexj=0;
                    while (prev[tu]!=globalMyID){
                        traceu[indexu]=tu;
                        tu = prev[tu];
                        indexu++;
                    }
                    while (prev[tj]!=globalMyID){
                        tj = prev[tj];
                        tracej[indexj]=tj;
                        indexj++;
                    }
                    indexj--;
                    indexu--;
                    while (tracej[indexj]==traceu[indexu]){
                        indexj--;
                        indexu--;
                    }
                    if (tracej[indexj]>traceu[indexu]){
                        dist[j] = dist[u] + cost_graph[u][j];
                        prev[j]=u;
                    }
                }
            }
        }
    }
    // build forwarding map
    for (i=0;i<NodeNum;i++){
        t=i;
        res=-1;
        while ( prev[t] !=-1){
            res = t;
            t=prev[t];
        }
        forwarding_table[i] =res;
    }
}

void writeLog(char* content,char* filename){
    FILE* fp;
    fp = fopen(filename, "a");
    if (fp == NULL){
        printf("Cannot open cost file.\n");
        exit(EXIT_FAILURE);}
    fwrite(content,1,strlen(content),fp);
    fclose(fp);
}

//Yes, this is terrible. It's also terrible that, in Linux, a socket
//can't receive broadcast packets unless it's bound to INADDR_ANY,
//which we can't do in this assignment.
void hackyBroadcast(const char* buf, int length)
{
	int i;
	for(i=0;i<256;i++)
		if(i != globalMyID) //(although with a real broadcast you would also get the packet yourself)
			sendto(globalSocketUDP, buf, length, 0,
				  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

void broadcastLSA(struct lsa* LSA,short int heardfrom){
    char* content= malloc(8+NodeNum * sizeof(int));
    if (globalMyID == LSA -> v) {LSA -> seq_num++;}
    memcpy(content,"lsa",3);
    memcpy(content+3,&(LSA -> v),sizeof(uint8_t));
    memcpy(content+4,&(LSA -> seq_num),sizeof(int));
    int cost;
    for (int i=0;i<NodeNum;i++){
        cost = (LSA -> content)[i];
        memcpy(content+8+sizeof(int)*i,&cost,sizeof(int));
    }
    calculate_shortest_path();
    for (int i=0;i<NodeNum;i++){
        if (i!=heardfrom && i!=globalMyID && local_lsa[globalMyID].content[i]>0){ // && local_lsa[globalMyID].content[i]>0
//            if (i==8){
//                printf("send node %d's LSA (seq %d) by %d to %d. \n",LSA->v,LSA->seq_num,globalMyID,i);
//            }

            sendto(globalSocketUDP, content, 8+NodeNum * sizeof(int), 0, (struct sockaddr *) &globalNodeAddrs[i],
                   sizeof(globalNodeAddrs[i]));
        }
    }
}

void* announceToNeighbors(void* unusedParam)
{
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 300 * 1000 * 1000; //300 ms
	while(1)
	{
		hackyBroadcast("HEREIAM", 7);
		nanosleep(&sleepFor, 0);
	}
}



void listenForNeighbors()
{
	char fromAddr[100];
	struct sockaddr_in theirAddr;
	socklen_t theirAddrLen;
	unsigned char recvBuf[1500];
    char content[2048];
	int bytesRecvd;
    int i;
	while(1)
	{
		theirAddrLen = sizeof(theirAddr);
        bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1500 , 0,(struct sockaddr*)&theirAddr, &theirAddrLen);
		if (bytesRecvd == -1)
		{
			perror("connectivity listener: recvfrom failed");
			exit(1);
		}
		
		inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);
        recvBuf[bytesRecvd] = '\0';
		short int heardFrom = -1;
		if(strstr(fromAddr, "10.1.1."))
		{
			heardFrom = atoi(
					strchr(strchr(strchr(fromAddr,'.')+1,'.')+1,'.')+1);
			
			//TODO: this node can consider heardFrom to be directly connected to it; do any such logic now.
			if (local_lsa[globalMyID].content[heardFrom] <=0) {// not neighbor
			    cost_graph[globalMyID][heardFrom] =-cost_graph[globalMyID][heardFrom];
			    //cost_graph[heardFrom][globalMyID] = cost_graph[globalMyID][heardFrom];
                local_lsa[globalMyID].content[heardFrom] = cost_graph[globalMyID][heardFrom];

                broadcastLSA(&local_lsa[globalMyID], globalMyID);
			}
			//record that we heard from heardFrom just now.
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);
		}

		struct timeval currentTime;
		gettimeofday(&currentTime,0);
		for (i=0;i<NodeNum;i++){
		    if (local_lsa[globalMyID].content[i]>0) //is neighbor
            {
		        if ((currentTime.tv_sec - globalLastHeartbeat[i].tv_sec)*1000000
                    + currentTime.tv_usec - globalLastHeartbeat[i].tv_usec > 600 * 1000*4){
		            printf("discard\n");
		            cost_graph[globalMyID][i] = -cost_graph[globalMyID][i];
		            //cost_graph[i][globalMyID] = cost_graph[globalMyID][i];
                    local_lsa[globalMyID].content[i] = cost_graph[globalMyID][i];

                    broadcastLSA(&local_lsa[globalMyID], globalMyID);
                }
            }
		}
        //print_graph();
		//Is it a packet from the manager? (see mp2 specification for more details)
		//send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		if(!strncmp(recvBuf, "send", 4) || (!strncmp(recvBuf, "fwrd", 4) ))
		{
            //print_graph();
            //print_forwardingtable();
			//TODO send the requested message to the requested destination node
            int16_t destID = (recvBuf[4] << 8 ) | ((uint8_t)recvBuf[5]);
            if (destID>255 || destID<0 ) {

                sprintf(content,"unreachable dest %d",destID);
                writeLog(content,logFileName);
            } // log unreachable
			else if (destID == globalMyID){
				printf("Reach the dest %d.\n",globalMyID);
				sprintf(content,"receive packet message %s\n",&recvBuf[6]);
				writeLog(content,logFileName);
			}
			else if ( forwarding_table[destID]<0) {
//                print_graph();
//                print_forwardingtable();
                sprintf(content,"unreachable dest %d\n",destID);
                writeLog(content,logFileName);
            }
			else{
			    printf("Forward to dest %d.\n",destID);
			    calculate_shortest_path();
			    if (!strncmp(recvBuf, "send", 4)){

//                    print_graph();
//                    print_forwardingtable();
                    sprintf(content, "sending packet dest %d nexthop %d message %s\n", destID, forwarding_table[destID],
                            &recvBuf[6]);
                    memcpy(recvBuf,"fwrd",4);
			    }
			    else {
                    sprintf(content, "forward packet dest %d nexthop %d message %s\n", destID, forwarding_table[destID],
                            &recvBuf[6]);
                }
			    sendto(globalSocketUDP, recvBuf, bytesRecvd, 0, (struct sockaddr *) &globalNodeAddrs[forwarding_table[destID]],
                                     sizeof(globalNodeAddrs[forwarding_table[destID]]));
			    writeLog(content,logFileName);
            }
		}
		else if(!strncmp(recvBuf,"lsa",3)){

		    struct lsa* temp_lsa;
		    temp_lsa=malloc(sizeof(struct lsa));
		    memcpy(&(temp_lsa->v),recvBuf+3,sizeof(uint8_t));
		    memcpy(&(temp_lsa->seq_num),recvBuf+4,sizeof(int));
//            if (globalMyID==8){
//                printf("receive lsa generated by %d, heardfrom node %d. \n",temp_lsa->v,heardFrom);
//            }
		    if (temp_lsa->seq_num>local_lsa[temp_lsa->v].seq_num){

		        for (i = 0;i<NodeNum;i++){
                    memcpy(&(temp_lsa->content[i]),recvBuf+8+i*(sizeof(int)),sizeof(int));
		        }
		        //copy
		        local_lsa[temp_lsa->v].seq_num = temp_lsa->seq_num;
		        bool flg=false;
		        for (i=0;i<NodeNum;i++){
		            if (local_lsa[temp_lsa->v].content[i]<0 && temp_lsa->content[i]>0){
                        flg=true;
		            }
		            local_lsa[temp_lsa->v].content[i] = temp_lsa->content[i];
                    cost_graph[temp_lsa->v][i] = local_lsa[temp_lsa->v].content[i];
		        }
                broadcastLSA(temp_lsa,heardFrom);
                if (flg==true){
                    broadcastLSA(&local_lsa[globalMyID],globalMyID);
                }

		    }
		}
	}
	//(should never reach here)
	close(globalSocketUDP);
}

