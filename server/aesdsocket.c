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
#include <pthread.h>
#include <stdarg.h>

#include "freebsd_queue.h"

#define LOG_FILE "/var/tmp/aesdsocketdata"
#define PORT "9000"
#define BUFFER_SIZE (1024)

#define DEBUG 0
#define USE_AESD_CHAR_DEVICE 1
#define AESD_CHAR_DEVICE "/dev/aesdchar"

void FK_DEBUG(const char *fmt, ...)
{

  if (DEBUG) {
    va_list args;
    va_start(args, fmt);
    printf(fmt, args);
    va_end(args);
  }
}

bool got_signal = false;
int sockfd;


typedef struct slist_data_s slist_data_t;
struct slist_data_s {
  struct sockaddr_in client_ca;
  int logfile;
  pthread_mutex_t *log_mutex;
  int c;
  //pid_t pid;
  pthread_t pid;
  bool completed;

  SLIST_ENTRY(slist_data_s) entries;
};

slist_data_t *datap=NULL;
SLIST_HEAD(slisthead, slist_data_s) head;

typedef struct timestamper_data_s timestamper_data_t;
struct timestamper_data_s {
  pthread_t pid;
  int logfile;
  pthread_mutex_t *log_mutex;
};


static void sigHandler(int sig)
{
  FK_DEBUG("SIGINT\n");
  syslog(LOG_ERR, "Caught signal, exiting");
  
  // close sockets
  shutdown(sockfd, SHUT_RD);
  shutdown(sockfd, SHUT_WR);
  close(sockfd);

#if !USE_AESD_CHAR_DEVICE
  unlink(LOG_FILE);
#endif

  got_signal = true;
  
  // should free all data in linkedlist and stop all threads
  while (!SLIST_EMPTY(&head)) {
    datap = SLIST_FIRST(&head);
    FK_DEBUG("Removing thread: %lu\n", datap->pid);
    pthread_cancel(datap->pid);

    SLIST_REMOVE_HEAD(&head, entries);
    free(datap);
  }
  _exit(EXIT_FAILURE);
    
}

void *timestamper(void *arg) {
  timestamper_data_t *data = (timestamper_data_t *) arg;
  struct timespec wanted_sleep;
  struct timespec remaining_sleep;
  int wanted_sleep_ms = 10 * 1000;
  char timestring[100];
  time_t t;
  struct tm *tmp;

  while(1){
    wanted_sleep.tv_sec  = wanted_sleep_ms / 1000;
    wanted_sleep.tv_nsec = (wanted_sleep_ms % 1000) * 1000000;
    if (0 != nanosleep(&wanted_sleep, &remaining_sleep)){
      // did not sleep enough
    }
    int res = pthread_mutex_lock(data->log_mutex);
    FK_DEBUG("mutex_lock: %d\n", res);
    
    // should use some error handling here
    t = time(NULL);
    tmp = localtime(&t);
    strftime(timestring, sizeof(timestring), "timestamp:%a, %d %b %Y %T %z\n", tmp);
    write(data->logfile, timestring, strlen(timestring));
    res = pthread_mutex_unlock(data->log_mutex);
    FK_DEBUG("mutex_unlock: %d\n", res);
  }
}

void *connection_thread(void *arg)
{
  slist_data_t *data = (slist_data_t *) arg;
  char *client_ip = inet_ntoa(data->client_ca.sin_addr);
  syslog(LOG_DAEMON, "Accepted connection from %s", client_ip);
    
  unsigned long totalbytes=0;
  // read messages separated by \n until \0 is received
  while (1) {
    char *buffer = malloc(BUFFER_SIZE);
    //if (NULL == buffer) goto ERR_BUFFER_ALLOCATION;
    FK_DEBUG("Waiting for data\n");
    
    // wait for data or 10second timeout
    int bytes_read = recv(data->c, buffer, BUFFER_SIZE, 0);
    if (bytes_read < 1){
      FK_DEBUG("socket failure: %d\n", errno);
      free(buffer);
      data->completed = true;
      return NULL;
    }
    totalbytes += bytes_read;
    
    // check if message contains \n
    char *nn = strchr(buffer, '\n');
    // if also end of all messages
    FK_DEBUG("Got %d bytes\n", bytes_read);
    
    // get mutex here
    int res = pthread_mutex_lock(data->log_mutex);
    FK_DEBUG("mutex_lock: %d\n", res);

    if (NULL != nn ) {
      int to_write = (nn-buffer);
      FK_DEBUG("Writing %d bytes\n", to_write);

      int written = write(data->logfile, buffer, to_write);
      if (written < 0){
        FK_DEBUG("\tfailed: %d\n", errno);
      }
      FK_DEBUG("\tComplete message len: %d wrote: %d\n", to_write, written);
      //buffer[to_write] = '\0';
      //FK_DEBUG("\tmessage '%s'\n", buffer);

      buffer[0] = '\n';
      written = write(data->logfile, buffer, 1);
      free(buffer);
      FK_DEBUG("unlocking mutex\n");
      res=pthread_mutex_unlock(data->log_mutex);
      FK_DEBUG("mutex_unlock: %d\n", res);

      break;
    } else {
      FK_DEBUG("No complete message yet, (writing %d to log)\n", bytes_read);
      write(data->logfile, buffer, bytes_read);
      free(buffer);

      FK_DEBUG("unlocking mutex\n");
      res=pthread_mutex_unlock(data->log_mutex);
      FK_DEBUG("mutex_unlock: %d\n", res);

    }
    //free(buffer);
    // give mutex here
    FK_DEBUG("unlocking mutex\n");
    res=pthread_mutex_unlock(data->log_mutex);
    FK_DEBUG("mutex_unlock: %d\n", res);

  }

  FK_DEBUG("Send data to socket\n");

  // get mutex again
  int res=pthread_mutex_lock(data->log_mutex);
  FK_DEBUG("mutex_lock: %d\n", res);


  // close file if 
#if USE_AESD_CHAR_DEVICE    
  close(data->logfile);
  data->logfile = open(AESD_CHAR_DEVICE, O_RDWR, 0644);
#else
  // getting filesize
  lseek(data->logfile, 0, SEEK_SET);
  long filesize = lseek(data->logfile, 0, SEEK_END);
  lseek(data->logfile, 0, SEEK_SET);
  if (filesize < 0) return NULL; //goto ERR_NOTHING_TO_SEND; 
#endif
 
  char *wrbuffer = (char*) malloc(BUFFER_SIZE);
  if (NULL==wrbuffer) return NULL; //goto ERR_BUFFER_ALLOCATION;
  size_t readbytes=0;
  FK_DEBUG("Sending bytes\n");
  size_t this_read = read(data->logfile, wrbuffer, BUFFER_SIZE);
  while(this_read > 0) {
    FK_DEBUG("Sending %ld bytes total sent: %ld\n", this_read, readbytes+this_read);
    send(data->c, (void *)wrbuffer, this_read, 0);
    readbytes += this_read;
    this_read = read(data->logfile, wrbuffer, BUFFER_SIZE);
  }
  
  free(wrbuffer);
  // give back mutex
  res = pthread_mutex_unlock(data->log_mutex);
  FK_DEBUG("mutex_unlock: %d\n", res);

  close(data->c);
  syslog(LOG_DAEMON, "Closed connection from %s", client_ip);
  data->completed = true;
  pthread_exit(NULL);
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
        FK_DEBUG("start daemon\n");
        
        switch(fork()){
          case -1: 
            FK_DEBUG("Failed at forking\n");
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
    int f_log = 0;



    // Create a mutex for file access
    static pthread_mutex_t m_logfile = PTHREAD_MUTEX_INITIALIZER;

    // Only use timestamper if we write to a file
#if !USE_AESD_CHAR_DEVICE
    f_log = open(LOG_FILE, O_CREAT | O_TRUNC |O_RDWR, 0644);
    if (f_log < 0) {
      syslog(LOG_ERR, "OPpenfile failed: %d", f_log);
      goto ERR_FILE_ERROR;
    }
    // create timestamper
    timestamper_data_t t_data;
    t_data.log_mutex = &m_logfile;
    t_data.logfile = f_log;
    pthread_create(&t_data.pid, NULL, &timestamper, (void*) &t_data);
#endif

    // Linked List for sockets
    SLIST_INIT(&head);
    while (1){
      datap = malloc(sizeof(slist_data_t));

      int len_client_ca = sizeof(struct sockaddr_in);
      if ( (datap->c = accept(sockfd, (struct sockaddr *) &datap->client_ca, (socklen_t *)&len_client_ca)) < 0) {
        // failed accepting socket
        FK_DEBUG("Timed out accepting: %d\n", errno);
        
        return 5;
      }
      #if USE_AESD_CHAR_DEVICE
        if (f_log == 0) {
            f_log = open(AESD_CHAR_DEVICE, O_RDWR, 0644);
            if (f_log < 0) {
              syslog(LOG_ERR, "OPpenfile failed: %d", f_log);
              goto ERR_FILE_ERROR;
            }
        }
      #endif
      datap->log_mutex = &m_logfile;
      datap->logfile = f_log;
      datap->completed = false;
      // do the fork dance here
      // update pid in data, and insert to linked lise
      int rr = pthread_create(&datap->pid, NULL, &connection_thread, (void *) datap);
      FK_DEBUG("rr: %d\n", rr);

      SLIST_INSERT_HEAD(&head, datap, entries);

      SLIST_FOREACH(datap, &head, entries) {
        FK_DEBUG("Thread: %ld\n", (long unsigned int)datap->pid);

        if (datap->completed) {
          FK_DEBUG("Thread: %ld is complete\n", (long unsigned int)datap->pid);
          void *ret = NULL;
          pthread_join(datap->pid, &ret);
          FK_DEBUG("Removing thread: %lu\n", datap->pid);
          //SLIST_REMOVE(datap, entries);
          SLIST_REMOVE(&head, datap, slist_data_s, entries);
          free(datap);
        }
        
      }
    } // while(1)
      
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
  
  ERR_FILE_ERROR:
    close(sockfd);
    syslog(LOG_ERR, "Error opening file: %d", errno);
    FK_DEBUG("Error opening file: %d\n", errno);
    goto RETURN_ERR;
    
  RETURN_ERR:
    closelog();
    return -1;
  
}