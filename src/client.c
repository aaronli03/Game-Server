#include "protocol.h"
#include "player.h"
#include "client_registry.h"
#include "jeux_globals.h"
#include "client.h"
#include "game.h"
#include "invitation.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "pthread.h"
#include <unistd.h>
#include "debug.h"

typedef struct invitation_node {
    INVITATION *invitation;
    struct invitation_node *next;
} INVITATION_NODE;

typedef struct client{
	PLAYER *player;
	int ref_count; 
    int fd;
    INVITATION_NODE *head;
    INVITATION_NODE *tail;
    size_t len;
    pthread_mutex_t client_lock;
}CLIENT;

int get_invitation_index_by_client(CLIENT *client, INVITATION *inv) {
    if (!client) {
        return -1;
    }
    INVITATION_NODE *curr = client->head;
    int index = 0;
    while (curr) {
        if (curr->invitation == inv) {
            return index;
        }
        curr = curr->next;
        index++;
    }
    return -1;
}

INVITATION *get_invitation_by_index(CLIENT *client, int index) {
    if (!client || index < 0 || index >= client->len) {
        return NULL;
    }
    INVITATION_NODE *curr = client->head;
    int i = 0;
    while (curr) {
        if (i == index) {
            return curr->invitation;
        }
        curr = curr->next;
        i++;
    }
    return NULL;
}


CLIENT *client_create(CLIENT_REGISTRY *creg, int fd){
    CLIENT *client = (CLIENT *) calloc(1, sizeof(CLIENT));
    client->ref_count = 0;
    client->fd = fd;
    client->head = NULL;
    client->tail = NULL;
    client->len = 0;
    pthread_mutex_init(&client->client_lock, NULL);
    return client;
}

CLIENT *client_ref(CLIENT *client, char *why){
    pthread_mutex_lock(&client->client_lock);
    client->ref_count++;
    pthread_mutex_unlock(&client->client_lock);
    return client;
}

void client_unref(CLIENT *client, char *why){
	pthread_mutex_lock(&client->client_lock);
    client->ref_count--;
    if(client->ref_count == 0) {
        pthread_mutex_destroy(&client->client_lock);
        // close(client->fd);
        // debug("about to free client");
        free(client);
        return;
    }
    pthread_mutex_unlock(&client->client_lock);
	return;
}

int client_login(CLIENT *client, PLAYER *player){
	if (player) { //if there is a player
		pthread_mutex_lock(&client->client_lock);
		client->player = player; //assign player to login
		pthread_mutex_unlock(&client->client_lock);
		return 0;
    }
	return -1;
}

int client_logout(CLIENT *client){
	if(!client || !client->player){
		return -1;
	}
    INVITATION_NODE *curr = client->head;
    int index = 0;
    while(curr){
        if(inv_get_game(curr->invitation)){
            client_resign_game(client, index);
        }
        else if(inv_get_source(curr->invitation) == client){
            client_revoke_invitation(client, index);
        }
        else{
            client_decline_invitation(client, index);
        }
        index++;
        curr = curr->next;
    }
    pthread_mutex_lock(&client->client_lock);
    player_unref(client->player, "client logged out");
    client->player = NULL;
    pthread_mutex_unlock(&client->client_lock);
    return 0;
}

PLAYER *client_get_player(CLIENT *client){
    return client->player;
}

int client_get_fd(CLIENT *client){
	return client->fd;
}

void set_time(JEUX_PACKET_HEADER hdr){
	struct timespec current_time;
    uint32_t seconds, nanoseconds;
	clock_gettime(CLOCK_REALTIME, &current_time);
    nanoseconds = current_time.tv_nsec;
    hdr.timestamp_nsec = nanoseconds;
    seconds = current_time.tv_sec;
    hdr.timestamp_sec = seconds;
	return;
}

int client_send_packet(CLIENT *player, JEUX_PACKET_HEADER *pkt, void *data){
	CLIENT *client = player;
    pthread_mutex_lock(&client->client_lock);
	set_time(*pkt);
    proto_send_packet(client_get_fd(client), pkt, data);
    pthread_mutex_unlock(&client->client_lock);
    return 0;
}

int client_send_ack(CLIENT *client, void *data, size_t datalen){
	pthread_mutex_lock(&client->client_lock);
    JEUX_PACKET_HEADER hdr = {0};
    hdr.type = JEUX_ACK_PKT;
    hdr.size = htons(datalen);
	set_time(hdr);
    proto_send_packet(client_get_fd(client), &hdr, data);
    pthread_mutex_unlock(&client->client_lock);
    return 0;
}

int client_send_nack(CLIENT *client){
	pthread_mutex_lock(&client->client_lock);
    JEUX_PACKET_HEADER hdr = {0};
    hdr.type = JEUX_NACK_PKT;
    hdr.size = 0;
	set_time(hdr);
    proto_send_packet(client->fd, &hdr, NULL);
	//use client_send_packet?
    pthread_mutex_unlock(&client->client_lock);
    return 0;
}

int client_add_invitation(CLIENT *client, INVITATION *inv){
	pthread_mutex_lock(&client->client_lock);
    INVITATION_NODE *new_inv = (INVITATION_NODE*)malloc(sizeof(INVITATION_NODE));
    new_inv->invitation = inv;
    new_inv->next = NULL;
    if (client->head == NULL) {
        client->head = new_inv;
        client->tail = new_inv;
    } else {
        client->tail->next = new_inv;
        client->tail = new_inv;
    }
    client->len++;
    inv_ref(inv, "inv added");
    pthread_mutex_unlock(&client->client_lock);
    return 0;
}

int client_remove_invitation(CLIENT *client, INVITATION *inv){
    if(!client){
        return-1;
    }
	CLIENT *search = client;
	if(client != inv_get_source(inv)){
		search = inv_get_target(inv);
	}
    pthread_mutex_lock(&search->client_lock);
    INVITATION_NODE *curr = search->head;
    INVITATION_NODE *prev = NULL;
    while (curr) {
        if (curr->invitation == inv) {
            if (prev) {
                prev->next = curr->next;
            } else {
                search->head = curr->next;
            }
            if (!curr->next) {
                search->tail = prev;
            }
            free(curr);
            search->len--;
            inv_unref(inv, "inv removed from list");
            pthread_mutex_unlock(&search->client_lock);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&search->client_lock);
    return -1;
}

//check
int client_make_invitation(CLIENT *source, CLIENT *target, GAME_ROLE source_role, GAME_ROLE target_role){
    CLIENT *client = source;
    INVITATION *inv = inv_create(client, target, source_role, target_role);
	if (inv && (client_add_invitation(client, inv) == 0) && (client_add_invitation(target, inv) == 0)) {
		//first packet
		JEUX_PACKET_HEADER hdr_one = {0};
		hdr_one.type = JEUX_ACK_PKT;
        hdr_one.id = get_invitation_index_by_client(client, inv);
		client_send_packet(client, &hdr_one, NULL);
		//second packet
		JEUX_PACKET_HEADER hdr_two = {0};
		char *name = player_get_name(client_get_player(client));
		hdr_two.type = JEUX_INVITED_PKT;
		hdr_two.role = inv_get_target_role(inv);
		hdr_two.size = htons(strlen(name));
        hdr_two.id = get_invitation_index_by_client(target, inv);
		client_send_packet(target, &hdr_two, (void*) name);
		return 0;
	}
	return -1;
}

void remove_inv(CLIENT *client, INVITATION *inv){
    INVITATION_NODE *curr = client->head;
    INVITATION_NODE *prev = NULL;
    while (curr) {
        if (curr->invitation == inv) {
            if (prev) {
                prev->next = curr->next;
            } else {
                client->head = curr->next;
            }
            if (!curr->next) {
                client->tail = prev;
            }
            free(curr);
            inv_unref(inv, "inv removed");
            client->len--;
            return ;
        }
        prev = curr;
        curr = curr->next;
    }
    return;
}

int client_revoke_invitation(CLIENT *client, int id){
	int target_id;
	int exit = 1;
	INVITATION *inv = get_invitation_by_index(client, id);
    CLIENT *target = inv_get_target(inv);
    if (!inv || (inv_get_source(inv) != client) || !client_get_player(client) || inv_get_game(inv) || !target) {
        return -1;
    }
    target_id = get_invitation_index_by_client(target, inv);
    inv_close(inv, NULL_ROLE);
    if (client > target) {
		pthread_mutex_lock(&target->client_lock);
        pthread_mutex_lock(&client->client_lock);
    }
    else {
		pthread_mutex_lock(&client->client_lock);
        pthread_mutex_lock(&target->client_lock);
    }
    remove_inv(client, inv);
    INVITATION *inv_two = get_invitation_by_index(target, target_id);
    if (inv_two == inv) {
        remove_inv(target, inv);
		exit = 0;
    }
    if (client > target) {
		pthread_mutex_unlock(&client->client_lock);
        pthread_mutex_unlock(&target->client_lock);
    }
    else {
        pthread_mutex_unlock(&target->client_lock);
        pthread_mutex_unlock(&client->client_lock); 
    }
    if (!exit) {
        JEUX_PACKET_HEADER hdr = {0};
		hdr.type = JEUX_REVOKED_PKT;
		hdr.id = target_id;
		client_send_packet(target, &hdr, NULL);
		return 0;
    }
	return -1;
}

int client_decline_invitation(CLIENT *client, int id){
	int source_id;
	int exit = 1;
	INVITATION *inv = get_invitation_by_index(client, id);
    CLIENT *target = inv_get_target(inv);
    if (!inv || !client->player || (target != client) || inv_get_game(inv)) {
        return -1;
    }
    source_id = get_invitation_index_by_client(client, inv);
    inv_close(inv, NULL_ROLE);
    CLIENT *source = inv_get_source(inv);

    if (client > source) {
        pthread_mutex_lock(&source->client_lock);
        pthread_mutex_lock(&client->client_lock);
    }
    else {
        pthread_mutex_lock(&client->client_lock);
        pthread_mutex_lock(&source->client_lock);
    }
    remove_inv(client, inv);
    INVITATION *inv_two = get_invitation_by_index(source, source_id);
    if (inv_two == inv) {
        remove_inv(source, inv);
		exit = 0;
    }
    if (client > source) {
        pthread_mutex_unlock(&client->client_lock);
        pthread_mutex_unlock(&source->client_lock);
    }
    else {
        pthread_mutex_unlock(&source->client_lock);
        pthread_mutex_unlock(&client->client_lock);
    }
    if (!exit) {
		JEUX_PACKET_HEADER hdr = {0};
		hdr.type = JEUX_DECLINED_PKT;
		hdr.id = source_id;
		client_send_packet(source, &hdr, NULL);
        return 0;
    }
    return -1;
}

int client_accept_invitation(CLIENT *client, int id, char **strp){
	INVITATION *inv = get_invitation_by_index(client, id);
    if (!inv || !client->player || inv_accept(inv)) {
        return -1;
    }
    char *game_state = game_unparse_state(inv_get_game(inv));
    JEUX_PACKET_HEADER hdr ={0};
    hdr.type = JEUX_ACCEPTED_PKT;
    hdr.id = get_invitation_index_by_client(client, inv);
    if (inv_get_target_role(inv) == FIRST_PLAYER_ROLE) {
		client_send_packet(inv_get_source(inv), &hdr, NULL);
        *strp = game_state;
    }
    else {
        hdr.size = htons(strlen(game_state));
        client_send_packet(inv_get_source(inv), &hdr, game_state);   
        free(game_state);
    }
    return 0;
}

int client_resign_game(CLIENT *client, int id) {
    if (!client->player) {
        return -1;
    }
    INVITATION *inv = get_invitation_by_index(client, id);
    if(!inv){
        return -1;
    }
    int role;
	GAME_ROLE winner;
	CLIENT *target;
    int target_id;
    //identify role
    role = (inv_get_source(inv) != client) ? inv_get_target_role(inv) : inv_get_source_role(inv);
    target = (inv_get_source(inv) != client) ? inv_get_source(inv) : inv_get_target(inv);
    target_id = get_invitation_index_by_client(target, inv);
    if (inv_close(inv, role) || !inv_get_game(inv)) {
        return -1;
    }
    //identify winner
    winner = (inv_get_source(inv) != client) ? inv_get_source_role(inv) : inv_get_target_role(inv);
    player_post_result(client_get_player(inv_get_source(inv)), client_get_player(inv_get_target(inv)), winner);
    client_remove_invitation(target, inv);
	client_remove_invitation(client, inv);
    JEUX_PACKET_HEADER hdr = {0};
    hdr.type = JEUX_RESIGNED_PKT;
    hdr.id = target_id;
    client_send_packet(target, &hdr, NULL);
    return 0;
}
void display_game_results(INVITATION *inv, int winner){
    PLAYER *source_player = client_get_player(inv_get_source(inv));
    PLAYER *target_player = client_get_player(inv_get_target(inv));
    int target_role = inv_get_target_role(inv);
    if(winner == target_role){
        player_post_result(target_player, source_player, winner);
    }
    else{
        player_post_result(source_player, target_player, winner);
    }
    return;
}
int determine_winner(INVITATION *inv, GAME *game){
    int winner = game_get_winner(game);
    JEUX_PACKET_HEADER hdr = {0};
    hdr.type = JEUX_ENDED_PKT;
    hdr.role = winner;
    hdr.size = 0;
    hdr.id = get_invitation_index_by_client(inv_get_source(inv), inv);
    client_send_packet(inv_get_source(inv), &hdr, NULL);
    hdr.id = get_invitation_index_by_client(inv_get_target(inv), inv);
    client_send_packet(inv_get_target(inv), &hdr, NULL);
    return winner;
}

int client_make_move(CLIENT *client, int id, char *move){
	if (!client->player) {
        return -1;
    }
    //get invitation and game
    INVITATION *inv = get_invitation_by_index(client, id);
    GAME *game = inv_get_game(inv);
    if(!inv || !game) {
        return -1;
    }
	int role;
    CLIENT *target;
    int id_two;
    //identify client role and target client
    role = (inv_get_source(inv) != client) ? inv_get_target_role(inv) : inv_get_source_role(inv);
    target = (inv_get_source(inv) != client) ? inv_get_source(inv) : inv_get_target(inv);
    id_two = get_invitation_index_by_client(target, inv);

    //parse game move
    GAME_MOVE *game_move = game_parse_move(game, role, move);
    if (!game_move || game_apply_move(game, game_move) == -1) {
        return -1;
    }
    free(game_move);
    //send packet of updated game
    JEUX_PACKET_HEADER hdr = {0};
    hdr.type = JEUX_MOVED_PKT;
    hdr.id = id_two;
    char *game_state = game_unparse_state(game);
    hdr.size = htons(strlen(game_state));
    client_send_packet(target, &hdr, (void *)game_state);
    free(game_state);
    //check for winner if game is over
    if (game_is_over(game)) {
        display_game_results(inv, determine_winner(inv, game));
        client_remove_invitation(target, inv);
        client_remove_invitation(client, inv);
    }
    return 0;
}