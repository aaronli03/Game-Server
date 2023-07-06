#include "player.h"
#include "jeux_globals.h"
#include "player_registry.h"
#include "pthread.h"
#include <string.h>
#include <stdlib.h>
/*
 * A player registry maintains a mapping from usernames to PLAYER objects.
 * Entries persist for as long as the server is running.
 */

/*
 * The PLAYER_REGISTRY type is a structure type that defines the state
 * of a player registry.  You will have to give a complete structure
 * definition in player_registry.c. The precise contents are up to
 * you.  Be sure that all the operations that might be called
 * concurrently are thread-safe.
 */
typedef struct player_node{
    PLAYER *player;
    struct player_node *next;
} PLAYER_NODE;

typedef struct player_registry{
    PLAYER_NODE *head;
    PLAYER_NODE *tail;
    size_t len;
    pthread_mutex_t lock;
}PLAYER_REGISTRY;

/*
 * Initialize a new player registry.
 *
 * @return the newly initialized PLAYER_REGISTRY, or NULL if initialization
 * fails.
 */
PLAYER_REGISTRY *preg_init(void) {
    PLAYER_REGISTRY *preg = calloc(1, sizeof(PLAYER_REGISTRY));
    preg->head = NULL;
    preg->tail = NULL;
    preg->len = 0;
    pthread_mutex_init(&preg->lock, NULL);
    return preg;
}

/*
 * Finalize a player registry, freeing all associated resources.
 *
 * @param cr  The PLAYER_REGISTRY to be finalized, which must not
 * be referenced again.
 */
void preg_fini(PLAYER_REGISTRY *preg) {
    PLAYER_NODE *curr = preg->head;
    while(curr){
        PLAYER_NODE *prev = curr;
        player_unref(curr->player, "preg_fini");
        curr = curr->next;
        free(prev);
    }
    pthread_mutex_destroy(&preg->lock);
    free(preg);
}

/*
 * Register a player with a specified user name.  If there is already
 * a player registered under that user name, then the existing registered
 * player is returned, otherwise a new player is created.
 * If an existing player is returned, then its reference count is increased
 * by one to account for the returned pointer.  If a new player is
 * created, then the returned player has reference count equal to two:
 * one count for the pointer retained by the registry and one count for
 * the pointer returned to the caller.
 *
 * @param name  The player's user name, which is copied by this function.
 * @return A pointer to a PLAYER object, in case of success, otherwise NULL.
 *
 */
PLAYER *preg_register(PLAYER_REGISTRY *preg, char *name) {
    pthread_mutex_lock(&preg->lock);
    //find if player exists
    PLAYER_NODE *curr = preg->head;
    while(curr){
        if(curr->player){
            if (strcmp(player_get_name(curr->player), name) == 0){
                player_ref(curr->player, "logging in as player");
                pthread_mutex_unlock(&preg->lock);
                free(name);
                return curr->player;
            }
        }
        curr = curr->next;
    }
    //add player
    PLAYER_NODE *new_player = (PLAYER_NODE*)malloc(sizeof(PLAYER_NODE));
    new_player->player = player_create(name);
    new_player->next = NULL;
    if(preg->head == NULL){
        preg->head = new_player;
        preg->tail = new_player;
    }
    else{
        preg->tail->next = new_player;
        preg->tail = new_player;
    }
    preg->len++;
    player_ref(new_player->player, "logging in as player");
    pthread_mutex_unlock(&preg->lock);
    return new_player->player;
}