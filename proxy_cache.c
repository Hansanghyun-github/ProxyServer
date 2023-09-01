//////////////////////////////////////////////////////////////////////
// File Name    : proxy_cache.c                                     //
// OS           : Ubuntu 16.04 LTS 64bits                           //
// ------------------------------------------------------           //
// Title : System Programming Assignment (proxy server)             //
// Description  : connect to web browser                            //
//		  recieve http request                                      //
//		  extract URL by HTTP request and check HIT/MISS            //
//		  report at logfile, in critical zone                       //
//		  if MISS, send http request to web server                  //
//			   if timeout, print "no response"                      //
//			   else, recieve http response                          //
//				 and store at hash file                             //
//		  send http response to web browser at hash file            //
//////////////////////////////////////////////////////////////////////

#include<stdio.h>
#include<string.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<unistd.h>
#include<stdlib.h>
#include<signal.h>
#include<sys/wait.h>
#include<dirent.h>
#include<pwd.h>
#include<fcntl.h>
#include<time.h>
#include<openssl/sha.h>
#include<stdbool.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<stdlib.h>
#include<sys/ipc.h>
#include<sys/sem.h>
#include<pthread.h>

#define BUFFSIZE    1024
#define PORTNO	    39999

/////////////////////////////////////// global variable //////////////
int client_fd = 0;
int child_process_socket_fd = 0;

// cache file descriptor, logfile descriptor
int cfd = 0, lfd = 0; 

// cache Directory Path, logfile Path
char cache_dir_path[BUFFSIZE] = "", logfile_path[BUFFSIZE] = "";

char hash_path[BUFFSIZE] = "";

char url[BUFFSIZE] = "", hashed_url[BUFFSIZE] = "";

char hashed_url_dir_name[4] = "", hashed_url_file_name[BUFFSIZE] = "";

bool ` = true;		// true = HIT false = MISS
bool flag_GET = true;			// true = GET false = not GET

char buf[BUFFSIZE]  = "";		// HTTP request
char host_name[BUFFSIZE] = "";

int count_child_process = 0;		// number of child process
time_t t, start_time;

union semum{
    int val;
    struct semid_ds *buf;
    unsigned int *array;
} arg;
//////////////////////////////////////////////////////////////////////


void make_cache_dir_and_logfile();
char *sha1_hash(char *input_url, char *hashed_url);
static void handler(int signo);
char *getIPAddr(char *addr);

void get_url_and_host_name();
void check_HIT_or_MISS();
void *record_at_logfile();
void HTTP_response();

int initialize_sem();
void P(int semid);
void V(int semid);

//////////////////////////////////////////////////////////////////////
// main								                                //
// ================================================================ //
// Input  : no input						                        //
// Output : 0 success						                        //
// Purpose: connect to web browser				                    //
//	    if recieve http request				                        //
//	    make child process					                        //
//	    In parent process, wait to recieve http request	            //
//	    In child  process, perform 2-3 assignment		            //
//////////////////////////////////////////////////////////////////////
int main(){
    struct sockaddr_in server_addr, client_addr;
    int socket_fd;
    int len;
    pid_t pid;
    int semid; // semaphore ID

    umask(0000);

    // make cache dir, cachepath and logfilepath
    make_cache_dir_and_logfile();

    semid = initialize_sem();			    // initialize semaphore

    // make socket
    int opt = 1;
    socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(PORTNO);

    // allocate ip number and port number
    bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));

    listen(socket_fd, 5);

    signal(SIGCHLD, (void *)handler);		    // signal handling to child process
    signal(SIGINT,  (void *)handler);		    // signal handling to input '^C'

    // counting program excuted time
    time(&start_time);

    while(1){
        bzero((char*)&client_addr, sizeof(client_addr));
        len=sizeof(client_addr);

        // recieve client's connection
        client_fd=accept(socket_fd, (struct sockaddr*)&client_addr, &len);
        if(client_fd < 0){
            printf("Server : accept failed  %d\n",getpid());
            close(socket_fd);
            return 0;
        }
        
        pid=fork();				    // make child process
        count_child_process++;

        if(pid == -1){
            printf("Fork error\n");
            close(client_fd);
            close(socket_fd);
            return 0;
        }
        if(pid == 0){ // child process

            read(client_fd, buf, sizeof(buf));	    // receive HTTP request

            get_url_and_host_name();


            if(flag_GET == true){

            check_HIT_or_MISS();		    // check HIT or MISS

            P(semid);			    // access critical zone

            int err;
            void *tret;
            pthread_t tid;

            // record HIT/MISS information at logfile, at thread
            err = pthread_create(&tid, NULL, record_at_logfile, NULL);

            printf("*PID# %d create the *TID# %d\n", (int)getpid(), (int)tid);

            pthread_join(tid, &tret);	    // wait thread

            printf("*TID# %d is exited\n", (int)tid);

            V(semid);			    // exit critical zone

            HTTP_response();		    // send HTTP request to web server
                                // and recieve HTTP response 
            }

            close(client_fd);
            exit(0);
        }
        close(client_fd); // disconnect to client in parent proces
    }
    close(socket_fd);
    if(semctl(semid, 0, IPC_RMID, arg) == -1){	    // remove semaphore
	    perror("semctl failed");
    }

    return 0;
}


//////////////////////////////////////////////////////////////////////
// make_cache_dir_and_logfile					                    //
// ================================================================ //
// Input  : no input						                        //
// Output : no output						                        //
// Purpose: make cache directory and logfile.txt		            //
//	    and store cache path and logfile path		                //
//////////////////////////////////////////////////////////////////////
void make_cache_dir_and_logfile(){
    struct passwd *usr_info = getpwuid(getuid());
    strcpy(cache_dir_path, usr_info->pw_dir);
    strcpy(logfile_path, usr_info->pw_dir);

    // make cache directory
    strcat(cache_dir_path, "/cache");
    mkdir(cache_dir_path, 0777);

    // make logfile directory and open logfile.txt
    strcat(logfile_path, "/logfile");
    mkdir(logfile_path, 0777);
    strcat(logfile_path, "/logfile.txt");
    lfd = open(logfile_path, O_RDWR | O_APPEND | O_CREAT, 0777);
    if(lfd < 0){
	    printf("can't open log file\n");
	    return;
    }
    close(lfd);
}


//////////////////////////////////////////////////////////////////////
// sha1_hash							                            //
// ================================================================ //
// Input  : url, hashed_url					                        //
// Output : hashed_url						                        //
// Purpose: convert url to hashed_url				                //
//////////////////////////////////////////////////////////////////////
char *sha1_hash(char *input_url, char *hashed_url){
    unsigned char hashed_160bits[20];
    char hashed_hex[41];
    int i;
 
    SHA1(input_url, strlen(input_url), hashed_160bits);

    for(int i=0;i<sizeof(hashed_160bits);i++)
	    sprintf(hashed_hex + i*2, "%02x", hashed_160bits[i]);
    strcpy(hashed_url, hashed_hex);
 
    return hashed_url;
}


//////////////////////////////////////////////////////////////////////
// handler							                                //
// ================================================================ //
// Input  : signo						                            //
// Output : no output						                        //
// Purpose: handling signal					                        //
//	    if signo is SIGCHLD,recieve chlid process's return value    //
//	    if signo is SIGALRM,print "no response"		                //
//				and disconnect to web server	                    //
//	    if signo is SIGINT, report server's information	            //
//				and exit program		                            //
//////////////////////////////////////////////////////////////////////
static void handler(int signo){
    if(signo == SIGCHLD){
        pid_t pid = 0;
        int status;
        while((pid = waitpid(-1, &status, WNOHANG)) > 0);	    // recieve child process's return value
    }
    else if(signo == SIGALRM){
        printf("=====================No Response=======================\n\n");
        close(child_process_socket_fd);				    // disconnect to web server
    }
    else if(signo == SIGINT){
        lfd = open(logfile_path, O_RDWR | O_APPEND);
        if(lfd < 0){
            printf("can't open logfile\n");
            exit(0);
        }
        char log[BUFFSIZE] = "";
        sprintf(log, "**SERVER** [Terminated] run time: %d sec. #sub process: %d\n", (int)(time(&t) - start_time), count_child_process);					// report server's information 
        write(lfd, log, strlen(log));				    // in logfile.txt
        
        close(lfd);
        exit(0);						    // exit program
    }
}


//////////////////////////////////////////////////////////////////////
// checking_HIT_or_MISS						                        //
// ================================================================ //
// Input  : no input						                        //
// Output : no output						                        //
// Purpose: check HIT or MISS					                    //
//	    if MISS, make hash directory and file		                //
//////////////////////////////////////////////////////////////////////
void check_HIT_or_MISS(){
    time_t t;
    struct tm *ltp;
    int i;
    int strlength;

    // change url to hashed_url
    sha1_hash(url, hashed_url);
	
    // make directory name and file name using hashed_url
    strlength = (int)strlen(hashed_url);
    for(i=0; i < 3; i++)
	    hashed_url_dir_name[i] = hashed_url[i];
    for(; i < strlength; i++)
	    hashed_url_file_name[i-3] = hashed_url[i];

    // make hash_path    
    strcat(hash_path, cache_dir_path);
    strcat(hash_path, "/");
    strcat(hash_path, hashed_url_dir_name);
	
    // check HIT or MISS
    if(mkdir(hash_path, 0777) != -1){		// MISS, make hash directory if MISS
	    flag_HIT_or_MISS = false;
	    strcat(hash_path, "/");
        strcat(hash_path, hashed_url_file_name);
	    cfd = creat(hash_path, 0777);
	    close(cfd);
    }
    else{
        strcat(hash_path, "/");
        strcat(hash_path, hashed_url_file_name);
        cfd = open(hash_path, O_RDWR);
        if(cfd == -1){				// MISS
            flag_HIT_or_MISS = false;
            cfd = creat(hash_path, 0777);	// make hash file
            close(cfd);
        }
        else {					// HIT
            flag_HIT_or_MISS = true;
            close(cfd);
        }
    }

    return;
}


//////////////////////////////////////////////////////////////////////
// HTTP_response						                            //
// ================================================================ //
// Input  : no input						                        //
// Output : no output						                        //
// Purpose: if MISS, send HTTP request to web serber		        //
//		     recieve http response			                        //
//		     and store at hash file			                        //
//	    send http response to web browser at hash file	            //
//////////////////////////////////////////////////////////////////////
void HTTP_response(){
    cfd = open(hash_path, O_RDWR);				// open hash file

    char response[BUFFSIZE] = "";

    if(flag_HIT_or_MISS == false){
        struct sockaddr_in server_addr;			// web server address
   
	// make socket 
	child_process_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

	char *IPAddr = getIPAddr(host_name);		// make IP address by host name


	int portnum = 80;					// http port number
	bzero((char*)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr =inet_addr(IPAddr);	// host name's IP address
	server_addr.sin_port = htons(portnum);

	if(connect(child_process_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
	    printf("can't connect\n");
	    return;
	} // connect to web server

	// send http request
	write(child_process_socket_fd, buf, sizeof(buf) + 1);

	signal(SIGALRM, (void *)handler);			// check timeout
    
	alarm(50);

	bzero(response, sizeof(response));

	while(read(child_process_socket_fd, response,sizeof(response)) > 0){ // recieve HTTP response

	    write(cfd, response, sizeof(response));		// store HTTP response at hash file
	    bzero(response, sizeof(response));
	}
	alarm(0);
	close(child_process_socket_fd);
    }

    bzero(response, sizeof(response));
    while(read(cfd, response, sizeof(response)) > 0){		// send HTTP response to web browser
        write(client_fd, response, sizeof(response));		// at hash file
        bzero(response, sizeof(response));
    }
    
    close(cfd);
}

//////////////////////////////////////////////////////////////////////
// getIPAddr							                            //
// ================================================================ //
// Input  : addr						                            //
// Output : haddr						                            //
// Purpose: convert host name to IP address			                //
//////////////////////////////////////////////////////////////////////
char *getIPAddr(char *addr){
    struct hostent* hent;
    char * haddr;
    int len = (int)strlen(addr);

    if((hent = (struct hostent*)gethostbyname(addr)) != NULL){	     // get host name's information
	    haddr = inet_ntoa(*((struct in_addr*)hent->h_addr_list[0])); // get IP address
    }
    return haddr;
}

//////////////////////////////////////////////////////////////////////
// get_url_and_host_name					                        //
// ================================================================ //
// Input  : no input						                        //
// Output : no output						                        //
// Purpose: check GET						                        //
//	    and, get url and host name				                    //
//////////////////////////////////////////////////////////////////////
void get_url_and_host_name(){
    char method[20] = "";
    char *tok;
    char tmp[BUFFSIZE] = "";
    
    strcpy(tmp, buf);
    tok = strtok(tmp, " ");
    strcpy(method, tok);
    if(strcmp(method, "GET") == 0){		    // HTTP method field is GET
        flag_GET = true;
        tok = strtok(NULL, " ");
        int len = strlen(tok);

        for(int i=7;i<len;i++)			    // get url
            url[i-7] = tok[i];
        
        if(url[len-8] == '/')
            url[len-8] = '\0';

        for(int i=11; tmp[i] != '/';i++)	    // get host name
            host_name[i-11] = tmp[i];
    }
    else{					    // HTTP method field is not GET
	    flag_GET = false;
	    return;
    }
}

//////////////////////////////////////////////////////////////////////
// record_at_logfile						                        //
// ================================================================ //
// Input  : no input						                        //
// Output : no output						                        //
// Purpose: record HIT/MISS information at logfile.txt		        //
//	    in critical zone					                        //
//////////////////////////////////////////////////////////////////////
void *record_at_logfile(){
    time_t t;
    struct tm *ltp;
    char log[BUFFSIZE] = "";

    lfd = open(logfile_path, O_RDWR | O_APPEND);

    if(flag_HIT_or_MISS == true){		    // HIT
	    
        time(&t);
        ltp = localtime(&t);

        // record HIT information at logfile.txt
        sprintf(log, "[HIT] %s/%s - [%04d/%d/%d, %02d:%02d:%02d]\n[HIT] %s\n", hashed_url_dir_name, hashed_url_file_name, ltp->tm_year + 1900, ltp->tm_mon + 1, ltp->tm_mday, ltp->tm_hour, ltp->tm_min, ltp->tm_sec, url);
        write(lfd, log, sizeof(log));

    }
    else{					    // MISS

        time(&t);
        ltp = localtime(&t);
	
	    // record MISS information at logfile.txt
        sprintf(log, "[MISS] %s - [%04d/%d/%d, %02d:%02d:%02d]\n", url, ltp->tm_year + 1900, ltp->tm_mon + 1, ltp->tm_mday, ltp->tm_hour, ltp->tm_min, ltp->tm_sec);
        write(lfd, log, sizeof(log));

    }

    close(lfd);
}

//////////////////////////////////////////////////////////////////////
// initialize_sem						                            //
// ================================================================ //
// Input  : no input						                        //
// Output : int - semaphore ID					                    //
// Purpose: initialize semaphore				                    //
//////////////////////////////////////////////////////////////////////
int initialize_sem(){
    int semid, i;

    if((semid = semget((key_t)1234, 1, IPC_CREAT | 0666)) == -1){   // set semaphore
        perror("semget failed");
        exit(2);
    }

    arg.val = 1;
    if(semctl(semid, 0, SETVAL, arg) == -1){			    // control semaphore
        perror("semctl failed");
        exit(2);
    }

    return semid;
}

//////////////////////////////////////////////////////////////////////
// P								                                //
// ================================================================ //
// Input  : int - semaphore ID					                    //
// Output : no output						                        //
// Purpose: access critical zone				                    //
//////////////////////////////////////////////////////////////////////
void P(int semid){
    struct sembuf pbuf;
    pbuf.sem_num = 0;
    pbuf.sem_op = -1;
    pbuf.sem_flg = SEM_UNDO;
    printf("PID# %d is waiting for the semaphore.\n", getpid());
    if(semop(semid, &pbuf, 1) == -1){
        perror("p : semop failed");
        exit(2);
    }
    printf("PID# %d is in the critical zone.\n", getpid());
}

//////////////////////////////////////////////////////////////////////
// V								                                //
// ================================================================ //
// Input  : int - semaphore ID					                    //
// Output : no output						                        //
// Purpose: exit critical zone					                    //
//////////////////////////////////////////////////////////////////////
void V(int semid){
    struct sembuf vbuf;
    vbuf.sem_num = 0;
    vbuf.sem_op = 1;
    vbuf.sem_flg = SEM_UNDO;
    if(semop(semid, &vbuf, 1) == -1){
        perror("v : semop failed");
        exit(2);
    }
    printf("PID# %d exited the critical zone.\n", getpid());
}















