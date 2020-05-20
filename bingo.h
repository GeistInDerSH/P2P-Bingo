#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int all_used_num[75];
int last_used = 0;
int num_match = 0;

int called_balls[75];
int called_count = 0;

/* Checks if the user is a host
 * 0 -> user is not the host
 * 1 -> user is the host
 */
int is_host = 0;

/* Alocates memory for the new users
 * Bingo board
 */
int **make_board() {
    int *brd = (int *)calloc(25, sizeof(int));
    int **rows = (int **)malloc(5 * sizeof(int *));

    for (int i = 0; i < 5; i++) {
        rows[i] = brd + i * 5;
    }

    return rows;
}

/* Remove Board
 * Dealocates the memory associated
 * with the board
 */
void remove_board(int **brd) {
    free(*brd);
    free(brd);
}

/* Check to see if the number has been used
 * on the bingo board before
 */
int is_used(int ball_num) {
    for (int i = 0; i < last_used; i++) {
        if (ball_num == all_used_num[i]) {
            return 1;
        }
    }

    return 0;
}

/* Resets the used numbers
 */
void reset_used() {
    for (int i = 0; i < last_used; i++) {
        all_used_num[i] = 0;
    }

    last_used = 0;

    for (int i = 0; i < called_count; i++) {
        called_balls[i] = 0;
    }

    called_count = 0;
}

/* Builds the BINGO board at the start of the game
 *
 * B is between 1 and 15
 * I is between 16 and 30
 * N is between 31 and 45
 * G is between 46 and 60
 * O is between 61 and 75
 */
int **generate_board_values() {
    int **bingo = make_board();

    // Set the random number to be based on the current time
    srand(time(0));

    for (int i = 0; i < 5; i++) {
        /* Generate 5 random variables based on the
         * possible ranges of their respective rows
         */
        int r1 = rand() % 15 + 1;
        int r2 = rand() % 15 + 16;
        int r3 = rand() % 15 + 31;
        int r4 = rand() % 15 + 46;
        int r5 = rand() % 15 + 61;

        // Check to see if the variable has been used
        while (is_used(r1) == 1) {
            // If it has, generate a new random num
            r1 = rand() % 15 + 1;
        }
        // Add the unused num to the list of used num
        all_used_num[last_used] = r1;
        // Increment the nubmer of spots used
        last_used++;

        while (is_used(r2) == 1) {
            r2 = rand() % 15 + 16;
        }
        all_used_num[last_used] = r2;
        last_used++;

        while (is_used(r3) == 1) {
            r3 = rand() % 15 + 31;
        }
        all_used_num[last_used] = r3;
        last_used++;

        while (is_used(r4) == 1) {
            r4 = rand() % 15 + 46;
        }
        all_used_num[last_used] = r4;
        last_used++;

        while (is_used(r5) == 1) {
            r5 = rand() % 15 + 61;
        }
        all_used_num[last_used] = r5;
        last_used++;

        bingo[i][0] = r1;
        bingo[i][1] = r2;
        bingo[i][2] = r3;
        bingo[i][3] = r4;
        bingo[i][4] = r5;
    }

    // Set the middle of the board to be the 'free space'
    bingo[2][2] = 0;

    // Reset the used array
    reset_used();

    return bingo;
}

/* Checks to see if the board has a winner
 */
int is_winner(int **board) {
    // Check to see if there is a full row of 0's
    for (int i = 0; i < 5; i++) {
        if (board[i][0] == 0 && board[i][1] == 0 && board[i][2] == 0 &&
            board[i][3] == 0 && board[i][4] == 0) {
            printf("Row %d is a winner\n", i + 1);
            return 1;
        }
    }

    // Check to see if there is a full col of 0's
    for (int i = 0; i < 5; i++) {
        if (board[0][i] == 0 && board[1][i] == 0 && board[2][i] == 0 &&
            board[3][i] == 0 && board[4][i] == 0) {
            printf("Col %d is a winner\n", i);
            return 1;
        }
    }

    /* Check the L-Top to R-Bot diagonal
     * is all 0
     */
    if (board[0][0] == 0 && board[1][1] == 0 && board[2][2] == 0 &&
        board[3][3] == 0 && board[4][4] == 0) {
        printf("L-Top to R-Bot is a winner\n");
        return 1;

        // Check the R-Top to L-Bot diagonal is all 0
    } else {
        if (board[0][4] == 0 && board[1][3] == 0 && board[2][2] == 0 &&
            board[3][1] == 0 && board[4][0] == 0) {
            printf("R-Top to L-Bot is a winner\n");
            return 1;
        }
    }
    // False
    return 0;
}

/* Checks to see if the number that was called
 * matches one that is in the board
 */
int is_match(int **board, int called_number) {
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            if (board[i][j] == called_number) {
                board[i][j] = 0;
                num_match++;
                return 1;
            }
        }
    }
    return 0;
}

/* Is Called
 *
 * Check to see if the given ball
 * has been called before
 */
int is_called(int ball) {
    for (int i = 0; i < called_count; i++) {
        if (ball == called_balls[i]) {
            return 1;
        }
    }

    return 0;
}

/* Call Ball
 *
 * Outputs the next ball that has not been used
 */
int call_ball() {
    // Set the random number to be based on the current time
    srand(time(0));
    int ball = rand() % 75 + 1;

    if (called_count == 75) {
        return -1;
    }

    while (is_called(ball) == 1) {
        ball = rand() % 75 + 1;
    }
    // printf("\t\t\tcalled #: %d\n", called_count);
    called_count++;
    return ball;
}

/* Prints out the player's board
 */
void print_board(int **board) {
    printf(" \tB\t\tI\t\tN\t\tG\t\tO\t\n");
    printf(
        "======================================================================"
        "===========\n");
    printf("|\t%d\t|\t%d\t|\t%d\t|\t%d\t|\t%d\t|\n", board[0][0], board[0][1],
           board[0][2], board[0][3], board[0][4]);
    printf("|\t%d\t|\t%d\t|\t%d\t|\t%d\t|\t%d\t|\n", board[1][0], board[1][1],
           board[1][2], board[1][3], board[1][4]);
    printf("|\t%d\t|\t%d\t|\t%d\t|\t%d\t|\t%d\t|\n", board[2][0], board[2][1],
           board[2][2], board[2][3], board[2][4]);
    printf("|\t%d\t|\t%d\t|\t%d\t|\t%d\t|\t%d\t|\n", board[3][0], board[3][1],
           board[3][2], board[3][3], board[3][4]);
    printf("|\t%d\t|\t%d\t|\t%d\t|\t%d\t|\t%d\t|\n", board[4][0], board[4][1],
           board[4][2], board[4][3], board[4][4]);
}