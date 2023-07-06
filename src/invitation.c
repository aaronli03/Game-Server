#include "client_registry.h"
#include "client.h"
#include "player.h"
#include "game.h"
#include "jeux_globals.h"
#include "invitation.h"
#include <stdlib.h>
#include <pthread.h>

/*
 * An INVITATION records the status of an offer, made by one CLIENT
 * to another, to participate in a GAME.  The CLIENT that initiates
 * the offer is called the "source" of the invitation, and the CLIENT
 * that is the recipient of the offer is called the "target" of the
 * invitation.  An INVITATION stores references to its source and
 * target CLIENTs, whose reference counts reflect the existence of
 * these references.  At any time, an INVITATION may be in one of
 * three states: OPEN, ACCEPTED, or CLOSED.  A newly created INVITATION
 * starts out in the OPEN state.  An OPEN invitation may be "accepted"
 * or "declined" by its target.  It may also be "revoked" by its
 * source.  An invitation that has been accepted by its target transitions
 * to the ACCEPTED state.  In association with such a transition a new
 * GAME is created and a reference to it is stored in the INVITATION.
 * An invitation that is declined by its target or revoked by its
 * source transitions to the CLOSED state.  An invitation in the
 * ACCEPTED state will also transition to the CLOSED state when the
 * game in progress has ended.
 */

/*
 * The INVITATION type is a structure type that defines the state of
 * an invitation.  You will have to give a complete structure
 * definition in invitation.c.  The precise contents are up to you.
 * Be sure that all the operations that might be called concurrently
 * are thread-safe.
 */
typedef struct invitation{
    int ref_count;
    int source_id;
    int target_id;
    CLIENT *source;
    CLIENT *target;
    int source_role;
    int target_role;
    GAME *game;
    INVITATION_STATE state;
    pthread_mutex_t invitation_lock;
}INVITATION;

/*
 * Create an INVITATION in the OPEN state, containing reference to
 * specified source and target CLIENTs, which cannot be the same CLIENT.
 * The reference counts of the source and target are incremented to reflect
 * the stored references.
 *
 * @param source  The CLIENT that is the source of this INVITATION.
 * @param target  The CLIENT that is the target of this INVITATION.
 * @param source_role  The GAME_ROLE to be played by the source of this INVITATION.
 * @param target_role  The GAME_ROLE to be played by the target of this INVITATION.
 * @return a reference to the newly created INVITATION, if initialization
 * was successful, otherwise NULL.
 */
INVITATION *inv_create(CLIENT *source, CLIENT *target, GAME_ROLE source_role, GAME_ROLE target_role) {
	if(source == target){
		return NULL;
	}
	INVITATION *invite = calloc(1, sizeof(INVITATION));
	pthread_mutex_init(&invite->invitation_lock, NULL);
	invite->source_role = source_role;
    invite->source = source;
	invite->target_role = target_role;
    invite->target = target;
	inv_ref(invite, "invitation is created");
	client_ref(source, "client is the source of new inv");
	client_ref(target, "client is the target of new inv");
	invite->state = INV_OPEN_STATE;
	return invite;
}

INVITATION *inv_ref(INVITATION *inv, char *why) {
	pthread_mutex_lock(&inv->invitation_lock);
	inv->ref_count++;
	pthread_mutex_unlock(&inv->invitation_lock);
	return inv;
}

void inv_unref(INVITATION *inv, char *why) {
	pthread_mutex_lock(&inv->invitation_lock);
	inv->ref_count--;
	if (inv->ref_count != 0) {
		pthread_mutex_unlock(&inv->invitation_lock);
    	return;
	}
	if (inv->source) {
		client_unref(inv->source, "inv freed");
	}
	if (inv->target) {
		client_unref(inv->target, "inv freed");
	}
	if (inv->game) {
		game_unref(inv->game, "inv of game freed");
		inv->game = NULL;
	}
	pthread_mutex_destroy(&inv->invitation_lock);
	free(inv);
	pthread_mutex_unlock(&inv->invitation_lock);
	return;
}

CLIENT *inv_get_source(INVITATION *inv) {
	return inv->source;
}

CLIENT *inv_get_target(INVITATION *inv) {
	return inv->target;
}

GAME_ROLE inv_get_source_role(INVITATION *inv) {
	return inv->source_role;
}

GAME_ROLE inv_get_target_role(INVITATION *inv) {
	return inv->target_role;
}

GAME *inv_get_game(INVITATION *inv) {
	return inv->game;
}

/*
 * Accept an INVITATION, changing it from the OPEN to the
 * ACCEPTED state, and creating a new GAME.  If the INVITATION was
 * not previously in the the OPEN state then it is an error.
 *
 * @param inv  The INVITATION to be accepted.
 * @return 0 if the INVITATION was successfully accepted, otherwise -1.
 */
int inv_accept(INVITATION *inv) {
	pthread_mutex_lock(&inv->invitation_lock);
	if (inv->state == INV_OPEN_STATE) {
        inv->state = INV_ACCEPTED_STATE;
        GAME *game = game_create();
        inv->game = game;
        pthread_mutex_unlock(&inv->invitation_lock);
        return 0;
	}
    pthread_mutex_unlock(&inv->invitation_lock);
	return -1;
}

/*
 * Close an INVITATION, changing it from either the OPEN state or the
 * ACCEPTED state to the CLOSED state.  If the INVITATION was not previously
 * in either the OPEN state or the ACCEPTED state, then it is an error.
 * If INVITATION that has a GAME in progress is closed, then the GAME
 * will be resigned by a specified player.
 *
 * @param inv  The INVITATION to be closed.
 * @param role  This parameter identifies the GAME_ROLE of the player that
 * should resign as a result of closing an INVITATION that has a game in
 * progress.  If NULL_ROLE is passed, then the invitation can only be
 * closed if there is no game in progress.
 * @return 0 if the INVITATION was successfully closed, otherwise -1.
 */
int inv_close(INVITATION *inv, GAME_ROLE role) {
	pthread_mutex_lock(&inv->invitation_lock);
    //if already closed state
	if (inv->state == INV_CLOSED_STATE) {
		
		pthread_mutex_unlock(&inv->invitation_lock);
		return -1;
	}
    //if there is a game in progress, close inv and game
	if(role != NULL_ROLE) {
		if (game_resign(inv->game, role)) {
            pthread_mutex_unlock(&inv->invitation_lock);
			return -1;
		}
	}
    //if no game is in progress close inv
	inv->state = INV_CLOSED_STATE;
	pthread_mutex_unlock(&inv->invitation_lock);
	return 0;
}