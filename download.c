#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 21

char buffer[1024];

void parse_url(char *url, char *user, char *password, char *host, char *path) {
    if (strncmp(url, "ftp://", 6) == 0) {
        url += 6;
    }

    if (strchr(url, '@') != NULL) {
        sscanf(url, "%[^:]:%[^/]/%s", user, password, path);
        char* last_character_of_password = password + strlen(password) - 1;
        while(*last_character_of_password != '@'){
            last_character_of_password--;
        }
        *last_character_of_password = '\0';
        strcpy(host, last_character_of_password + 1);
    } else {
        strcpy(user, "anonymous");
        strcpy(password, "password@example.com");
        sscanf(url, "%[^/]/%s", host, path);
    }
}

void read_response(int sockfd, char *buffer) {
    char* buffer_backup = buffer;
    do {
        unsigned char byte = 0;
        while(byte != '\n'){
            read(sockfd, &byte, 1);
            *buffer = byte;
            buffer++;
        }
        buffer = buffer_backup;
    } while (buffer[3] == '-');
}

void write_command(int sockfd, char *command) {
    size_t bytes = write(sockfd, command, strlen(command));
    if(bytes < 0) {
        perror("write()");
        exit(-1);
    }
}

void login(int sockfd, char *user, char *pass) {
    write_command(sockfd, "user ");
    write_command(sockfd, user);
    write_command(sockfd, "\n");

    read_response(sockfd, buffer);
    
    if(buffer[0] == '2' && buffer[1] == '3' && buffer[2] == '0') {
        printf("Login successful.\n");
        return;
    }
    if(buffer[0] != '3' || buffer[1] != '3' || buffer[2] != '1') {
        printf("Login failed - User doesn't exist\n");
        exit(-1);
    }

    write_command(sockfd, "pass ");
    write_command(sockfd, pass);
    write_command(sockfd, "\n");

    read_response(sockfd, buffer);
    if(buffer[0] != '2' || buffer[1] != '3' || buffer[2] != '0') {
        printf("Login failed - Wrong Password\n");
        exit(-1);
    }

    printf("Login successful.\n");
}

void parse_to_get_ip_port(char *buffer, char *ip, int *port) {
    char *token = strtok(buffer, "(");
    token = strtok(NULL, "(");
    token = strtok(token, ")");
    token = strtok(token, ",");
    for(int i = 0; i < 4; i++) {
        strcat(ip, token);
        strcat(ip, ".");
        token = strtok(NULL, ",");
    }
    ip[strlen(ip)-1] = '\0';
    *port = atoi(token) * 256;
    token = strtok(NULL, ",");
    *port += atoi(token);
}

void enter_passive_mode(int sockfd, char *ip, int *port) {
    write_command(sockfd, "pasv\n");
    read_response(sockfd, buffer);
    if (buffer[0] != '2' || buffer[1] != '2' || buffer[2] != '7') {
        printf("Error entering passive mode.\n");
        exit(-1);
    }
    
    parse_to_get_ip_port(buffer, ip, port);
    printf("Entered passive mode. Content address is %s:%d.\n", ip, *port);
}

int create_socket(char* ip, int port){
    int sockfd;
    struct sockaddr_in server_addr;

    /*server address handling*/
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);    /*32 bit Internet address network byte ordered*/
    server_addr.sin_port = htons(port);        /*server TCP port must be network byte ordered */

    /*open a TCP socket*/
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(-1);
    }
    
    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        exit(-1);
    }

    return sockfd;
}

void request_content(int control_socket, char* content) {
    write_command(control_socket, "retr ");
    write_command(control_socket, content);
    write_command(control_socket, "\n");
    read_response(control_socket, buffer);
    if(buffer[0] != '1' || buffer[1] != '5' || buffer[2] != '0') {
        printf("Error requesting content.\n");
        exit(-1);
    }
    printf("Requested %s.\n", content);
}

void get_content(int content_socket, const char *content) {
    char *filename = basename(strdup(content));
    FILE *fd;

    if ((fd = fopen(filename, "wb")) == NULL) {
        perror("open()");
        fprintf(stderr, "Error opening file %s: %s\n", filename, strerror(errno));
        exit(-1);
    }
    
    printf("Downloading...\n");

    unsigned char buffer[1];
    unsigned int byte;
    while (read(content_socket, &byte, 1) > 0) {
        buffer[0] = byte;
        if (fwrite(buffer, 1, 1, fd) < 0) {
            perror("fwrite()");
            exit(-1);
        }
    }

    if (fclose(fd) < 0) {
        perror("close() content");
        exit(-1);
    }

    printf("Downloaded content.\n");
}

int main(int argc, char **argv) {

    if (argc != 2) {
        printf("Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n", argv[0]);
        exit(-1);
    }

    char user[50], password[50], host[50], path[50];
    parse_url(argv[1], user, password, host, path);
    
    if (strchr(path, '/') != NULL) {
    	memmove(path + 1, path, 49);
    	path[0] = '/';
    }
    		

    struct hostent *h;
    if ((h = gethostbyname(host)) == NULL) {
        herror("gethostbyname()");
        exit(-1);
    }

    char control_ip[16];
    sprintf(control_ip, "%s", inet_ntoa(*((struct in_addr *) h->h_addr)));
    int control_socket = create_socket(control_ip, SERVER_PORT);
    read_response(control_socket, buffer);
    if (buffer[0] != '2' || buffer[1] != '2' || buffer[2] != '0') {
        printf("Connection refused by server.\n");
        exit(-1);
    }
    printf("Connection with server estabilished.\n");

    login(control_socket, user, password);

    char content_ip[16] = "";
    int content_port;
    enter_passive_mode(control_socket, content_ip, &content_port);
    int content_socket = create_socket(content_ip, content_port);
    printf("Created content socket.\n");

    request_content(control_socket, path);
    get_content(content_socket, path);

    if (close(content_socket)<0) {
        perror("close()");
        exit(-1);
    }
    printf("Closed content socket.\n");

    if (close(control_socket)<0) {
        perror("close()");
        exit(-1);
    }
    printf("Closed control socket.\n");

    return 0;
}

