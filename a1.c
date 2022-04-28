#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <arpa/inet.h>
#include <time.h>

#define BUFSIZE 10485760 //Max http request/respond headers size: B; 
#define Res_BUFSIZE 10485760 //Max http respond message size: B;
#define MAXLINE 100 //Max length of characters for a header line
#define MAX_OBJECT_LEN 100 //Max length of characters for cache object name
#define MAX_OBJECT_SIZE 10485760 //Max size of cache object
#define MAX_CACHE_NUM 10 // Max allowed number of caches
#define DefaultPort 80 //default port number 
#define default_Max_age 3600 // default max age
int cache_count = 0; 
char *ageStr = "Age: ";

/* A cache object/block */
typedef struct cache_object {
	char tag[MAX_OBJECT_LEN];
	char data[MAX_OBJECT_SIZE];
  int respond_mess_len;
	time_t timestamp; //the timestamp that data was first stored in the cache
  int access_counts;  
	struct cache_object *next;
}cache_object;

typedef struct res_header{
  int max_age;
  size_t content_len;
}res_header;

cache_object *cache_start=NULL;

/* This method searches for the requested resource in the cache and returns the pointer to the cache block which contains the resource.
   Returns NULL otherwise.*/
cache_object *check_cache_hit(char *tag) {
	cache_object *ptr = cache_start;
  // printf("cache_start:%s\n",cache_start->tag);
  char *temp=tag;
	while(ptr != NULL) 
	{  
    if(!strcmp(ptr->tag, temp)) {return ptr;}
		ptr = ptr->next;
	}
	return NULL;
}

/* Read the request headers from the client and populate the request content*/
int read_requesthdrs(char *buf, char *URL, char *hostname){
    int port;
    char *buf_temp = (char *) malloc(BUFSIZE);
    char *key, *temp;
    strcpy(buf_temp, buf);
    if((temp = strstr(buf_temp, "GET")) != NULL){
        temp = temp + 4;
        key = strtok(temp, " ");
        strcpy(URL, key);
    }

    strcpy(buf_temp, buf);
    if((temp = strstr(buf_temp, "Host")) != NULL){
        temp = temp + 6;
        key = strtok(temp, "\r\n");
        if(strstr(key, ":") != NULL){
            key = strtok(key, ":");
            strcpy(hostname, key);
            key = strtok(NULL, ":");
            port = atoi(key);
        }
        else{
            port = DefaultPort;
            strcpy(hostname, key);
        }
    }
    free(buf_temp);
    return port;
}

res_header *extract_res_headers(char*Res_buffer){
    res_header *header = malloc(sizeof(res_header));
    bzero(header,sizeof(res_header));
    char *token, *buffer, *temp;
    buffer = (char*) malloc(Res_BUFSIZE);
    bzero(buffer, Res_BUFSIZE);
    memcpy(buffer, Res_buffer, Res_BUFSIZE);

    if((temp = strstr(buffer, "Cache-Control")) != NULL){
        memcpy(buffer, Res_buffer, Res_BUFSIZE);
        if((temp = strstr(buffer, "max-age")) != NULL){
            token = strtok(temp, "\r\n");
            token = strtok(temp, "=");
            token = strtok(NULL, "=");
            header->max_age = atoi(token);
        }
        else{header->max_age=default_Max_age;}
    }
    else{header->max_age=default_Max_age;}
    
    bzero(buffer, Res_BUFSIZE);
    memcpy(buffer, Res_buffer, Res_BUFSIZE);

    if((temp = strstr(buffer, "Content-Length")) != NULL){
        token = strtok(temp, "\r\n");
        token = strtok(temp, ":");
        token = strtok(NULL, ":");
        header->content_len = atoi(token);
    }
    else{
        header->content_len = 0;
    }
    free(buffer);
    return header;
}

void reorder_cache(char *tag){
    cache_object *target = check_cache_hit(tag);
    // find the precessor of the target
    if(target!=cache_start && target!=NULL){
        cache_object *ptr = cache_start;
        while(ptr != NULL) 
        {  
        if(ptr->next==target) {break;}
        ptr = ptr->next;
        }
        ptr->next = target->next;
        target->next = cache_start;
        cache_start = target;
    }
}

/* 
	* Function to cache the response received from the server. 
    The function checks to see if the response is within allowed object size and is in limit with the allowed cache number. 
	* If the cache is free, a new cache object is added. Else an existing block is evicted and the old content is replaced with the new data.
*/
void write_to_cache(char tag[MAX_OBJECT_LEN], char data[MAX_OBJECT_SIZE], int respond_mess_len) {
    cache_object *object = malloc(sizeof(cache_object)),*hit_stat;
    //Check if the tag has already existed in the cache list
    hit_stat = check_cache_hit(tag);
    //The tag has been found in the cache - update
    if (hit_stat!=NULL){
        memcpy(hit_stat->data,data,MAX_OBJECT_SIZE); //update new respond body
		    hit_stat->timestamp = time(NULL); //update time
        hit_stat->access_counts = 1; //update access counts
        hit_stat->respond_mess_len=respond_mess_len; //update respond message length
        reorder_cache(tag);
    }
	  //Tag is new and cache is not full - Add a new block without eviction
    else if(cache_count + 1 <= MAX_CACHE_NUM) {
      //Creating node
      memcpy(object->data,data,MAX_OBJECT_SIZE);
      memcpy(object->tag,tag,MAX_OBJECT_LEN);
      object->timestamp=time(NULL);
      object->access_counts=1;
      object->respond_mess_len=respond_mess_len;
      //Adding node to the front of the list
      object->next = cache_start;
      cache_start = object;
      cache_count += 1;
    }
    //Tag is new and cache is full - Evict and add new data
    else {
      cache_object *victim, *victim_pre=NULL, *ptr;
      res_header *header;

      int token = 0;
      ptr = cache_start;
      //try to find a stale item from the cache list to evict
      while(ptr != NULL) {
        header = extract_res_headers(ptr->data);
        if((time(NULL)-ptr->timestamp)>=header->max_age){
              victim = ptr;
              printf("Find stale item with URL: %s\n",victim->tag);
              token=1; //if found, token be set to 1
              break; //break the search
        }
        ptr=ptr->next;
      }
      //if do not find a stale item, then try to find Least Recently Accessed block (the last item) to evict
      if(!token){
        printf("No stale item exists, then evict the least recently accessed item\n");
        ptr = cache_start;
        while(ptr != NULL) {
          if(ptr->next==NULL){
            victim = ptr;
            break;
          }
          ptr=ptr->next;
        }
      }
      printf("The URL(tag) of evicted item is: %s\n",victim->tag);
      //Modify victim
      memcpy(victim->data,data,MAX_OBJECT_SIZE);
      memcpy(victim->tag,tag,MAX_OBJECT_LEN);
      victim->timestamp = time(NULL);
      victim->access_counts=1;
      victim->respond_mess_len=respond_mess_len;
      //add victim to the start of the linked list
      reorder_cache(tag);
      }
}

int main(int argc, char **argv) {
  int client_listenfd; /* listening socket */
  int client_connfd; /* connection socket */
  int portno; /* port to listen on */
  int clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* proxy server's addr */
  struct sockaddr_in clientaddr; /* client addr */
  struct hostent *hostp; /* client host info */
  struct resp_content *res_content;
  char *hostaddrp; /* dotted decimal host addr string */
  int optval; /* flag value for setsockopt */
  int n,n1,n2,n3; /* message byte size */

  res_header *header;
  size_t *content_len,rem_bytes;
  int temp=n2;
  int port=DefaultPort;

  /* check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  portno = atoi(argv[1]);

  /* socket: create a socket */
  client_listenfd = socket(AF_INET, SOCK_STREAM, 0);
  int error();
  if (client_listenfd < 0){
     error("ERROR opening socket");
  }

  /* setsockopt: Handy debugging trick that lets 
   * us rerun the server immediately after we kill it; 
   * otherwise we have to wait about 20 secs. 
   * Eliminates "ERROR on binding: Address already in use" error. 
   */
  optval = 1;
  setsockopt(client_listenfd, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));

  /* build the proxy server's internet address */
  bzero((char *) &serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET; /* we are using the Internet */
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); /* accept reqs to any IP addr */
  serveraddr.sin_port = htons((unsigned short)portno); /* port to listen on */

  /* bind: associate the listening socket with a port */
  if (bind(client_listenfd, (struct sockaddr *) &serveraddr, 
	   sizeof(serveraddr)) < 0) 
    error("ERROR on binding");

  /* listen: make it a listening socket ready to accept connection requests */
  if (listen(client_listenfd, 100) < 0) /* allow 100 requests to queue up */ 
    error("ERROR on listen");

  while (1) {
  /* main loop: wait for a connection request, get http message, then close connection. */
  clientlen = sizeof(clientaddr);

  /* accept: wait for a connection request */
  client_connfd = accept(client_listenfd, (struct sockaddr *) &clientaddr, &clientlen);
  if (client_connfd < 0) 
      error("ERROR on accept");
  
  /* gethostbyaddr: determine who sent the message */
  hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
      sizeof(clientaddr.sin_addr.s_addr), AF_INET);
  if (hostp == NULL)
    error("ERROR on gethostbyaddr");
  hostaddrp = inet_ntoa(clientaddr.sin_addr);
  if (hostaddrp == NULL)
    error("ERROR on inet_ntoa\n");
  printf("***************************************************************************************************************************\n");
  printf("\nDest server established connection with %s (%s)\n", 
    hostp->h_name, hostaddrp);
  
  /* read: read input string from the client */
  int request_time=0;
  char *buf = (char *) malloc(BUFSIZE);
  bzero(buf, BUFSIZE);
  n = read(client_connfd, buf, BUFSIZE);
  // the time a request is made
  request_time=time(NULL);

  if (n < 0) 
    error("ERROR reading from socket");
  printf("\nProxy server received %d bytes:\n%s", n, buf);

  char *req_hostname = (char *) malloc(MAXLINE);
  char *URL = (char *) malloc(MAXLINE);
  bzero(req_hostname, MAXLINE);
  bzero(URL, MAXLINE);
  
  port = read_requesthdrs(buf, URL, req_hostname);
  printf("Dest Server-URL:%s, Hostname:%s, Port:%d\n",URL,req_hostname,port);

  cache_object *tar_cache;
  tar_cache = check_cache_hit(URL);
  // if URL(tage) is in cache
  if(tar_cache!=NULL){
      header = extract_res_headers(tar_cache->data);
      printf("respond message: content length:%d, max-age:%d\n",header->content_len,header->max_age);
      // if data is not stale
      if((request_time+header->max_age>time(NULL)) && (time(NULL)-tar_cache->timestamp<header->max_age)){
        
              reorder_cache(URL);
              /* fetch the respond message from cache to the client and add "Age" field*/
              long total_time = time(NULL)-tar_cache->timestamp;
              char *buffer=(char*)malloc(200);
              char *age=(char*)malloc(300);
              bzero(age, 300);
              bzero(buffer, 200);
              sprintf (buffer,"%lu",total_time);

              strcat(age,ageStr);
              strcat(age,buffer);
              strcat(age,"\r\n");
              
              char *subrespond;
              subrespond = strstr(tar_cache->data, "\r");
              subrespond = subrespond+2;
              char *temp = tar_cache->data; //initial point
              char *respond=(char*)malloc(Res_BUFSIZE);
              int first_row_len= subrespond-tar_cache->data;

              memcpy(respond,tar_cache->data,first_row_len);
              memcpy(respond+first_row_len,age,strlen(age));
              memcpy(respond+first_row_len+strlen(age),subrespond,tar_cache->respond_mess_len-first_row_len);

              n3 = write(client_connfd, respond, tar_cache->respond_mess_len+strlen(age));
              printf("Message len from the cache: %d bytes\n",n3);
              tar_cache->access_counts++;
              if (n3 < 0) 
                error("ERROR writing to socket");
              printf("Finish reading respond message from cache!\n");
              printf("Current used cache counts: %d\n", cache_count);
              close(client_connfd);
              free(age);
              free(buffer);
              free(buf);
              continue; //break current loop and begin with next loop
        }//end if
   }//end if

  /* start establishing proxy client */
  int server_sockfd;
  struct sockaddr_in serveraddr2;
  struct hostent *server2;
  char *hostname2;

  /* socket: create the socket for server*/
  server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_sockfd < 0) 
      error("ERROR opening socket");

  /* gethostbyname: get the server's DNS entry */
  server2 = gethostbyname(req_hostname);
  if (server2 == NULL) {
      fprintf(stderr,"ERROR, no such host as %s\n", req_hostname);
      exit(0);
  }
  free(req_hostname);

  /* build the server's Internet address */
  bzero((char *) &serveraddr2, sizeof(serveraddr2));
  serveraddr2.sin_family = AF_INET;
  bcopy((char *)server2->h_addr_list[0], 
  (char *)&serveraddr2.sin_addr.s_addr, server2->h_length);
  serveraddr2.sin_port = htons(port);

  /* connect: create a connection with the des server */
  if (connect(server_sockfd, &serveraddr2, sizeof(serveraddr2)) < 0) 
    error("ERROR connecting");

  /* send the message line to the des server */
  n1 = write(server_sockfd, buf, n);
  if (n1 < 0) 
    error("ERROR writing to socket");
  
  /* print the des server's reply */
  char *buf2 = (char *) malloc(Res_BUFSIZE);
  bzero(buf2, Res_BUFSIZE);

  n2 = read(server_sockfd, buf2, Res_BUFSIZE);
  if (n2 < 0)error("ERROR reading from socket");
  
  header = extract_res_headers(buf2);
  printf("respond message: content length:%d, max-age:%d\n",header->content_len, header->max_age);

  char*temp_p=buf2;
  int count=0;
  
  while(n2<=header->content_len){
  temp = read(server_sockfd, buf2+n2, Res_BUFSIZE);
  if(temp==0){break;}
  n2=n2+temp;
  }

  printf("\nProxy client read received %d bytes Respond from dest server.\n", n2);
  
  char *buf2_subti=buf2;
  write_to_cache(URL, buf2_subti, n2);

  /* send the respond message to the client */
  n3 = write(client_connfd, buf2, n2);
  if (n3 < 0) 
    error("ERROR writing to socket");
  
  printf("Current used cache counts: %d\n", cache_count);
  
  close(server_sockfd);
  close(client_connfd);
  free(buf);
  free(buf2);
  free(URL);
  }
}