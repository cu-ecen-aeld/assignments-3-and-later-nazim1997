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
#define SIZE 50
#define TIMESTAMP_INTERVAL 10

volatile sig_atomic_t terminate_flag = false;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int aesdsocketdata_fd = -1;

struct thread_node {
    pthread_t thread;     // Store thread ID here
    int complete;         // Flag to mark thread completion
    SLIST_ENTRY(thread_node) entries;  // Macro for list linkage
};
SLIST_HEAD(thread_list, thread_node) head;  // Define list head type

static void signal_handler(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        terminate_flag = true;
    }
}

void* timestamp_thread(void* args) {
    time_t t;
    struct tm *tmp;
    char time_buffer[SIZE];
    char output_buffer[SIZE + 20]; // For "timestamp:" prefix and newline
    
    while(!terminate_flag) {
        time(&t);
        tmp = localtime(&t);
        
        // Format according to RFC 2822
        strftime(time_buffer, sizeof(time_buffer), "%a, %d %b %Y %T %z", tmp);
        
        // Create timestamp message
        snprintf(output_buffer, sizeof(output_buffer), "timestamp:%s\n", time_buffer);
        
        // Write timestamp to file with mutex protection
        pthread_mutex_lock(&mutex);
        if (write(aesdsocketdata_fd, output_buffer, strlen(output_buffer)) < 0) {
            perror("Error writing timestamp");
        }
        pthread_mutex_unlock(&mutex);
        
        // Sleep for 10 seconds
        sleep(TIMESTAMP_INTERVAL);
    }
    
    return NULL;
}

void* handle_client(void* args) {
    int connfd = *((int *) args);
    free(args);
    char buffer[MAX];
    int bytes_read;
    bool packet_complete = false;
    
    while ((bytes_read = read(connfd, buffer, sizeof(buffer) - 1)) > 0) {
        // Check for newline to determine packet completion
        for (int i = 0; i < bytes_read; i++) {
            if (buffer[i] == '\n') {
                packet_complete = true;
                break;
            }
        }
        
        // Write to file with mutex protection
        pthread_mutex_lock(&mutex);
        if (write(aesdsocketdata_fd, buffer, bytes_read) < 0) {
            perror("writing to file failed");
            pthread_mutex_unlock(&mutex);
            break;
        }
        
        // If a complete packet has been received, send the entire file back
        if (packet_complete) {
            // Reposition to start of file
            if (lseek(aesdsocketdata_fd, 0, SEEK_SET) < 0) {
                perror("lseek failed");
                pthread_mutex_unlock(&mutex);
                break;
            }
            
            // Read and send entire file
            int read_bytes;
            while ((read_bytes = read(aesdsocketdata_fd, buffer, sizeof(buffer) - 1)) > 0) {
                if (write(connfd, buffer, read_bytes) < 0) {
                    perror("writing to socket failed");
                    break;
                }
            }
            
            // Return to end of file for next writes
            if (lseek(aesdsocketdata_fd, 0, SEEK_END) < 0) {
                perror("lseek failed");
                pthread_mutex_unlock(&mutex);
                break;
            }
            
            packet_complete = false;
        }
        
        pthread_mutex_unlock(&mutex);
    }
    
    if (bytes_read < 0) {
        perror("read failed");
    }
    
    close(connfd);
    return NULL;
}

int main(int argc, char **argv) {
    int sockfd;
    struct sockaddr_in server_address, client_address;
    socklen_t client_len;
    struct sigaction sa;
    int daemon_mode = 0;
    int c;
    pthread_t timestamp_thread_id;
    
    // Initialize the singly linked list
    SLIST_INIT(&head);

    // Set up signal handling
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Check for daemon mode
    while ((c = getopt(argc, argv, "d")) != -1) {
        if (c == 'd') daemon_mode = 1;
    }

    // Open the data file
    aesdsocketdata_fd = open("/var/tmp/aesdsocketdata", O_RDWR | O_CREAT | O_APPEND | O_TRUNC, 0644);
    if (aesdsocketdata_fd == -1) {
        perror("open failed");
        exit(EXIT_FAILURE);
    }

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket creation failed");
        close(aesdsocketdata_fd);
        exit(EXIT_FAILURE);
    }

    // Enable address reuse
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(sockfd);
        close(aesdsocketdata_fd);
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
        close(aesdsocketdata_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(sockfd, 5) == -1) {
        perror("listen failed");
        close(sockfd);
        close(aesdsocketdata_fd);
        exit(EXIT_FAILURE);
    }

    // Daemonize if requested
    if (daemon_mode) {
        int pid = fork();
        if (pid < 0) {
            perror("fork failed");
            close(sockfd);
            close(aesdsocketdata_fd);
            exit(EXIT_FAILURE);
        }
        else if (pid > 0) {
            // Parent process exits
            exit(EXIT_SUCCESS);
        }
        
        // Child process continues
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    // Start timestamp thread
    if (pthread_create(&timestamp_thread_id, NULL, timestamp_thread, NULL) != 0) {
        perror("Timestamp thread creation failed");
        close(sockfd);
        close(aesdsocketdata_fd);
        exit(EXIT_FAILURE);
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
        
        // Create new thread node
        struct thread_node* new_node = malloc(sizeof(struct thread_node));
        if (new_node == NULL) {
            perror("Failed to allocate thread node");
            free(connfd);
            continue;
        }
        
        new_node->complete = 0;
        
        // Create thread to handle client
        if (pthread_create(&new_node->thread, NULL, handle_client, (void *)connfd) != 0) {
            perror("Thread creation failed");
            free(connfd);
            free(new_node);
            continue;
        }
        
        // Add thread to list
        pthread_mutex_lock(&mutex);
        SLIST_INSERT_HEAD(&head, new_node, entries);
        pthread_mutex_unlock(&mutex);
        
        // Clean up completed threads
        pthread_mutex_lock(&mutex);
        struct thread_node *node = SLIST_FIRST(&head);
        struct thread_node *prev = NULL;
        
        while (node != NULL) {
            struct thread_node *next = SLIST_NEXT(node, entries);
            
            if (node->complete) {
                // Remove from list
                if (prev == NULL) {
                    SLIST_REMOVE_HEAD(&head, entries);
                } else {
                    SLIST_NEXT(prev, entries) = SLIST_NEXT(node, entries);
                }
                pthread_join(node->thread, NULL);
                free(node);
            } else {
                prev = node;
            }
            
            node = next;
        }
        pthread_mutex_unlock(&mutex);
    }

    // Cleanup when exiting
    close(sockfd);

    // Join timestamp thread
    pthread_join(timestamp_thread_id, NULL);

    // Join all client threads
    struct thread_node *node;
    while (!SLIST_EMPTY(&head)) {
        node = SLIST_FIRST(&head);
        SLIST_REMOVE_HEAD(&head, entries);
        pthread_join(node->thread, NULL);
        free(node);
    }

    // Close and remove data file
    close(aesdsocketdata_fd);
    unlink("/var/tmp/aesdsocketdata");
    
    syslog(LOG_INFO, "Caught signal, exiting");
    return EXIT_SUCCESS;
}