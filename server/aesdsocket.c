#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define MAX 80 

void listening(int fd) {
  char buffer[MAX];

  int aesdsocketdata_fd = open("/var/tmp/aesdsocketdata", O_RDWR | O_CREAT | O_APPEND);

  for (;;) {
    bzero(buffer, MAX);

    while(read(fd, buffer, sizeof(buffer)) > 0) {

        write(aesdsocketdata_fd, buffer, sizeof(buffer));

        write(fd, buffer, sizeof(buffer));
    }
  }
}

int main() {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);

  if (sockfd == -1) {
    perror("socket creation failed...\n");
    exit(1);
  } 

  struct sockaddr_in server_address, client_address;
  memset(&server_address, 0, sizeof(server_address));

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(9000);
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(sockfd, (struct sockaddr* )&server_address, sizeof(server_address)) == -1) {
    perror("binding failed\n");
    exit(1);
  }

  if (listen(sockfd, 5) == -1) {
    perror("listening failed\n");
    exit(1);
  }

  socklen_t client_address_length = sizeof(client_address);

  int connfd = accept(sockfd, (struct sockaddr*)&client_address, &client_address_length);

  if (connfd < 0) {
      perror("server accepted failed");
  }
  else {
    syslog(LOG_INFO, "Accepted connection from %d", client_address.sin_addr.s_addr);
  }

  listening(connfd);

  return 0;
}
