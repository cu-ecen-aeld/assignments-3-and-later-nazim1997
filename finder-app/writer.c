#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h> 
#include <unistd.h>


int main(int argc, char *argv[])
{
    int fileFd;
    size_t numRead;

    openlog(NULL, 0, LOG_USER);

    if (argc != 3 || strcmp(argv[1], "--help") == 0) {
        printf("you should provide two arguments\n");
        syslog(LOG_ERR, "you should provide two arguments");
        closelog();
        return 1;
    }

    fileFd = open(argv[1], O_WRONLY | O_CREAT, 0644);

    if (fileFd == -1) {
        printf("error opening file %s\n", argv[1]);
        syslog(LOG_ERR, "error opening file %s", argv[1]);
        closelog();
        return 1;
    }

    numRead = write(fileFd, argv[2], strlen(argv[2]));

    if(numRead < 0) {
        printf("error writing to file %s\n", argv[1]);
        syslog(LOG_ERR, "error writing to file %s", argv[1]);
        close(fileFd);
        closelog();
        return 1;
    }

    syslog(LOG_DEBUG ,  "Writing %s to %s", argv[2], argv[1]);

    close(fileFd);
    return 0;
}
