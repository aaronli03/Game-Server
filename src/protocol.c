#include "protocol.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include "jeux_globals.h"
#include "csapp.h"

int proto_send_packet(int fd, JEUX_PACKET_HEADER *hdr, void *data) {
    size_t header_size = sizeof(JEUX_PACKET_HEADER);
    void *header = (void *)hdr;
    rio_writen(fd, header, header_size);
    if (data) {
        rio_writen(fd, data, ntohs(hdr->size));
    }
    return 0;
}

int proto_recv_packet(int fd, JEUX_PACKET_HEADER *hdr, void **payloadp){
    size_t header_size = sizeof(JEUX_PACKET_HEADER);
    ssize_t n = header_size;
    void *header = (void *) hdr;
    n = rio_readn(fd, header, header_size);
    if (n == -1) {
        return -1;
    }
    else if (n == -2) {
        hdr->type = JEUX_NO_PKT;
        return 0;
    }

    if (hdr->size) {
        char *payload = malloc(ntohs(hdr->size));
        n = rio_readn(fd, payload, ntohs(hdr->size));
        if (n == -1) {
            free(payload);
            return -1;
        }
        else if (n == -2) {
            free(payload);
            hdr->type = JEUX_NO_PKT;
            return 0;
        }
        *payloadp = payload;
    }
    return 0; 
}