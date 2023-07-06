/*
 * The CLIENT_REGISTRY type is a structure that defines the state of a
 * client registry.  You will have to give a complete structure
 * definition in client_registry.c.  The precise contents are up to
 * you.  Be sure that all the operations that might be called
 * concurrently are thread-safe.
 */
#include "pthread.h"
#include "csapp.h"
#include "debug.h"
#include "jeux_globals.h"
#include "client_registry.h"
#include "player_registry.h"
#include "invitation.h"
#include "client.h"
#include "player.h"

typedef struct client_node{
    CLIENT *client;
    struct client_node *next;
} CLIENT_NODE;

typedef struct client_registry{
    CLIENT_NODE *head;
    CLIENT_NODE *tail;
    int len;
    pthread_mutex_t len_lock;
    pthread_mutex_t registry_lock;
}CLIENT_REGISTRY;

CLIENT_REGISTRY *creg_init(){
    CLIENT_REGISTRY* cr = (CLIENT_REGISTRY *) calloc(1, sizeof(CLIENT_REGISTRY));
    cr->head = NULL;
    cr->tail = NULL;
    cr->len = 0;
    pthread_mutex_init(&cr->len_lock, NULL);
    pthread_mutex_init(&cr->registry_lock, NULL);
    return cr;
}

void creg_fini(CLIENT_REGISTRY *cr){
    CLIENT_NODE *curr = cr->head;
    while(curr){
        CLIENT_NODE *prev = curr;
        curr = curr->next;
        free(prev);
    }
    pthread_mutex_destroy(&cr->len_lock);

    pthread_mutex_destroy(&cr->registry_lock);
    free(cr);
    return;
}

CLIENT *creg_register(CLIENT_REGISTRY *cr, int fd){
    pthread_mutex_lock(&cr->registry_lock);

    pthread_mutex_lock(&cr->len_lock);
    if(cr->len == 64){
        pthread_mutex_unlock(&cr->len_lock);
        pthread_mutex_unlock(&cr->registry_lock);
        return NULL;
    }
    CLIENT_NODE *new_client = (CLIENT_NODE*)malloc(sizeof(CLIENT_NODE));
    new_client->client = client_create(cr, fd);
    new_client->next = NULL;
    if(cr->head == NULL){
        cr->head = new_client;
        cr->tail = new_client;
    }
    else{
        cr->tail->next = new_client;
        cr->tail = new_client;
    }
    cr->len++;
    client_ref(new_client->client, "logging in as client");
    pthread_mutex_unlock(&cr->len_lock);
    pthread_mutex_unlock(&cr->registry_lock);
    return new_client->client;
}

int creg_unregister(CLIENT_REGISTRY *cr, CLIENT *client){
    if(!cr || !client){
        return -1;
    }
    pthread_mutex_lock(&cr->registry_lock);
    CLIENT_NODE *curr = cr->head;
    CLIENT_NODE *prev = NULL;
    while(curr){
        if(curr->client == client){
            if (prev) {
                prev->next = curr->next;
            } else {
                cr->head = curr->next;
            }
            if (!curr->next) {
                cr->tail = prev;
            }
            free(curr);
            pthread_mutex_lock(&cr->len_lock);
            cr->len--;
            pthread_mutex_unlock(&cr->len_lock);

            client_unref(client, "client unregistered");
            pthread_mutex_unlock(&cr->registry_lock);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&cr->registry_lock);
    return -1;
}

CLIENT *creg_lookup(CLIENT_REGISTRY *cr, char *user){
    pthread_mutex_lock(&cr->registry_lock);
    CLIENT_NODE *curr = cr->head;
    CLIENT *curr_client = NULL;
    while(curr){
        curr_client = curr->client;
        if(curr_client){
            PLAYER *curr_player = client_get_player(curr_client);
            if(curr_player){
                if(strcmp(player_get_name(curr_player), user) == 0){
                    client_ref(curr_client, "creg_lookup");
                    break;
                }
            }
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&cr->registry_lock);
    return curr_client;
}

PLAYER **creg_all_players(CLIENT_REGISTRY *cr){
    pthread_mutex_lock(&cr->registry_lock);
    PLAYER **player_list = calloc(MAX_CLIENTS, sizeof(PLAYER *));
    int index = 0;
    CLIENT_NODE *curr = cr->head;
    while(curr){
        CLIENT *curr_client = curr->client;
        if(curr_client){
            PLAYER *curr_player = client_get_player(curr_client);
            if(curr_player){
                player_ref(curr_player, "added to list of players");
                player_list[index++] = curr_player;
            }
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&cr->registry_lock);
    return player_list;
}

void creg_wait_for_empty(CLIENT_REGISTRY *cr){
    while(1){
        pthread_mutex_lock(&cr->registry_lock);

        if(cr->len == 0){
            break;
        }
        pthread_mutex_unlock(&cr->registry_lock);

    }
    pthread_mutex_unlock(&cr->registry_lock);

    return;
}

void creg_shutdown_all(CLIENT_REGISTRY *cr){
    pthread_mutex_lock(&cr->registry_lock);
    CLIENT_NODE *curr = cr->head;
    // CLIENT_NODE *prev = NULL;
    while(curr){
        // prev = curr;
        // curr = curr->next;
        // shutdown(client_get_fd(prev->client), SHUT_RD);
        shutdown(client_get_fd(curr->client), SHUT_RD);
        curr = curr->next;
    }
    pthread_mutex_unlock(&cr->registry_lock);
    return;
}