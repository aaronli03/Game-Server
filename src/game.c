#include "jeux_globals.h"
#include <pthread.h>
#include "game.h"
#include "invitation.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct game{
    char** board;    
    GAME_ROLE player_role;
    GAME_ROLE game_winner;
    int over;
    int ref_count;
    pthread_mutex_t game_lock;
}GAME;

typedef struct game_move{
    int pos;
    int row;
    int col;
    GAME_ROLE player_role;
}GAME_MOVE;

GAME *game_create(void){
    GAME *game= calloc(1, sizeof(GAME));
    game->player_role = FIRST_PLAYER_ROLE;
    game->game_winner = NULL_ROLE;
    game->over = 0;
    pthread_mutex_init(&game->game_lock, NULL);
	game_ref(game, "Creating new game");
    char** board = calloc(3, sizeof(char *));
    for (int i = 0; i < 3; i++) {
        board[i] = calloc(3, sizeof(char));
        for (int j = 0; j < 3; j++) {
            board[i][j] = ' ';
        }
    }
    game->board = board; 
    return game;
};

GAME *game_ref(GAME *game, char *why){
    pthread_mutex_lock(&game->game_lock);
    game->ref_count++;
    pthread_mutex_unlock(&game->game_lock);
    return game;
}

void game_unref(GAME *game, char *why){
    pthread_mutex_lock(&game->game_lock);
    game->ref_count--;
    if(game->ref_count == 0) {
        for (int i = 0; i < 3; i++) {
            free(game->board[i]);
        }
        free(game->board);
        pthread_mutex_destroy(&game->game_lock);
        free(game);
        return;
    }
    pthread_mutex_unlock(&game->game_lock);
    return;
}

void win_check(GAME *game, char x, GAME_ROLE winner){
    int win = 0;
    int row;
    int col;
    // Check diagonals
    if (game->board[0][2] == x && game->board[0][2] == game->board[1][1] && game->board[1][1] == game->board[2][0]) {
        win = 1;
    }
    else if (game->board[0][0] == x && game->board[0][0] == game->board[1][1] && game->board[1][1] == game->board[2][2]) {
        win = 1;
    }
    // Check rows
    for (row = 0; row < 3; row++) {
        if (game->board[row][0] == x && game->board[row][0] == game->board[row][1] && game->board[row][1] == game->board[row][2]) {
            win = 1;
        }
    }
    // Check columns
    for (col = 0; col < 3; col++) {
        if (game->board[0][col] == x && game->board[0][col] == game->board[1][col] && game->board[1][col] == game->board[2][col]) {
            win = 1;
        }
    }
    if(!win){
        int draw = 1;
        //check for draw
        for (row = 0; row < 3; row++) {
            for (col = 0; col < 3; col++) {
                if (game->board[row][col] == ' ') {
                    draw = 0;
                    break;
                }
            }
        }
        if(draw){
            win = 2;
        }
    }
    if(win == 2) {
        game->game_winner = NULL_ROLE;
    }
    else if(win == 1){
        game->game_winner = winner;
    }
    else{
        return;
    }
    game->over = 1;
    return;
}

int game_apply_move(GAME *game, GAME_MOVE *move){
    pthread_mutex_lock(&game->game_lock);
    if (game->board[move->row][move->col] != ' ') {
        pthread_mutex_unlock(&game->game_lock);
        return -1;
    }
    GAME_ROLE curr_role = move->player_role;
    switch(curr_role){
        case FIRST_PLAYER_ROLE:
            game->board[move->row][move->col] = 'X';
            game->player_role = SECOND_PLAYER_ROLE;
            win_check(game, 'X', FIRST_PLAYER_ROLE);
            break;
        case SECOND_PLAYER_ROLE:
            game->board[move->row][move->col] = 'O';
            game->player_role = FIRST_PLAYER_ROLE;
            win_check(game, 'O', SECOND_PLAYER_ROLE);
            break;
        default:
            pthread_mutex_unlock(&game->game_lock);
            return -1;
    }
    pthread_mutex_unlock(&game->game_lock);
    return 0;

}

int game_resign(GAME *game, GAME_ROLE role) {
    if (game) {
        pthread_mutex_lock(&game->game_lock);
        if (game_is_over(game)) {
            pthread_mutex_unlock(&game->game_lock);
            return -1;
        }
        game->game_winner = (role == FIRST_PLAYER_ROLE) ? SECOND_PLAYER_ROLE : FIRST_PLAYER_ROLE;
        game->over = 1;
        pthread_mutex_unlock(&game->game_lock);
        return 0;
    }
    return -1;
}

char *game_unparse_state(GAME *game){
    char *buf;
    size_t s;
    FILE *stream = open_memstream(&buf, &s);
    char **board = game->board;
    fprintf(stream, "%s\n%c|%c|%c\n-----\n%c|%c|%c\n-----\n%c|%c|%c\n",
            "Game Board:",
            board[0][0], board[0][1], board[0][2],
            board[1][0], board[1][1], board[1][2],
            board[2][0], board[2][1], board[2][2]);

    fprintf(stream, "player %c turn\n", (game->player_role == FIRST_PLAYER_ROLE) ? 'X' : 'O');
    fclose(stream);
    return buf;
}

int game_is_over(GAME *game){
    return game->over;
}

GAME_ROLE game_get_winner(GAME *game){
    return game->game_winner;
}

GAME_MOVE *game_parse_move(GAME *game, GAME_ROLE role, char *str){
    if(game->player_role == role) {
        GAME_MOVE *game_move= calloc(1, sizeof(GAME_MOVE));
        char *endptr;
        unsigned long position = strtoul(str, &endptr, 10);
        game_move->row = (position-1)/3;
        game_move->col = (position-1)%3;
        game_move->pos = position;
        game_move->player_role = role;
        return game_move;
    }
    return NULL;
}

char *game_unparse_move(GAME_MOVE *move){
    char *buf;
    size_t s;
    FILE *stream = open_memstream(&buf, &s);
    fprintf(stream, "%d", move->pos);
    if(move->player_role != FIRST_PLAYER_ROLE) {
        fprintf(stream, "<O");
    }
    else {
        fprintf(stream, "<X");
    }
    fclose(stream);
    return buf;
}