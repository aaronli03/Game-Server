#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "debug.h"
#include "protocol.h"
#include "server.h"
#include "client_registry.h"
#include "player_registry.h"
#include "jeux_globals.h"
#include "csapp.h"

#ifdef DEBUG
int _debug_packets_ = 1;
#endif

volatile sig_atomic_t sighup_flag = 0;

static void terminate(int status);
void sighup_handler(int signum, siginfo_t *siginfo, void *context);
void sighup_handler(int signum, siginfo_t *siginfo, void *context){
    sighup_flag = 1;
}
/*
 * "Jeux" game server.
 *
 * Usage: jeux <port>
 */
int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.
    char *PORT = NULL;
    if(argc < 3 || strcmp(argv[1], "-p") != 0){
        return EXIT_FAILURE;
    }
    PORT = argv[2];
    // Perform required initializations of the client_registry and
    // player_registry.
    client_registry = creg_init();
    player_registry = preg_init();

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function jeux_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.

    struct sigaction sighup = {0};
    sighup.sa_sigaction = sighup_handler;
    sigemptyset(&sighup.sa_mask);
    sighup.sa_flags = 0;
    sigaction(SIGHUP, &sighup, NULL);
    int listen_fd;
    struct sockaddr_storage client_addr;
    socklen_t client_len;
    listen_fd = Open_listenfd(PORT);
    // debug("Listening on port %s\n", PORT);
    while(1){
        client_len = sizeof(struct sockaddr_storage);
        int *conn_fd = malloc(sizeof(int));
        *conn_fd = accept(listen_fd, (SA *)&client_addr, &client_len);

        if(sighup_flag) {
            terminate(0);
        }
        pthread_t thread;
        pthread_create(&thread, NULL, jeux_client_service, conn_fd);
    }
    terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
void terminate(int status) {
    creg_shutdown_all(client_registry);
    creg_wait_for_empty(client_registry);
    creg_fini(client_registry);
    preg_fini(player_registry);
    exit(status);
}
