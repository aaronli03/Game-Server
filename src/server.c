#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "debug.h"
#include "jeux_globals.h"
#include <string.h>


void send_invite(CLIENT *client, char *name, int role, size_t len){
    GAME_ROLE src_role;
    GAME_ROLE target_role;
    PLAYER *player = client_get_player(client);
    char *nameCopy = (char *)calloc(1, len + 1);
    strncpy(nameCopy, name, len);
    CLIENT *target = creg_lookup(client_registry, nameCopy);
    free(nameCopy);
    if(!target || !player || ((role != FIRST_PLAYER_ROLE) && (role != SECOND_PLAYER_ROLE))){
        client_send_nack(client);
        return;
    }
    if (role == SECOND_PLAYER_ROLE) {
        src_role = FIRST_PLAYER_ROLE;
        target_role =  SECOND_PLAYER_ROLE;
    }
    else {
        src_role = SECOND_PLAYER_ROLE;
        target_role = FIRST_PLAYER_ROLE;
    }
    if (client_make_invitation(client, target, src_role, target_role) == -1) {
        client_send_nack(client);
    }
    return;
}

void show_users(CLIENT *client){
    size_t s;
    char *buf;
    PLAYER **player_list = creg_all_players(client_registry);
    FILE *stream = open_memstream(&buf, &s);
    int index = 0;
    PLAYER *player = player_list[index];
    while(player){
        fprintf(stream, "%s\t%d\n", player_get_name(player), player_get_rating(player));
        player_unref(player, "player list printed");
        player_list[index] = NULL;
        player = player_list[++index];
    }
    free(player_list);
    fclose(stream);
    client_send_ack(client, (void *) buf, s);
    free(buf);
    return;
}

void login(CLIENT *client, char *name, size_t len) {
    if(client_get_player(client)){
        client_send_nack(client);
        return;
    }
    char *nameCopy = (char *)calloc(1, len + 1);
    strncpy(nameCopy, name, len);
    PLAYER *player = preg_register(player_registry, nameCopy);
    if (client_login(client, player) == 0) { //success
        client_send_ack(client, NULL, 0);
        // free(nameCopy);
    }
    else{ //fail
        free(nameCopy);
        client_send_nack(client);
    }
    return;
}

void* jeux_client_service(void *vargp) {
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self()); 
    free(vargp);
    CLIENT *client = creg_register(client_registry, connfd);  
    while (1) {
        char *payload = NULL;
        char *name;
        JEUX_PACKET_HEADER header = {0};
        JEUX_PACKET_HEADER *hdr = &header;
        proto_recv_packet(connfd, hdr, (void **)&payload);
        JEUX_PACKET_TYPE type = hdr->type;
        
        hdr->size = ntohs(hdr->size);
        // debug("type: %d\n", type);
        switch(type){
            case JEUX_LOGIN_PKT:
                name = payload;
                login(client, name, hdr->size);
                break;
            case JEUX_USERS_PKT:
                show_users(client);
                break;
            case JEUX_INVITE_PKT:
                name = payload;
                send_invite(client, name, hdr->role, hdr->size);
                break;
            case JEUX_REVOKE_PKT:
                if (client_revoke_invitation(client, hdr->id) == -1) {
                    client_send_nack(client);
                }
                else {
                    client_send_ack(client, NULL, 0);
                }
                break;
            case JEUX_ACCEPT_PKT:
                char *strp = NULL;
                if (client_accept_invitation(client, hdr->id, &strp) == -1) {
                    client_send_nack(client);
                }
                else {
                    if (strp) {
                        client_send_ack(client, (void *) strp, strlen(strp));
                        free(strp);
                    }
                    else {
                        client_send_ack(client, NULL, 0);
                    }
                }
                break;
            case JEUX_DECLINE_PKT:
                if (client_decline_invitation(client, hdr->id) == -1) {
                    client_send_nack(client);
                }
                else {
                    client_send_ack(client, NULL, 0);
                }
                break;
            case JEUX_MOVE_PKT:
                if (client_make_move(client, hdr->id, payload) == -1)  {
                    client_send_nack(client);
                }
                else {
                    client_send_ack(client, NULL, 0);
                }
                break;
            case JEUX_RESIGN_PKT:
                if (client_resign_game(client, hdr->id) == -1)  {
                    client_send_nack(client);
                }
                else {
                    client_send_ack(client, NULL, 0);
                } 
                break;
            default:
                // eof
                close(client_get_fd(client));

                client_logout(client);
                creg_unregister(client_registry, client);
                // possibly need to client_unref
                if(payload) {
                    free(payload);
                }
                return NULL; 
        }
        if(payload) {
            free(payload);
            payload = NULL;
        }
    }
    return NULL;
}