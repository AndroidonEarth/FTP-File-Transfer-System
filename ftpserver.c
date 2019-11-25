/*
 * Program name: ftpserver
 * Author: Andrew Swaim
 * Date: November 2019
 * Description: 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <dirent.h>

typedef enum { FALSE, TRUE } bool;   // bool type for C89/C99 compilation
//typedef enum { SEND,  RECV } mode_t; // mode_t type to specify sendrecv() mode

#define BUF_LEN 128
//#define LG_BUF 4096
#define CMD_OK  "OK"
#define BAD_CMD "INVALID COMMAND"
#define BAD_DIR "ERROR READING DIRECTORY"
#define BAD_FIL "FILE NOT FOUND"

/*
 * Declarations
 */
int ctrlListen(char* port);
int dataConnect(char* host, char* port);
void handleRequest(int ctrlConn, char* host); 
int getDir(char* buf);
int getFile(char* buf, char *name);
int sendAll(int conn, char* str, int len);
void* getInAddr(struct sockaddr *client);
void bye(int signum);

/*
 * Main
 */
int main(int argc, char *argv[]) {

    int port, sock, conn;
    struct sockaddr_storage client;
    socklen_t clientSize;
    char clientHost[INET6_ADDRSTRLEN];

    // Validate num of command line args
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s port\n", argv[0]); exit(1);
    }
    // Validate port num
    port = atoi(argv[1]);
    if (port < 1024 || port > 65535) {
        fprintf(stderr, "ERROR, invalid port: %d\n", port); exit(1);
    }
    if (port < 50000) {
        printf("WARNING, recommended to use port number above 50000\n");
    }
    
    // Setup signal handler.
    // Signal handling from Beej's guide
    // on the page titled '3. Signals'
    // https://beej.us/guide/bgipc/html/multi/signals.html
    struct sigaction sa;
    sa.sa_handler = bye;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) { perror("sigaction"); exit(1); }

    sock = ctrlListen(argv[1]);

    printf("Welcome to ftpserver! (press CTRL-C at any time to exit)\n");
    while (1) {
        printf("Waiting for connection...\n");

        // Accept client connection.
        clientSize = sizeof(client);
        if ((conn = accept(sock, (struct sockaddr *)&client, &clientSize)) == -1) {
            perror("ERROR, accepting client connection\n");
            continue;
        }
        printf("Client connection established!\n");

        // get the client host
        inet_ntop(client.ss_family, getInAddr((struct sockaddr*)&client),
                clientHost, sizeof(clientHost));

        handleRequest(conn, clientHost);
        close(conn);
        printf("Client connection closed.\n");
    }

    perror("ERROR: program did not exit normally\n");
    return -1;
}

/*
 * Definitions
 */

// Name: ctrlListen()
// Desc: Gets the server info, sets up the socket, and tries to bind and listen on a port number.
// Arg : The port number to try to bind and listen on.
// Pre : A port number were declared on the command line and passed as the arg.
// Post: The server either successfully binds, listens, and returns the socket file descriptor or exits.
// Rtrn: The socket file descriptor for the connection.
int ctrlListen(char *port) {
    
    // Socket setup and connection from Beej's guide
    // in the section 'A Simple Stream Server'
    // https://beej.us/guide/bgnet/html/#a-simple-stream-server
    int sock, yes=1;
    struct addrinfo addr, *serv, *ptr;

    // setup server address struct
    memset(&addr, 0, sizeof(addr));
    addr.ai_family = AF_UNSPEC;
    addr.ai_socktype = SOCK_STREAM;
    addr.ai_flags = AI_PASSIVE;

    // get address info of the server host
    if (getaddrinfo(NULL, port, &addr, &serv) != 0) {
        fprintf(stderr, "ERROR, could not get address info on port: %s\n", port);
        exit(1);
    }

    // loop through all possible connections and try to set socket options and bind.
    for (ptr = serv; ptr != NULL; ptr = ptr->ai_next) {
        
        if ((sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) >= 0) { // socket found
            
            if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
                perror("ERROR, setting socket options\n"); exit(1); // critical error setting options
            }
            else if (bind(sock, ptr->ai_addr, ptr->ai_addrlen) == -1) {
                close(sock); // if bind unsuccessful be sure to close socket.
            }
            else { break; } // setting socket options and binding successful - exit loop.
        }
    }
    freeaddrinfo(serv); // not needed anymore

    // If pointer looped to the end then binding was unsuccessful
    if (ptr == NULL) {
        fprintf(stderr, "ERROR, failed to bind socket on port: %s\n", port);
        exit(1);
    }

    // Try to start listening
    if (listen(sock, 1) == -1) {
        fprintf(stderr, "ERROR, listening on port: %s\n", port);
        exit(1);
    }

    return sock;
}

// Name: dataConnect()
// Desc: Gets the client info, sets up the socket, and tries to connect to the client.
// Arg1: The host address of the client to try to connect to.
// Arg2: The port number to try to connect to the client on.
// Pre : A host name was extracted from the control connection, and a port number was supplied by the client.
// Post: The server either successfully connects and returns the socket file descriptor or exits.
// Rtrn: The socket file descriptor for the data connection.
int dataConnect(char *host, char *port) {
    
    // Socket setup and connection from Beej's guide
    // in the section 'A Simple Stream Client'
    // https://beej.us/guide/bgnet/html/#a-simple-stream-client
    int sock;
    struct addrinfo addr, *serv, *ptr;

    // setup server address struct
    memset(&addr, 0, sizeof(addr));
    addr.ai_family = AF_UNSPEC;
    addr.ai_socktype = SOCK_STREAM;

    // get address info of the server host
    if (getaddrinfo(host, port, &addr, &serv) != 0) {
        fprintf(stderr, "ERROR, could not get address info for host: %s port: %s\n", host, port);
        return -1;
    }

    // loop through all possible connections and try to connect
    for (ptr = serv; ptr != NULL; ptr = ptr->ai_next) {
        
        if ((sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) >= 0) {
            
            if (connect(sock, ptr->ai_addr, ptr->ai_addrlen) == -1) {
                close(sock); // make sure to close connection if connect resulted in err
            }
            else { break; } // connection successful, exit loop
        }
    }
    freeaddrinfo(serv); // not needed anymore

    // If pointer looped to the end then connecting was unsuccessful
    if (ptr == NULL) {
        fprintf(stderr, "ERROR, failed to connect to host: %s on port: %s\n", host, port);
        return -1;
    }

    return sock;
}

// Name: handleRequest()
// Desc:
// Arg1:
// Arg2:
// Pre :
// Post:
void handleRequest(int ctrlConn, char* host) {

    // Socket variables
    int dataConn;
    struct sockaddr_storage client;
    socklen_t clientSize;

    char cmd[BUF_LEN];  // to hold the full command from the client
    char name[BUF_LEN]; // to hold a filename
    char* msg;
    char lenStr[BUF_LEN];
    int len;
    char* token;

    // Get command
    memset(cmd, '\0', sizeof(cmd));
    if (recv(ctrlConn, cmd, sizeof(cmd)-1, 0) <= 0) {
        perror("ERROR, receiving command from client\n");
        return;
    }

    // Parse command
    token = strtok(cmd, " ");
    if (strcmp(token, "-l") == 0) { // get directory contents command

        // Try to get the directory contents
        if ((len = getDir(msg)) == -1) {
            
            // error reading directory contents
            if (send(ctrlConn, BAD_DIR, sizeof(BAD_DIR)-1, 0) <= 0) {
                perror("ERROR, ERROR READING DIRECTORY error not sent to client\n");
            }
            return;
        }

    } else if (strcmp(token, "-g") == 0) { // get file contents command
        
        // Get the filename
        token = strtok(NULL, " ");
        memset(name, '\0', sizeof(name));
        strcpy(name, token);

        // Try to get the file contents
        if ((len = getFile(msg, name)) == -1) {

            // could not find file to open
            if (send(ctrlConn, BAD_FIL, sizeof(BAD_FIL)-1, 0) <= 0) {
                perror("ERROR, FILE NOT FOUND error not sent to client\n");
            }
            return;
        }
        
        
    } else { // invalid command
        if (send(ctrlConn, BAD_CMD, sizeof(BAD_CMD)-1, 0) <= 0) { 
            perror("ERROR, INVALID COMMAND error not sent to client\n");
        }
        return;
    }

    // command [and filename] good, send acknowledgment
    if (send(ctrlConn, CMD_OK, sizeof(CMD_OK)-1, 0) <= 0) {
        perror("ERROR, OK response not sent to client\n");
        return;
    }

    // Get the length of the message as a string
    memset(lenStr, '\0', sizeof(lenStr));
    sprintf(lenStr, "%d", len);

    // Get the port number and open data connection with client host
    token = strtok(NULL, " ");
    if ((dataConn = dataConnect(token, host)) == -1) { return; } // Error connecting

    // Send buffer length and then buffer contents.
    if (send(dataConn, lenStr, sizeof(lenStr)-1, 0) <= 0) {
        perror("ERROR, could not send message length, aborting\n");
        close(dataConn);
        return;
    }
    if (sendAll(dataConn, msg, len) < len) { 
        perror("WARNING, entire message not sent\n"); 
    }

    /* TODO: definitely implement this wait for receipt to handle large files!
    // Get acknowledgment of receipt from client before closing
    memset(cmd, '\0', sizeof(cmd));
    if (recv(dataCon, cmd, sizeof(cmd)-1, 0) <= 0) {
        perror("ERROR, getting acknowledgment back from client\n");
    }
    */

    close(dataConn);
    free(msg);
}


// Name: sendAll()
// Desc: Handles the sending of a long message to the client.
// Arg1:
// Arg2:
// Arg3:
// Pre : A message is retrieved by server (either a directory or file contents).
// Post: The entire message up to the provided length is sent to the client.
// Rtrn: 0 if successful, or -1 if an error is encountered.
int sendAll(int conn, char *str, int len) {
    
    // Handling partial sends taken from Beej's guide
    // in the section 'Handling Partial send()s'
    // https://beej.us/guide/bgnet/html/#sendall
    int total = 0;
    int rem = len;
    int n;

    while (total < len) {
        n = send(conn, str+total, rem, 0);
        if (n == -1) { break; }
        total += n;
        rem -= n;
    }

    return ((n == -1) ? -1 : total);
}

// Name: get_in_addr()
// Desc: get an IPv4 or IPv6 sockaddr
// Arg : 
// Pre :
// Post:
// Rtrn:
void* getInAddr(struct sockaddr* client) {

    // Getting IPv4 or IPv6 sockaddr rom Beej's guide 
    // in the section 'A Simple Stream Server'
    // https://beej.us/guide/bgnet/html/#a-simple-stream-server
    if (client->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)client)->sin_addr);
    }

    return &(((struct sockaddr_in6*)client)->sin6_addr);
}

// Name: getDir()
// Desc:
// Arg :
// Pre :
// Post:
// Rtrn:
int getDir(char* buf) {

    // Opening and ready directory contents from official manpage
    // http://man7.org/linux/man-pages/man3/readdir.3.html
    DIR* dir;
    struct dirent* dirEnt;
    int i = 0;
    int count = 0;
    int size = 0;
    char* files[BUF_LEN];

    if ((dir = opendir(".")) == NULL) { 
        perror("ERROR, opening current directory");
        return -1; 
    }

    // Loop through the directory and get the names of the regular files.
    while ((dirEnt = readdir(dir)) != NULL) {
        if (dirEnt->d_type == DT_REG) {
            strcpy(files[count], dirEnt->d_name);
            size += strlen(files[i]);
            size++; // to account for the newlines
            count++;
        }
    }
    closedir(dir);

    // If there were no regular files in the directory just put a blank space
    if (size == 0) { buf = " "; return 1; }
    else {
        // Append all the file names as one long string
        buf = malloc(sizeof(char) * (size+1)); 
        
        bool first = TRUE;
        for (i = 0; i < count; i++) {
            if (first) {
                strcpy(buf, files[i]);
                strcat(buf, "\n");
                first = FALSE;
            } else {
                strcat(buf, files[i]);
                strcat(buf, "\n");
            }
        }
        buf[size+1] = '\0';
    }

    return size; // return size of concatenated string
}

// Name: getFile()
// Desc:
// Arg1:
// Arg2:
// Pre :
// Post:
// Rtrn:
int getFile(char* buf, char *name) {

    // Opening and reading files from official manpage
    // http://man7.org/linux/man-pages/man3/fopen.3.html
    // Using fseek and ftell to get the file size from official manpage
    // http://man7.org/linux/man-pages/man3/fseek.3.html
    FILE* file;
    long size;

    if ((file = fopen(name, "r")) == NULL) {
        fprintf(stderr, "ERROR, could not open file %s\n", name);
        return -1;
    }

    // Get size of file
    if (fseek(file, 0, SEEK_END) != 0
            || (size = ftell(file)) == -1
            || fseek(file, 0, SEEK_SET) != 0) {
        perror("ERROR, getting file size\n");
        return -1;
    }

    // Get the file contents
    buf = malloc(sizeof(char) * (size+1));
    if (fread(buf, sizeof(char), size, file) < size || ferror(file)) {
        perror("ERROR, reading file contents to buffer\n");
        return -1;
    } else {
        buf[size+1] = '\0';
    }

    return size;
}

// Name: bye()
// Desc: Signal handler to exit the program
// Pre : CTRL + C is pressed / SIGINT signal is sent
// Post: The program is exited
void bye(int signum) {
 
    printf(""); // flush
    printf("\nftpserver is exiting... Goodbye!\n");
    exit(0);
}

