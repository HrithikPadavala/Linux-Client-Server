#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <stdbool.h>
#include <pwd.h>

#define PORT 8085
#define MAX_PENDING_CONNECTIONS 5
#define BUFFER_SIZE 256           // Maximum size of message that can be sent or received
#define HOME_DIR "/home/rudaba/" // Change this to the directory where you want to search for files
#define MAX_FILES 1024            // Maximum number of files that can be searched
#define MIRROR_PORT_1 8086        // Define a separate port 1 for mirroring traffic
#define MIRROR_PORT_2 8088        // Define a separate port 2 for mirroring traffic

typedef enum SearchType
{
    FILE_SIZE,
    FILE_EXTENSION,
    FILE_CREATION_DATE_BEFORE,
    FILE_CREATION_DATE_AFTER
} SearchType;

typedef struct FileSearchCriteria
{
    long long minSize;
    long long maxSize;
    char fileExtensions[3][10];
    int numExtensions;
    time_t date;
    SearchType searchType;
} FileSearchCriteria;


typedef int (*CompareFunction)(const char *, const char *);

void send_message(int socket, char *message)
{
    int n = send(socket, message, BUFFER_SIZE, 0);
    if (n < 0)
    {
        perror("Error writing to socket");
        exit(EXIT_FAILURE);
    }
}

void send_int(int conn, int number)
{
    ssize_t bytes_sent = send(conn, &number, sizeof(int), 0);
    if (bytes_sent != sizeof(int))
    {
        perror("send");
        exit(EXIT_FAILURE);
    }
}

void send_long(int conn, long file_size)
{
    ssize_t bytes_sent = send(conn, &file_size, sizeof(long), 0);
    if (bytes_sent != sizeof(long))
    {
        perror("send");
        exit(EXIT_FAILURE);
    }
}

void send_file(int socket, char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    fseek(fp, 0L, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    send_long(socket, file_size); // Send the file size to the client
    printf("Sending file of size %ld bytes\n", file_size);

    char buffer[BUFFER_SIZE];
    while (1)
    {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), fp);
        if (bytes_read <= 0)
        {
            break;
        }
        send(socket, buffer, bytes_read, 0);
        printf("Sent %ld bytes\n", bytes_read);
    }

    fclose(fp);
}

void create_tarball(char files[][BUFFER_SIZE], int num_files)
{
    FILE *fp;
    char command[1024];
    int i;

    // Create a temporary list file
    fp = fopen("./tmp/file_list.txt", "w");
    if (fp == NULL)
    {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < num_files; i++)
    {
        fprintf(fp, "%s\n", files[i]);
    }

    fclose(fp);

    // Create tarball
    sprintf(command, "tar -czf ./tmp/temp.tar.gz -T ./tmp/file_list.txt");
    system(command);

     //Remove temporary list file
     remove("./tmp/file_list.txt");
}

bool is_file_size_in_range(char *path, long long size1, long long size2)
{
    struct stat fileStat;
    stat(path, &fileStat);                         // Get the file status
    long long filesize = fileStat.st_size;         // Get the file size
    return filesize >= size1 && filesize <= size2; // Check if the file size is within the range
}

bool is_extension_match(char *path, char extensions[][10], int num_extensions)
{
    char *extension = strrchr(path, '.'); // Get the extension of the file
    if (extension == NULL)
        return false; // No extension found

    // Compare the extension with the list of extensions
    for (int i = 0; i < num_extensions; i++)
    {
        if (strcmp(extension + 1, extensions[i]) == 0)
        {
            return true; // Extension matches
        }
    }
    return false; // No matching extension found
}

bool is_file_created_before_date(char *path, time_t date)
{
    struct stat fileStat;
    stat(path, &fileStat); // Get the file status
    return fileStat.st_ctime < date;
}

bool is_file_created_after_date(char *path, time_t date)
{
    struct stat fileStat;
    stat(path, &fileStat); // Get the file status
    return fileStat.st_ctime > date;
}

bool match_search_condition(char *path, FileSearchCriteria fileSearchCriteria)
{
    switch (fileSearchCriteria.searchType)
    {
    case FILE_SIZE:
        return is_file_size_in_range(path, fileSearchCriteria.minSize, fileSearchCriteria.maxSize);
    case FILE_EXTENSION:
        return is_extension_match(path, fileSearchCriteria.fileExtensions, fileSearchCriteria.numExtensions);
    case FILE_CREATION_DATE_BEFORE:
        return is_file_created_before_date(path, fileSearchCriteria.date);
    case FILE_CREATION_DATE_AFTER:
        return is_file_created_after_date(path, fileSearchCriteria.date);
    default:
        return false;
    }
}

void get_file_list(char *root_directory, FileSearchCriteria fileSearchCriteria, char fileList[][BUFFER_SIZE], int *num_files)
{

    DIR *dir;
    struct dirent *entry;
    char path[BUFFER_SIZE];

    dir = opendir(root_directory);
    if (dir == NULL)
    {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_DIR)
        {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                snprintf(path, sizeof(path), "%s/%s", root_directory, entry->d_name);
                get_file_list(path, fileSearchCriteria, fileList, num_files);
            }
        }
        else
        {
            snprintf(path, sizeof(path), "%s/%s", root_directory, entry->d_name);
            if (match_search_condition(path, fileSearchCriteria))
            {
                printf("File#%d Found file: %s\n", *num_files, path);
                snprintf(fileList[*num_files], BUFFER_SIZE, "%s", path);
                (*num_files)++;
            }
        }
    }

    closedir(dir);
}

void handle_file_search_and_send(int conn, FileSearchCriteria fileSearchCriteria)
{
    char files[MAX_FILES][BUFFER_SIZE];
    int num_files = 0;
    get_file_list(HOME_DIR, fileSearchCriteria, files, &num_files);
    printf("Number of files found: %d\n", num_files);

    if (num_files > 0)
    {
        create_tarball(files, num_files);
        send_file(conn, "./tmp/temp.tar.gz");
    }
    else
    {
        send_long(conn, 0); // Send file size 0 to indicate no file found
        send_message(conn, "No file found");
        printf("No file found\n");
    }
}

 void get_folder_list(char *root_directory, char folderList[][BUFFER_SIZE], int *num_folders)
 {

     DIR *dir;
     struct dirent *entry;

     dir = opendir(root_directory);
     if (dir == NULL)
     {
         perror("opendir");
         exit(EXIT_FAILURE);
     }

     while ((entry = readdir(dir)) != NULL)
     {
         if (entry->d_type == DT_DIR)
         {
             if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
             {
                 snprintf(folderList[*num_folders], BUFFER_SIZE, "%s", entry->d_name);
                 (*num_folders)++;
             }
        }
     }

     closedir(dir);
 }


void search_file_by_file_name(const char *filename, const char *root_directory, char *result, bool *found)
{
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char path[BUFFER_SIZE];
    char file_permissions[10];
    char created_time[50];

    dir = opendir(root_directory);
    if (dir == NULL)
    {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_DIR)
        {
            // If the entry is a directory, recursively search in the subdirectory
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                snprintf(path, sizeof(path), "%s/%s", root_directory, entry->d_name);
                search_file_by_file_name(filename, path, result, found);
            }
        }
        else
        {
            // If the entry is a file, check if it matches the filename
            // and update the result if it is found
            if (strcmp(entry->d_name, filename) == 0 && !*found)
            {
                snprintf(path, sizeof(path), "%s/%s", root_directory, entry->d_name);
                stat(path, &file_stat);
                strftime(created_time, BUFFER_SIZE, "%Y-%m-%d %H:%M:%S", localtime(&file_stat.st_ctime));
                sprintf(file_permissions, "%o", file_stat.st_mode & 0777);
                snprintf(result, BUFFER_SIZE, "%s %lld(bytes) %s %s", filename, (long long)file_stat.st_size, created_time, file_permissions);
                closedir(dir);
                *found = true;
                return;
            }
        }
    }

    closedir(dir);
    return;
}

FileSearchCriteria get_FileSearchCriteria_from_file_extension(char *file_extension_str)
{
    // Create a FileSearchCriteria object
    FileSearchCriteria fileSearchCriteria;

    // Split the file_extension_str into individual file extensions
    char file_extensions[3][10];
    int num_extensions = 0;
    char *token = strtok(file_extension_str, " ");
    while (token != NULL)
    {
        // Copy each file extension to the file_extensions array
        strcpy(file_extensions[num_extensions], token);
        num_extensions++;
        token = strtok(NULL, " ");
    }

    // Set the search criteria in the FileSearchCriteria object
    fileSearchCriteria.minSize = 0;
    fileSearchCriteria.maxSize = 0;
    fileSearchCriteria.numExtensions = num_extensions;
    fileSearchCriteria.searchType = FILE_EXTENSION;

    // Copy the file extensions to the search criteria
    for (int i = 0; i < num_extensions; i++)
    {
        strcpy(fileSearchCriteria.fileExtensions[i], file_extensions[i]);
    }

    return fileSearchCriteria;
}

FileSearchCriteria get_FileSearchCriteria_from_date_string(char *date_str, SearchType searchType)
{
    // Convert the given date string to a struct tm
    struct tm given_date;
    memset(&given_date, 0, sizeof(struct tm));
    strptime(date_str, "%Y-%m-%d", &given_date);
    time_t given_time = mktime(&given_date);

    // Create a FileSearchCriteria object and set the search criteria
    FileSearchCriteria fileSearchCriteria;
    fileSearchCriteria.minSize = 0;
    fileSearchCriteria.maxSize = 0;
    fileSearchCriteria.numExtensions = 0;
    fileSearchCriteria.date = given_time;
    fileSearchCriteria.searchType = searchType;

    return fileSearchCriteria;
}

int compare_by_alphabet(const char *a, const char *b)
{
    return strcmp(a, b);
}

int compare_by_creation_time(const char *a, const char *b)
{
    struct stat stat_a, stat_b;
    stat(a, &stat_a);
    stat(b, &stat_b);
    return stat_a.st_ctime - stat_b.st_ctime;
}

void bubble_sort(char arr[MAX_FILES][BUFFER_SIZE], int n, CompareFunction compare)
{
    int i, j;
    char temp[BUFFER_SIZE];

    for (i = 0; i < n - 1; i++)
    {
        for (j = 0; j < n - i - 1; j++)
        {
            if (compare(arr[j], arr[j + 1]) > 0)
            {
                // Swap arr[j] and arr[j+1]
                strcpy(temp, arr[j]);
                strcpy(arr[j], arr[j + 1]);
                strcpy(arr[j + 1], temp);
            }
        }
    }
}

void handle_dirlist(int conn, char *orderOption)
{
    char directories[MAX_FILES][BUFFER_SIZE];
    int num_folders = 0;
    get_folder_list(HOME_DIR, directories, &num_folders);
    printf("Number of folders found: %d\n", num_folders);
    send_int(conn, num_folders);
    if (num_folders == 0)
    {
        send_message(conn, "No folders found");
        return;
    }

    if (strcmp(orderOption, "-t") == 0)
    {
        bubble_sort(directories, num_folders, compare_by_creation_time);
    }
    else // -a
    {
        bubble_sort(directories, num_folders, compare_by_alphabet);
    }

    for (int i = 0; i < num_folders; i++)
    {
        send_message(conn, directories[i]);
        printf("%s\n", directories[i]);
    }
}

void handle_w24fn(int conn, char *filename)
{
    char fileInfo[BUFFER_SIZE] = "File not found";
    bool found = false;
    search_file_by_file_name(filename, HOME_DIR, fileInfo, &found);
    send_message(conn, fileInfo);
}

void handle_w24fz(int conn, long long size1, long long size2)
{
    FileSearchCriteria fileSearchCriteria = {size1, size2, {}, 0, FILE_SIZE};
    handle_file_search_and_send(conn, fileSearchCriteria);
}

void handle_w24ft(int conn, char *file_extension_str)
{
    FileSearchCriteria fileSearchCriteria = get_FileSearchCriteria_from_file_extension(file_extension_str);
    handle_file_search_and_send(conn, fileSearchCriteria);
}

void handle_w24fdb(int conn, char *date_str)
{
    FileSearchCriteria fileSearchCriteria = get_FileSearchCriteria_from_date_string(date_str, FILE_CREATION_DATE_BEFORE);
    handle_file_search_and_send(conn, fileSearchCriteria);
}

void handle_w24fda(int conn, char *date_str)
{
    FileSearchCriteria fileSearchCriteria = get_FileSearchCriteria_from_date_string(date_str, FILE_CREATION_DATE_AFTER);
    handle_file_search_and_send(conn, fileSearchCriteria);
}

void crequest(int client_socket)
{
    char buffer[BUFFER_SIZE];
    int n;

    // Communication with the client
    while (1)
    {
        bzero(buffer, BUFFER_SIZE); // Clears the buffer to store incoming data
        n = read(client_socket, buffer, BUFFER_SIZE); //Reads data from the client socket into the buffer
        if (n < 0)
        {
            perror("Error reading from socket");
            exit(EXIT_FAILURE);
        }
        if (n == 0)
        {
            printf("Client disconnected\n");
            break;
        }
        printf("Received message from client: %s\n", buffer);
        // Check the received message and handle accordingly
        if (strncmp(buffer, "dirlist ", 8) == 0)
        {
            char *orderOption = buffer + 8; // Extracts the option from the received message
            handle_dirlist(client_socket, orderOption);
        }
        else if (strncmp(buffer, "w24fn ", 6) == 0)
        {
            char *filename = buffer + 6;
            printf("Searching for file: %s\n", filename);
            handle_w24fn(client_socket, filename);
        }
        else if (strncmp(buffer, "w24fz ", 6) == 0)
        {
            long long size1, size2;
            sscanf(buffer + 6, "%lld %lld", &size1, &size2);
            printf("Searching for file with size between %lld and %lld\n", size1, size2);
            handle_w24fz(client_socket, size1, size2);
        }
        else if (strncmp(buffer, "w24ft ", 6) == 0)
        {
            char *file_extension = buffer + 6;
            printf("Searching for file with extension: %s\n", file_extension);
            handle_w24ft(client_socket, file_extension);
        }
        else if (strncmp(buffer, "w24fdb ", 7) == 0)
        {
            char *date_str = buffer + 7;
            printf("Searching for file created before date: %s\n", date_str);
            handle_w24fdb(client_socket, date_str);
        }
        else if (strncmp(buffer, "w24fda ", 7) == 0)
        {
            char *date_str = buffer + 7;
            printf("Searching for file created after date: %s\n", date_str);
            handle_w24fda(client_socket, date_str);
        }
        else if (strcmp(buffer, "quitc") == 0)
        {
            printf("Client requested to close connection\n");
            break;
        }
        else
        {
            send_message(client_socket, buffer);
        }
    }

    // Close client socket
    printf("Closing connection with client\n");
    close(client_socket);
}


void redirect_to_mirror(int client_socket, int mirror_port) {
    struct sockaddr_in mirror_addr;
    int mirror_socket;

    // Create mirror socket
    mirror_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (mirror_socket < 0) {
        perror("Error creating mirror socket");
        exit(EXIT_FAILURE);
    }

    // Initialize mirror address structure
    bzero((char *)&mirror_addr, sizeof(mirror_addr));
    mirror_addr.sin_family = AF_INET;
    mirror_addr.sin_addr.s_addr = INADDR_ANY;
    mirror_addr.sin_port = htons(mirror_port);

    // Connect to mirror server
    if (connect(mirror_socket, (struct sockaddr *)&mirror_addr, sizeof(mirror_addr)) < 0) {
        perror("Error connecting to mirror server");
        exit(EXIT_FAILURE);
    }

    // Forward client data to mirror server
    char buffer[256];
    int n;
    while ((n = read(client_socket, buffer, sizeof(buffer))) > 0) {
        write(mirror_socket, buffer, n);
    }

    // Close mirror socket
    close(mirror_socket);
}

int main() {
    int server_socket, client_socket, pid;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int active_clients = 1;

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Initialize server address structure
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;//Sets the address family to IPv4.
    server_addr.sin_addr.s_addr = INADDR_ANY;// Sets the IP address of the server to accept connections on any of the host's IP addresses.
    server_addr.sin_port = htons(PORT);//Sets the port number to the specified PORT, converting it to network byte order.

    // Bind socket to address
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    //Sets the socket server_socket to listen for incoming connections, with a maximum queue length of MAX_PENDING_CONNECTIONS.
    if (listen(server_socket, MAX_PENDING_CONNECTIONS) < 0) {
        perror("Error listening for connections");
        exit(EXIT_FAILURE);
    }
    printf("Server waiting for connections...\n");

    // Accept incoming connections and fork child processes to handle them
    while (1) {
        //Accepts an incoming connection from a client and assigns the socket descriptor for communication with that client to client_socket.
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Error accepting connection");
            exit(EXIT_FAILURE);
        }
        printf("Connection accepted from client\n");

        // Check number of active clients and redirect if necessary
        if (active_clients < 4) {
            pid = fork();
            if (pid < 0) {
                perror("Error forking process");
                exit(EXIT_FAILURE);
            }
            if (pid == 0) {
                // Child process
                close(server_socket); // Close unused server socket in child
                crequest(client_socket); // Handle communication with client
                printf("Child process exiting\n");
                exit(EXIT_SUCCESS);
            } else {
                // Parent process
                close(client_socket); // Close unused client socket in parent
            }
        } else if (active_clients >= 4 && active_clients < 7) {
            // If there are 4 to 6 active clients, redirect to mirror_port_1
            printf("Redirecting to mirror_port_1\n");
            redirect_to_mirror(client_socket, MIRROR_PORT_1);
        } else if (active_clients >=7 && active_clients < 10) {
             // If there are 7 to 9 active clients, redirect to mirror_port_2
            printf("Redirecting to mirror_port_2\n");
            redirect_to_mirror(client_socket, MIRROR_PORT_2);
        }
        else{
            // If there are 10 or more active clients
            if (active_clients %3 ==1) 
            { // Fork a new process for every 3rd active client
                pid = fork();
                if (pid < 0) {
                    perror("Error forking process");
                    exit(EXIT_FAILURE);
                }
                if (pid == 0) {
                    // Child process
                    close(server_socket); // Close unused server socket in child
                    crequest(client_socket);
                    printf("Child process exiting\n");
                    exit(EXIT_SUCCESS);
                } else {
                    // Parent process
                    close(client_socket); // Close unused client socket in parent
                }
            } 
            else if (active_clients % 3 == 2) {
                printf("Redirecting to mirror_port_1\n");
                redirect_to_mirror(client_socket, MIRROR_PORT_1);
            } else if (active_clients %3 ==0) {
                printf("Redirecting to mirror_port_2\n");
                redirect_to_mirror(client_socket, MIRROR_PORT_2);
            }
        }
        active_clients++;
    }

    // Close server socket
    close(server_socket);
    return 0;
}