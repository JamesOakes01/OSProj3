#include "io_helper.h"
#include "request.h"
#include <string.h>
#include <stdio.h>

#define MAXBUF (8192)

//added to fix bugs:
int buffer_max_size = DEFAULT_BUFFER_SIZE;
int buffer_size = 0;
int scheduling_algo = DEFAULT_SCHED_ALGO;
int num_threads = DEFAULT_THREADS;

pthread_mutex_t requestBufferLock = PTHREAD_MUTEX_INITIALIZER;
int currentRequestBufferSize = 0;
pthread_cond_t bufferEmpty = PTHREAD_COND_INITIALIZER; //for children
pthread_cond_t bufferFull = PTHREAD_COND_INITIALIZER; //for parent

typedef struct{
  int fd;
  char filename[MAXBUF]; //or hardcode 256
  int size;
} Request;

Request requestBuffer[10]; //requests buffer array


//
//	TODO: add code to create and manage the buffer // mostly done already
//

//
// Sends out HTTP response in case of errors
//
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    
    // Create the body of error message first (have to know its length for header)
    sprintf(body, ""
	    "<!doctype html>\r\n"
	    "<head>\r\n"
	    "  <title>CYB-3053 WebServer Error</title>\r\n"
	    "</head>\r\n"
	    "<body>\r\n"
	    "  <h2>%s: %s</h2>\r\n" 
	    "  <p>%s: %s</p>\r\n"
	    "</body>\r\n"
	    "</html>\r\n", errnum, shortmsg, longmsg, cause);
    
    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    
    // Write out the body last
    write_or_die(fd, body, strlen(body));
    
    // close the socket connection
    close_or_die(fd);
}

//
// Reads and discards everything up to an empty text line
//
void request_read_headers(int fd) {
    char buf[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
		readline_or_die(fd, buf, MAXBUF);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content (executable file)
// Calculates filename (and cgiargs, for dynamic) from uri
//
int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    
    if (!strstr(uri, "cgi")) { 
	// static
	strcpy(cgiargs, "");
	sprintf(filename, ".%s", uri);
	if (uri[strlen(uri)-1] == '/') {
	    strcat(filename, "index.html");
	}
	return 1;
    } else { 
	// dynamic
	ptr = index(uri, '?');
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	} else {
	    strcpy(cgiargs, "");
	}
	sprintf(filename, ".%s", uri);
	return 0;
    }
}

//
// Fills in the filetype given the filename
//
void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) 
		strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) 
		strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) 
		strcpy(filetype, "image/jpeg");
    else 
		strcpy(filetype, "text/plain");
}

//
// Handles requests for static content
//
void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    
    // Rather than call read() to read the file into memory, 
    // which would require that we allocate a buffer, we memory-map the file
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    
    // put together response
    sprintf(buf, ""
	    "HTTP/1.0 200 OK\r\n"
	    "Server: OSTEP WebServer\r\n"
	    "Content-Length: %d\r\n"
	    "Content-Type: %s\r\n\r\n", 
	    filesize, filetype);
       
    write_or_die(fd, buf, strlen(buf));
    
    //  Writes out to the client socket the memory-mapped file 
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}


//function to find the size of a file. This code function is from GeeksForGeeks.com. https://www.geeksforgeeks.org/c-program-find-size-file/
long int findSize(char file_name[]) 
{ 
    // opening the file in read mode 
    FILE* fp = fopen(file_name, "r"); 
  
    // checking if the file exist or not 
    if (fp == NULL) { 
        printf("File Not Found!\n"); 
        return -1; 
    } 
  
    fseek(fp, 0L, SEEK_END); 
  
    // calculating the size of the file 
    long int res = ftell(fp); 
  
    // closing the file 
    fclose(fp); 
  
    return res; 
} 

//get index of request to serve based on fifo scheduler
int fifo(){ 
  return 0;
}

//get index of request to serve based on random scheduler
int random_schedule(){ 
  //random number between 0 and current buffersize
  return (rand() % currentRequestBufferSize);
}

//smallest file first
int SmallestFirst(){
  int currentSmallestSize = findSize(requestBuffer[0].filename);
  int indexOfSmallestSize = 0;
  for (int i = 1; i < currentRequestBufferSize; i++){
    if ((findSize(requestBuffer[i].filename)) < currentSmallestSize){
      indexOfSmallestSize = i;
      currentSmallestSize = findSize(requestBuffer[i].filename);
    }
  }
  return indexOfSmallestSize;
}

//
// Fetches the requests from the buffer and handles them (thread logic)
//
void* thread_request_serve_static(void* arg)
{
	// TODO: write code to actualy respond to HTTP requests
  //remove request from buffer. lock, check the condition variable to make sure it isn't empty, then remove, then decrement buffer size vairable. signal on full. unlock
  //use different functions for fifo, random, and smallest first

  //locks the buffer
  pthread_mutex_lock(&requestBufferLock);

  //check condition variable before proceeding
  while (currentRequestBufferSize == 0){
    //buffer is empty
    printf("requestBuffer is empty\n");
    pthread_cond_wait(&bufferEmpty, &requestBufferLock);
  }

  int index = -1;
  switch (scheduling_algo)
  {
  case 0:
    index = fifo();
    break;
  case 1:
    index = SmallestFirst();
    break;
  case 2:
    index = random();
    break;
  default:
    printf("invalid value in switch case");
    break;
  }
  
  //sets the newRequest to the request in the buffer from the scheudling policies
  Request newRequest = requestBuffer[index];

  //removes request from buffer and logic to shift buffer so there are no gaps. Used some online help here
  for (int i = index; i < currentRequestBufferSize - 1; i++) { 
    requestBuffer[i] = requestBuffer[i + 1];
  }
  //buffersize decrement
  currentRequestBufferSize--;

  //child wait on buffer empty and signal on buffer full?????    <-this might be reversed
  //variable condition signal
  pthread_cond_signal(&bufferFull);

  //unlock
  pthread_mutex_unlock(&requestBufferLock);
  request_serve_static(newRequest.fd, newRequest.filename, newRequest.size);
  return NULL;
}

//
// Initial handling of the request
//
void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    
	// get the request type, file path and HTTP version
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

	// verify if the request type is GET or not
    if (strcasecmp(method, "GET")) {
		request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
		return;
    }
    request_read_headers(fd);
    
	// check requested content type (static/dynamic)
    is_static = request_parse_uri(uri, filename, cgiargs);
    
	// get some data regarding the requested file, also check if requested file is present on server
    if (stat(filename, &sbuf) < 0) {
		request_error(fd, filename, "404", "Not found", "server could not find this file");
		return;
    }
    
	// verify if requested content is static
    if (is_static) {
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
			request_error(fd, filename, "403", "Forbidden", "server could not read this file");
			return;
		}
		
		// TODO: write code to add HTTP requests in the buffersed on the scheduling policy
    //Request newRequest = {fd, filename, sbuf.st_size}; removing based to fix errors

    //AI Recommended bug fix. Apparently C does not automatically copy in the way I was expecting.
    Request newRequest;
    newRequest.fd = fd;
    strncpy(newRequest.filename, filename, sizeof(newRequest.filename) - 1);
    newRequest.filename[sizeof(newRequest.filename) - 1] = '\0';  // ensure null-termination
    newRequest.size = sbuf.st_size;


    //bufferRequestLock = //lock the buffer if the buffer is locked then wait or spin or something otherwise lock
    pthread_mutex_lock(&requestBufferLock);
      //the bufferRequestLock wasn't locked so now it is
      
      //check to see if buffer is fullsd
      while (currentRequestBufferSize >= 10){
        //buffer is full
        printf("requestBuffer is full\n");
        pthread_cond_wait(&bufferFull, &requestBufferLock);
      }
        //buffer is not full, proceed
        //put request into the buffer at current size
        requestBuffer[currentRequestBufferSize] = newRequest;
        //increment the size
        currentRequestBufferSize++;
        pthread_cond_signal(&bufferEmpty);
      pthread_mutex_unlock(&requestBufferLock); //unlock buffer
    }
  
    


    // Directory Traversal mitigation
    char cwdbuff[256];
    getcwd(cwdbuff, sizeof(cwdbuff));
    if (strstr(filename, "..") != NULL) { /*the request contains a file path outside of the current webserver director (current working directory)*/
      request_error(fd, filename, "403", "Invalid Path", "The requested file path contains an illegal character.");
    } else {
		request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}
