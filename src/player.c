#include "protocol.h"
#include "jeux_globals.h"
#include "player.h"
#include "math.h"
#include <stdlib.h>
#include "pthread.h"
#include "debug.h"

/*
 * A PLAYER represents a user of the system.  A player has a username,
 * which does not change, and also has a "rating", which is a value
 * that reflects the player's skill level among all players known to
 * the system.  The player's rating changes as a result of each game
 * in which the player participates.  PLAYER objects are managed by
 * the player registry.  So that a PLAYER object can be passed around
 * externally to the player registry without fear of dangling
 * references, it has a reference count that corresponds to the number
 * of references that exist to the object.  A PLAYER object will not
 * be freed until its reference count reaches zero.
 */
/* The initial rating assigned to a player. */
#define PLAYER_INITIAL_RATING 1500
/*
 * The PLAYER type is a structure type that defines the state of a player.
 * You will have to give a complete structure definition in player.c.
 * The precise contents are up to you.  Be sure that all the operations
 * that might be called concurrently are thread-safe.
 */
typedef struct player{
    char *username;
    int ref_count;
    double rating;
    pthread_mutex_t player_lock;
}PLAYER;

/*
 * Create a new PLAYER with a specified username.  A private copy is
 * made of the username that is passed.  The newly created PLAYER has
 * a reference count of one, corresponding to the reference that is
 * returned from this function.
 *
 * @param name  The username of the PLAYER.
 * @return  A reference to the newly created PLAYER, if initialization
 * was successful, otherwise NULL.
 */
PLAYER *player_create(char *name){
    PLAYER *player = calloc(1, sizeof(PLAYER));
    player->username = name;
    player->ref_count = 1;
    player->rating = PLAYER_INITIAL_RATING;
    pthread_mutex_init(&player->player_lock, NULL);
    return player;
}

/*
 * Increase the reference count on a player by one.
 *
 * @param player  The PLAYER whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same PLAYER object that was passed as a parameter.
 */
PLAYER *player_ref(PLAYER *player, char *why){
    pthread_mutex_lock(&player->player_lock);
    player->ref_count++;
    pthread_mutex_unlock(&player->player_lock);
    return player;
}

/*
 * Decrease the reference count on a PLAYER by one.
 * If after decrementing, the reference count has reached zero, then the
 * PLAYER and its contents are freed.
 *
 * @param player  The PLAYER whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 *
 */
void player_unref(PLAYER *player, char *why){
    pthread_mutex_lock(&player->player_lock);

    // debug("player ref count went from %d to %d because %s", player->ref_count, player->ref_count -1, why);
    player->ref_count--;
    if(player->ref_count == 0) {
        //free other like refcount?
        pthread_mutex_destroy(&player->player_lock);
        free(player->username);

        debug("about to free player");
        free(player);      
        return;
    }
    pthread_mutex_unlock(&player->player_lock);
    return;
}

/*
 * Get the username of a player.
 *
 * @param player  The PLAYER that is to be queried.
 * @return the username of the player.
 */
char *player_get_name(PLAYER *player){
    return player->username;
}

/*
 * Get the rating of a player.
 *
 * @param player  The PLAYER that is to be queried.
 * @return the rating of the player.
 */
int player_get_rating(PLAYER *player){
    return player->rating;
}
/*
 * Post the result of a game between two players.
 * To update ratings, we use a system of a type devised by Arpad Elo,
 * similar to that used by the US Chess Federation.
 * The player's ratings are updated as follows:
 * Assign each player a score of 0, 0.5, or 1, according to whether that
 * player lost, drew, or won the game.
 * Let S1 and S2 be the scores achieved by player1 and player2, respectively.
 * Let R1 and R2 be the current ratings of player1 and player2, respectively.
 * Let E1 = 1/(1 + 10**((R2-R1)/400)), and
 *     E2 = 1/(1 + 10**((R1-R2)/400))
 * Update the players ratings to R1' and R2' using the formula:
 *     R1' = R1 + 32*(S1-E1)
 *     R2' = R2 + 32*(S2-E2)
 *
 * @param player1  One of the PLAYERs that is to be updated.
 * @param player2  The other PLAYER that is to be updated.
 * @param result   0 if draw, 1 if player1 won, 2 if player2 won.
 */
void player_post_result(PLAYER *player1, PLAYER *player2, int result){
    double p1_score = 0;
    double p2_score = 0;
    switch(result){
        case 1:
            p1_score = 1;
            break;
        case 2:
            p2_score = 1;
            break;
        default:
            p2_score = 0.5;
            p1_score = 0.5;
            break;
    }
    if (player1 > player2) {
        pthread_mutex_lock(&player2->player_lock);
        pthread_mutex_lock(&player1->player_lock);
    }
    else {
        pthread_mutex_lock(&player1->player_lock);
        pthread_mutex_lock(&player2->player_lock);
    }
    double E1 = 1/(1 + pow(10, (player2->rating - player1->rating)/400) );
    player1->rating = player1->rating + 32 * (p1_score - E1);
    double E2 = 1/(1 + pow(10, (player1->rating - player2->rating)/400) );
    player2->rating = player2->rating + 32 * (p2_score - E2);
    if (player1 > player2) {
        pthread_mutex_unlock(&player1->player_lock);
        pthread_mutex_unlock(&player2->player_lock);
    }
    else {
        pthread_mutex_unlock(&player2->player_lock);
        pthread_mutex_unlock(&player1->player_lock);
    }
    return;
}
