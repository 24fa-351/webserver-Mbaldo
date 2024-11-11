#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFFER_SIZE 4096
#define STATIC_DIR "/mnt/c/Users/baldo/Desktop/PyProjects/351/web/static"

// Global stats counters
int total_requests = 0;
size_t total_bytes_received = 0;
size_t total_bytes_sent = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to parse query parameters in /calc request
int parse_query_params(const char *query, int *a, int *b) {
    if (sscanf(query, "a=%d&b=%d", a, b) == 2) {
        return 0; // success
    }
    return -1; // failure
}

// Function to handle the /static endpoint
void handle_static(int client_socket, const char *path) {
    char full_path[BUFFER_SIZE];
    snprintf(full_path, sizeof(full_path), "%s%s", STATIC_DIR, path + strlen("/static"));
    
    int file = open(full_path, O_RDONLY);
    if (file < 0) {
        char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile not found.";
        send(client_socket, not_found_response, strlen(not_found_response), 0);
        return;
    }

    struct stat file_stat;
    fstat(file, &file_stat);

    char header[BUFFER_SIZE];
    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", file_stat.st_size);
    send(client_socket, header, strlen(header), 0);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(file, buffer, sizeof(buffer))) > 0) {
        send(client_socket, buffer, bytes_read, 0);
        pthread_mutex_lock(&stats_mutex);
        total_bytes_sent += bytes_read;
        pthread_mutex_unlock(&stats_mutex);
    }
    close(file);
}

// Function to handle the /stats endpoint
void handle_stats(int client_socket) {
    pthread_mutex_lock(&stats_mutex);
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
             "<html><body><h1>Server Stats</h1><p>Total requests: %d</p>"
             "<p>Total bytes received: %zu</p><p>Total bytes sent: %zu</p></body></html>",
             total_requests, total_bytes_received, total_bytes_sent);
    pthread_mutex_unlock(&stats_mutex);

    send(client_socket, response, strlen(response), 0);
}

// Function to handle the /calc endpoint
void handle_calc(int client_socket, const char *query) {
    int a = 0, b = 0;
    if (parse_query_params(query, &a, &b) == 0) {
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response),
                 "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                 "<html><body><h1>Calculation Result</h1><p>%d + %d = %d</p></body></html>",
                 a, b, a + b);
        send(client_socket, response, strlen(response), 0);
    } else {
        char *bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid parameters.";
        send(client_socket, bad_request, strlen(bad_request), 0);
    }
}

// Function to handle each client connection
void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        pthread_exit(NULL);
    }

    buffer[bytes_received] = '\0';

    // Update stats
    pthread_mutex_lock(&stats_mutex);
    total_requests++;
    total_bytes_received += bytes_received;
    pthread_mutex_unlock(&stats_mutex);

    // Parse the request line
    char method[16], path[256], version[16];
    sscanf(buffer, "%15s %255s %15s", method, path, version);

    // Check HTTP method and path
    if (strcmp(method, "GET") == 0) {
        if (strncmp(path, "/static", 7) == 0) {
            handle_static(client_socket, path);
        } else if (strcmp(path, "/stats") == 0) {
            handle_stats(client_socket);
        } else if (strncmp(path, "/calc?", 6) == 0) {
            handle_calc(client_socket, path + 6);
        } else {
            char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nEndpoint not found.";
            send(client_socket, not_found_response, strlen(not_found_response), 0);
        }
    } else {
        char *method_not_allowed = "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\n\r\nOnly GET is allowed.";
        send(client_socket, method_not_allowed, strlen(method_not_allowed), 0);
    }

    close(client_socket);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    int port = 80;

    if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        port = atoi(argv[2]);
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        close(server_socket);
        return 1;
    }

    printf("Server is listening on port %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (*client_socket < 0) {
            perror("Accept failed");
            free(client_socket);
            continue;
        }

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_socket);
        pthread_detach(thread);
    }

    close(server_socket);
    return 0;
}
