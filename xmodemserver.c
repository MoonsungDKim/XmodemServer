#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include "xmodemserver.h"
#include "wrapsock.h"
#include "crc16.h"

#ifndef PORT
#define PORT 52016
#endif
#define LISTENQ 10
#define MAXBUFFER 1024
/*The following function open_file_in_dir was taken from helper.c provided in the assignment*/
FILE *open_file_in_dir(char *filename, char *dirname){
    char buffer[MAXBUFFER];
    strncpy(buffer, "./", MAXBUFFER);
    strncat(buffer, dirname, MAXBUFFER - strlen(buffer));
    // create the directory dirname. Fail silently if directory exists
    if(mkdir(buffer, 0700) == -1) {
        if(errno != EEXIST){
            perror("mkdir");
            exit(1);
        }
    }
    strncat(buffer, "/", MAXBUFFER - strlen(buffer));
    strncat(buffer, filename, MAXBUFFER - strlen(buffer));
    return fopen(buffer, "w");
}
/* addclient adds a new client to the linked list of clients the server needs to check up on. Some of its
 * design was taken from the muffinman.c provided in the assignment. */
void addclient(int fd, struct in_addr addr, struct client **top){
    struct client *p = malloc (sizeof (struct client));
    struct client *i;
    if (!p){
        fprintf(stderr, "out of memory!\n");
        exit(1);
    }
    p->fd = fd;
    p->inbuf = 0;
    p->state = initial;
    p->current_block = 1;
    p->next = *top;
    *top = p;
}
/* removeclient removes the client from the linked list. Some of its design was taken from the muffinman.c 
 * provided by the assignment*/
void removeclient(int fd, struct client **top){
    struct client **p;
    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next);
    if (*p){
        struct client *t = (*p)->next;
        if ((*p)->fp){ //close files, if any, that are open for this client.
            if (fclose((*p)->fp)==-1){
                perror("close");
                exit(1);
            }
        }
        free(*p);
        *p = t;//links the list by skipping p
        fprintf(stderr, "removed client\n");
    }
    else{
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n", fd);
    }
}
/* creates a new connection with a client, some design was taken from the muffinman.c*/
void newconnection(int listenfd, struct client **top){
    int fd;
    struct sockaddr_in r;
    socklen_t socklen = sizeof(r);
    fd = Accept(listenfd, (struct sockaddr *)&r, &socklen);
    fprintf (stderr, "accepted a new client\n");
    addclient(fd, r.sin_addr, top);
}

int main (){
    int maxfd, listenfd, connfd, nread, i;
    char c = 'C';
    char temp;
    char *after;
    unsigned short block_crc;
    unsigned char char_block, crc_first, crc_second;
    char *dirname = "filestore";
    struct client *top = malloc(sizeof (struct client));
    top->fd = -1;
    top->next = NULL;
    struct client *p;
    fd_set allset;
    socklen_t clilen;
    struct sockaddr_in cliaddr, servaddr;
    listenfd = Socket(PF_INET, SOCK_STREAM, 0);
    memset(&servaddr, '\0', sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);
    
    Bind (listenfd, (struct sockaddr *) &servaddr, sizeof (servaddr));
    Listen (listenfd, LISTENQ);
    while(1){
        fprintf(stderr, "Time to Select\n");
        maxfd = listenfd;
        FD_ZERO (&allset);
        FD_SET(listenfd, &allset);
        //loop through the linked list of clients to refresh the allset
        for (p = top; p->fd >=0; p = p->next){
            FD_SET (p->fd, &allset);
            if (p->fd > maxfd) maxfd = p->fd; //if the fd is larger than the maxfd, change maxfd.
        }
        Select(maxfd+1, &allset, NULL, NULL, NULL);
        //loop through the linked list until the fd corresponding to the client that is set is found
        for (p = top; p->fd >=0; p = p->next){
            if (FD_ISSET(p->fd, &allset)) break;
        }
        //if it is our listening socket then a new client has come
        if (FD_ISSET(listenfd, &allset)){ 
            newconnection(listenfd, &top);
        }
        // otherwise its one of our old clients
        else if(!p){ //if p is null, we have a problem
            fprintf(stderr,"uhoh\n");
            exit(1);
        }
        // if p exists, then we go through the states
        else {
            if(p){
                if (p->state == initial){
                    fprintf(stderr, "initial reading from client\n");
                    // read as many as you can up to 20 characters, leaving off where it last wrote
                    nread = read(p->fd, &(p->buf[p->inbuf]), sizeof(char)*(20 - p->inbuf));
                    if(nread<0){
                        perror("read");
                        removeclient(p->fd, &top);
                    }
                    //use inbuf as an index of where to write next, and how much more can be written
                    p->inbuf = p->inbuf + nread;
                    //transfer stuff in buf to filename until a network newline is reached
                    for (i = 0; i < 20; i++){
                        p->filename[i] = p->buf[i];
                        if (p->buf[i] == '\r'){ //once the network newline is found
                            p->filename[i] = '\0';//place a null character to end the string
                            p->state = pre_block; //change states
                            p->fp = open_file_in_dir(p->filename, dirname);  //open a file
                            p->inbuf = 0;// reset inbuf to be 0, going to write over buf from 0 index
                            if (write(p->fd, &c, 1)<0){ //send 'C' to client
                                perror("write");
                                removeclient(p->fd, &top);
                            }
                            break;
                        }
                    }
                    //if the network newline is not found in the 20 characters sent by client, error in filename, drop client
                    if (p->inbuf == 20){
                        fprintf(stderr, "filename was not found. filename must be less than 20 characters\n");
                        removeclient(p->fd, &top);
                    }
                }
                if (p->state == pre_block){
                    fprintf(stderr, "pre_block readering from client \n"); 
                    nread = read(p->fd, &temp, 1); //read a single character
                    if(nread<0){ //if there was a problem with nread then drop the client
                        perror("read");
                        removeclient(p->fd, &top);
                    }
                    if (temp== EOT){
                        temp = ACK;
                        if (write(p->fd, &temp, 1)<0){
                            perror("write");
                            removeclient(p->fd, &top);
                        }
                        fprintf(stderr, "finished\n");
                        removeclient(p->fd, &top);
                    }
                    if (temp == SOH){
                        p->blocksize = 132;
                        p->state = get_block;
                    }
                    if (temp == STX){
                        p->blocksize = 1028;
                        p->state = get_block;
                    }
                }
                if (p->state == get_block){
                    fprintf(stderr, "get_block readering from client \n"); 
                    /* reads into the buffer as much as it can upto the blocksize of the client
                     * and continues writing where it left off*/
                    nread = read(p->fd, &(p->buf[p->inbuf]), p->blocksize - p->inbuf);
                    if(nread < 0){
                        perror("read");
                        removeclient(p->fd, &top);
                    }
                    p->inbuf = p->inbuf + nread;
                    //once the entire block is received, go to the next state;
                    if (p->inbuf == p->blocksize) p->state = check_block;
                }
                if (p->state == check_block){
                    fprintf(stderr, "checking_block  client \n"); 
                    char_block = p->current_block;
                    /*removes client if block number and inverse don't match or block number is not
                     * what was expected. however if the blocknum is a previously received block num, send ack*/
                    if (255 - p->buf[0] !=  p->buf[1]){
                        fprintf(stderr, "block number and inverse do not match\n");
                        removeclient(p->fd, &top);
                    }
                    else if (char_block > p->buf[0]){
                        temp = ACK;
                        if(write(p->fd, &temp, 1)<0){
                            perror("write");
                            removeclient(p->fd, &top);
                        }
                    }
                    else if (char_block != p->buf[0]){
                        fprintf(stderr, "char_block is not correct\n");
                        removeclient(p->fd, &top);
                    }
                    //otherwise, need to check crc
                    else{
                        block_crc = crc_message (XMODEM_KEY, &(p->buf[2]), p->blocksize - 4);
                        crc_first = block_crc>>8;
                        crc_second = block_crc;
                        if ((crc_first != p->buf[p->blocksize -2]) || (crc_second != p->buf[p->blocksize -1])){
                            fprintf(stderr, "crc does not match \n");
                            temp = NAK;
                            if(write(p->fd, &temp, 1) < 0){
                                perror("write");
                                removeclient(p->fd, &top);
                            }
                        }
                        else{
                            temp = ACK;
                            fprintf(stderr, "writing to client ACK\n");
                            if (write (p->fd, &temp, 1)<0){
                                perror("write");
                                removeclient(p->fd, &top);
                            }
                           
                            if(fwrite(&(p->buf[2]), p->blocksize-4, 1, p->fp)<0){
                                perror("write");
                                exit(1);
                            }
                            p->state = pre_block;
                            p->current_block ++;
                            p->inbuf = 0;
                            if(p->current_block > 255) p->current_block = 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}
