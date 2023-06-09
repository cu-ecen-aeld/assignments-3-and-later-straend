#include "aesdsocket.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#define LOG_FILE "/var/tmp/aesdsocketdata"
#define PORT "9000"
#define BUFFER_SIZE (1024)

#define FK_DEBUG 
bool got_signal = false;

int sockfd;

static void sigHandler(int sig)
{
  FK_DEBUG("SIGINT\n");
  syslog(LOG_ERR, "Caught signal, exiting");
  
  // close sockets
  shutdown(sockfd, SHUT_RD);
  shutdown(sockfd, SHUT_WR);
  close(sockfd);

  //close(sockfd);
  //shutdown(sockfd, SHUT_RDWR);
  unlink(LOG_FILE);
  got_signal = true;

  _exit(EXIT_FAILURE);
    
}

int main(int argc, char *argv[])
{
    openlog("AESDSOCKET", 0, LOG_USER);
   

    // making socket to listen on
    struct addrinfo hints;
    struct addrinfo *servinfo;

    memset((void *)&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    
    FK_DEBUG("getaddrinfo\n");
    if (0 != getaddrinfo(NULL, PORT, &hints, &servinfo) ) goto ERR_GETADDRINFO;

    FK_DEBUG("creating socket\n");
    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (0 == sockfd) goto ERR_SOCKET;
      
    // Enable reuseaddr
    int on = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)

    FK_DEBUG("binding socket\n");
    int sock = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (0 != sock) goto ERR_BIND;
     
    freeaddrinfo(servinfo);
    if (argc > 1) {
      if (strncmp("-d", argv[1], 2) == 0) {
        printf("start daemon\n");
        
        switch(fork()){
          case -1: 
            printf("Failed at forking\n");
            return -1;
          case 0:
            // We should continue the app
            break;
          default:
            _exit(EXIT_SUCCESS);
        }
      }
    }
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);
    FK_DEBUG("start listening\n");
    if( listen(sockfd, 5) < 0 ) goto ERR_LISTEN;

    int f_log = open(LOG_FILE, O_CREAT | O_TRUNC |O_RDWR|S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (f_log < 0) {
      syslog(LOG_ERR, "OPpenfile failed: %d", f_log);
      goto ERR_FILE_ERROR;
    }
    
    while (1){
      int c;
      struct sockaddr_in client_ca;
      int len_client_ca = sizeof(struct sockaddr_in);
      if ( (c = accept(sockfd, (struct sockaddr *) &client_ca, &len_client_ca)) < 0) {
        // failed accepting socket
        FK_DEBUG("Timed out accepting: %d\n", errno);
        
        return 5;
      }
      char *client_ip = inet_ntoa(client_ca.sin_addr);
      syslog(LOG_DAEMON, "Accepted connection from %s", client_ip);
      
      unsigned long totalbytes=0;
      // read messages separated by \n until \0 is received
      while (1) {
        char *buffer = malloc(BUFFER_SIZE);
        if (NULL == buffer) goto ERR_BUFFER_ALLOCATION;
        
        int bytes_read = recv(c, buffer, BUFFER_SIZE, 0);
        if (bytes_read < 1){
          FK_DEBUG("socket failure: %d\n", errno);
          free(buffer);
          goto RETURN_ERR;
        }
        totalbytes += bytes_read;
        
        // check if message contains \n
        char *nn = strchr(buffer, '\n');
        // if also end of all messages
        FK_DEBUG("Got %d bytes\n", bytes_read);
        
        if (NULL != nn ) {
          int to_write = (nn-buffer);
          int written = write(f_log, buffer, to_write);
          if (written < 0){
            FK_DEBUG("\tfailed: %d\n", errno);
          }
          FK_DEBUG("\tComplete message len: %d wrote: %d\n", to_write, written);
          buffer[0] = '\n';
          written = write(f_log, buffer, 1);
          free(buffer);
          break;
        } else {
          FK_DEBUG("No complete message yet, (writing %d to log)\n", bytes_read);
          write(f_log, buffer, bytes_read);
        }
        free(buffer);
        
      }
      FK_DEBUG("Send data to socket\n");

     
      // getting filesize
      lseek(f_log, 0, SEEK_SET);
      long filesize = lseek(f_log, 0, SEEK_END);
      lseek(f_log, 0, SEEK_SET);
      if (filesize < 0) goto ERR_NOTHING_TO_SEND; 

      char *wrbuffer = (char*) malloc(BUFFER_SIZE);
      if (NULL==wrbuffer) goto ERR_BUFFER_ALLOCATION;
      size_t readbytes=0;
      FK_DEBUG("Sending %ld bytes\n", filesize);
      while(readbytes < filesize) {
        
        size_t this_read = read(f_log, wrbuffer, BUFFER_SIZE);
        if (0==this_read){
          // end of file
          break;
        }
        FK_DEBUG("Sending %ld bytes total: %ld/%ld\n", this_read, readbytes+this_read, filesize);
        int r = send(c, (void *)wrbuffer, this_read, 0);
        readbytes += this_read;
      }
      free(wrbuffer);

      close(c);
      syslog(LOG_DAEMON, "Closed connection from %s", client_ip);
      unlink(LOG_FILE);

    }
    shutdown(sockfd, SHUT_RD);
    shutdown(sockfd, SHUT_WR);
    close(sockfd);
    closelog();
    return 0;
      
  ERR_GETADDRINFO:
    syslog(LOG_ERR, "getaddrinfo failed");
    goto RETURN_ERR;
  ERR_SOCKET:
    freeaddrinfo(servinfo);
    syslog(LOG_ERR, "Error creating socket");
    goto RETURN_ERR;
  ERR_BIND:
    freeaddrinfo(servinfo);
    syslog(LOG_ERR, "Error binding to address");
    goto RETURN_ERR;
    
  ERR_LISTEN:
    close(sockfd);
    syslog(LOG_ERR, "Error listening");
    FK_DEBUG("Cold not listen\n");
    goto RETURN_ERR;
  
  ERR_BUFFER_ALLOCATION:
    close(sockfd);
    syslog(LOG_ERR, "Error allocating buffer");
    FK_DEBUG("BUFFER\n");
    close(f_log);
    
    goto RETURN_ERR;
  
  ERR_NOTHING_TO_SEND:
    close(sockfd);
    syslog(LOG_ERR, "No data received to send");
    close(f_log);
    goto RETURN_ERR;
  
  ERR_NO_ARGUMENTS:
    syslog(LOG_ERR, "No arguments specified");
    goto RETURN_ERR;
  
  ERR_FILE_ERROR:
    close(sockfd);
    syslog(LOG_ERR, "Error opening file: %d", errno);
    FK_DEBUG("Error opening file: %d\n", errno);
    goto RETURN_ERR;
    
  ERR_FILE_WRITE:
    close(sockfd);
    syslog(LOG_ERR, "Error writing to file: %d", errno);
    goto RETURN_ERR;


  RETURN_ERR:
    closelog();
    return -1;
  
}