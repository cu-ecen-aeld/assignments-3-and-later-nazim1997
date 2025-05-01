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
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/queue.h>

#define MAX 80
#define PORT 9000

volatile sig_atomic_t terminate_flag = false;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int aesdsocketdata_fd = -1;

struct thread_node {
    pthread_t thread;     // Store thread ID here
    SLIST_ENTRY(thread_node) entries;  // Macro for list linkage
};
SLIST_HEAD(thread_list, thread_node) head;  // Define list head type

static void signal_handler(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        terminate_flag = true;
    }
}

void* handle_client(void* args) {
    int connfd = *((int *) args);
    free(args);
    char buffer[MAX];
    int bytes_read;
    
    printf("accepted connection from thread %d\n", gettid());
    
    while ((bytes_read = read(connfd, buffer, sizeof(buffer))) > 0) {
        pthread_mutex_lock(&mutex);
        write(aesdsocketdata_fd, buffer, bytes_read);
        
        // Check for newline to determine packet completion
        if (memchr(buffer, '\n', bytes_read) != NULL) {
            // Send back the complete file content
            lseek(aesdsocketdata_fd, 0, SEEK_SET);
            while ((bytes_read = read(aesdsocketdata_fd, buffer, sizeof(buffer))) > 0) {
                if (write(connfd, buffer, bytes_read) < 0) {
                    perror("writing to file failed");
                    break;
                }
            }
        }
        pthread_mutex_unlock(&mutex);
    }
    

    close(aesdsocketdata_fd);
    close(connfd);
    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    int sockfd;
    struct sockaddr_in server_address, client_address;
    socklen_t client_len;
    struct sigaction sa;
    int daemon_mode = 0;
    int c;
    pthread_t thread;
    SLIST_INIT(&head);

    // Set up signal handling
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    aesdsocketdata_fd = open("/var/tmp/aesdsocketdata", O_RDWR | O_CREAT | O_APPEND, 0644);
    
    if (aesdsocketdata_fd == -1) {
        perror("open failed");
        close(aesdsocketdata_fd);
        exit(EXIT_FAILURE);
    }

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set up server address
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(PORT);

    // Bind socket
    if (bind(sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(sockfd, 5) == -1) {
        perror("listen failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    
    while ((c = getopt(argc, argv, "d")) != -1) {
      if (c == 'd') daemon_mode = 1;
    }

    if (daemon_mode == 1) {
      int pid = fork();

      if (pid < 0) {
        perror("fork failed\n");
        exit(EXIT_FAILURE);
      }
      else if (pid == 0) {
        printf("starting daemon process\n");
        setsid();
        close(STDIN_FILENO);
      }
      else {
        exit(0);
      }
    }

    // Main server loop
    while (!terminate_flag) {
        client_len = sizeof(client_address);
        int* connfd = (int*) malloc(sizeof(int));
        *connfd = accept(sockfd, (struct sockaddr *)&client_address, &client_len);
        
        if (*connfd < 0) {
            free(connfd);
            if (terminate_flag) break;
            perror("accept failed");
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_address.sin_addr));
        struct thread_node* new_node = malloc(sizeof(struct thread_node));
        
        if (pthread_create(&new_node->thread, NULL, handle_client, (void *)connfd) < 0) {
            perror("thread creation failed");
            free(connfd);
            free(new_node);
        }
        pthread_mutex_lock(&mutex);
        SLIST_INSERT_HEAD(&head, new_node, entries);
        pthread_mutex_unlock(&mutex);   
    }

    // Cleanup
    close(sockfd);

    // Signal all threads to exit
    struct thread_node *node;
    while (!SLIST_EMPTY(&head)) {
        node = SLIST_FIRST(&head);
        SLIST_REMOVE_HEAD(&head, entries);
        pthread_join(node->thread, NULL);
        free(node);
    }

    if (aesdsocketdata_fd != -1) {
        close(aesdsocketdata_fd);
    }
    unlink("/var/tmp/aesdsocketdata");
    syslog(LOG_INFO, "Caught signal, exiting");
    
    return 0;
}
