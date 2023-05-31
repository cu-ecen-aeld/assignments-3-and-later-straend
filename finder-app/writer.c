#include <stdint.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

int main(int argc, char *argv[])
{
    openlog("WRITER", 0, LOG_USER);

    if (argc != 3) {
        goto ERR_NO_ARGUMENTS;
    }
    
    FILE *f;
    f = fopen(argv[1], "w");
    if (NULL == f) goto ERR_FILE_ERROR;
    
    syslog(LOG_DEBUG, "Writing %s to %s\n", argv[2], argv[1]);
    size_t written = fwrite(argv[2], sizeof(char), strlen(argv[2]), f);
    fclose(f);

    if (written != strlen(argv[2])) goto ERR_FILE_WRITE;

    closelog();
    return 0;

  
  ERR_NO_ARGUMENTS:
    syslog(LOG_ERR, "No arguments specified");
    goto RETURN_ERR;
  
  ERR_FILE_ERROR:
    syslog(LOG_ERR, "Error opening file: %d", errno);
    goto RETURN_ERR;
    
  ERR_FILE_WRITE:
    syslog(LOG_ERR, "Error writing to file: %d", errno);
    goto RETURN_ERR;
  
  RETURN_ERR:
    closelog();
    return 1;
  
}