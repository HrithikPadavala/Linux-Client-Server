#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdbool.h>

#define PORT 8085
#define BUFFER_SIZE 256
#define HOME_DIR "./client_tmp/"

void receive_file(int client_socket, long file_size, char *file_name)
{
    char *file_path = malloc(strlen(HOME_DIR) + strlen(file_name) + 1);// dynamically allocates memory to store the complete file path.
    strcpy(file_path, HOME_DIR);
    strcat(file_path, file_name);

    FILE *temp_file = fopen(file_path, "wb");
    if (temp_file == NULL)
    {
        printf("Error creating temp file.\n");
        return;
    }

    char response[BUFFER_SIZE];
    ssize_t bytes_received = 0;
    ssize_t total_bytes_received = 0;
    // Receive file data in chunks and write to the temporary file
    while (total_bytes_received < file_size)
    {    // Receive data from the server
        bytes_received = recv(client_socket, response, sizeof(response), 0);// It uses the recv function to receive data from the server into the response buffer.
        
        // Error handling for receiving data
        if (bytes_received <= 0)
        {
            printf("Error receiving file.\n");
            fclose(temp_file);
            return;
        }
         // Write received data to the temporary file
        fwrite(response, 1, bytes_received, temp_file);
          // Update total bytes received
        total_bytes_received += bytes_received;
        printf("Received %ld bytes\n", total_bytes_received);
    }
    // Close the temporary file
    fclose(temp_file);
    printf("Received %s\n", file_name);
}

bool has_client_requested_file(char clientInput[256])
{
    return strncmp(clientInput, "w24fz ", 6) == 0 || strncmp(clientInput, "w24ft ", 6) == 0 || strncmp(clientInput, "w24fdb ", 7) == 0 || strncmp(clientInput, "w24fda ", 7) == 0;
}

int main()
{
    int client_socket, n;
    struct sockaddr_in server_addr;
    struct hostent *server;
    char clientInput[BUFFER_SIZE];
    char serverResponse[BUFFER_SIZE];

    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Get server IP address
    server = gethostbyname("localhost");
    if (server == NULL)
    {
        fprintf(stderr, "Error, no such host\n");
        exit(EXIT_FAILURE);
    }

    // Initialize server address structure
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length);
    server_addr.sin_port = htons(PORT);

    // Connect to server
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error connecting to server");
        exit(EXIT_FAILURE);
    }

    // Communication with server
    while (1)
    {
        printf("Enter message: ");
        fgets(clientInput, BUFFER_SIZE, stdin);
        clientInput[strcspn(clientInput, "\n")] = 0;

        // Send message to server
        n = write(client_socket, clientInput, strlen(clientInput));
        if (n < 0)
        {
            perror("Error writing to socket");
            exit(EXIT_FAILURE);
        }
        // dirlist
        if (strncmp(clientInput, "dirlist ", 8) == 0)
        {
            int folder_count;
            recv(client_socket, &folder_count, sizeof(int), 0);
            // when no folders present
            if (folder_count == 0)
            {
                recv(client_socket, serverResponse, BUFFER_SIZE, 0);
                printf("Server response: %s\n", serverResponse);
            }
            else
            { // when folders present
                printf("%d folders found\n", folder_count);
                for (int i = 0; i < folder_count; i++)
                {
                    recv(client_socket, serverResponse, BUFFER_SIZE, 0);
                    printf("%s\n", serverResponse);
                    bzero(serverResponse, BUFFER_SIZE);// The bzero() function erases the data in the n bytes of the memory.
                }
            }
        }// TAR
        else if (has_client_requested_file(clientInput))
        {
            long file_size;
            recv(client_socket, &file_size, sizeof(long), 0);
            if (file_size > 0)
            {
                printf("Received file size: %ld\n", file_size);
                receive_file(client_socket, file_size, "temp.tar.gz");
            }
            else// the file size received is 0 or less, it indicates that the file does not exist or an error occurred. 
            {
                recv(client_socket, serverResponse, BUFFER_SIZE, 0);
                printf("Server response: %s\n", serverResponse);
            }
        }//Quitc
        else if (strcmp(clientInput, "quitc") == 0)
        {
            break;
        }
        else
        {
            recv(client_socket, serverResponse, BUFFER_SIZE, 0);
            printf("Server response: %s\n", serverResponse);
        }
        bzero(clientInput, BUFFER_SIZE);
        bzero(serverResponse, BUFFER_SIZE);
    }

    // Close socket
    close(client_socket);
    return 0;
}
