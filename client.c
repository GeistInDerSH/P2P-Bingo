// System files
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Local files
#include "bingo.h"
#include "msg.h"

char name[20];
char my_name[20];

int **bingo_board;

int gen_ball = 0;
int has_winner = 0;
int match_count = 0;
int peer_num = 0;
int sock;

pthread_mutex_t print_lock;
pthread_mutex_t player_lock;

struct sockaddr_in my_addr;
struct sockaddr_in peer_list[255];
struct sockaddr_in server_address;

unsigned int game_number = 0;

// Functions in this file
void create_game_request();
void create_game_response(packet *new_packet);
void generate_ball();
void get_game_info();
void get_open_games(packet *new_packet);
void join_room_request(int new_game_number);
void join_game_response(packet *new_packet);
void leave_game_request();
void leave_game_response(packet *new_packet);
void parse_args(int argc, char **argv);
void player_connection_updates(packet *new_packet);
void print_name(packet *new_packet);
void *read_user_input(void *ptr);
void receive_message(struct sockaddr_in *from_addr, packet *new_packet);
void receive_packet();
void request_open_games();
void reply_to_ping(struct sockaddr_in *from_addr);
void send_message(char *msg);
void stop_generate_ball();

int main(int argc, char **argv) {
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "%s\n", "Failed to create socket");
        abort();
    }

    bingo_board = generate_board_values();

    parse_args(argc, argv);

    if (bind(sock, (struct sockaddr *)&my_addr, sizeof(my_addr))) {
        fprintf(stderr, "%s\n", "Failed to bind socket");
        abort();
    }

    // Thread for reading input
    pthread_t input_thread;
    pthread_create(&input_thread, NULL, read_user_input, NULL);
    pthread_detach(input_thread);

    receive_packet();
}

/* Parse Arguments
 *
 * Read in what the user enters
 */
void parse_args(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "%s\n%s\n", "Invalid format",
                "./client <Server IP Address> <Server Port> <Local Port> <Play "
                "Name>");
    }

    if (strlen(argv[4]) >= sizeof(name)) {
        fprintf(stderr, "%s\n%s\n", "Name is too long", "Exiting...");
        exit(1);
    } else {
        strcpy(name, argv[4]);
        strcpy(my_name, argv[4]);
    }

    // Get the server IP Address
    short server_ip = atoi(argv[2]);

    // Get my port number
    short my_port = atoi(argv[3]);
    char server_ip_addr[20];

    // Copy to the stack
    memcpy(server_ip_addr, argv[1],
           (strlen(argv[1]) + 1 > sizeof(server_ip_addr))
               ? sizeof(server_ip_addr)
               : strlen(argv[1]));

    // Save my local address
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    my_addr.sin_port = htons(my_port);

    // Set the server address
    server_address.sin_family = AF_INET;
    if (inet_aton(server_ip_addr, &server_address.sin_addr) == 0) {
        fprintf(stderr, "%s\n", "Failed to pasrse server IP Address");
        abort();
    }
    server_address.sin_port = htons(server_ip);
}

/* Read User Input
 *
 * Reads the user's input and executes
 * the command that corrisponds to that
 */
void *read_user_input(void *ptr) {
    // Read line buffer
    char read_line[1000];

    // Char for reading in
    char *ch;
    while (1) {
        // Place the read line buffer on the stack
        memset(read_line, 0, sizeof(read_line));

        // Read the line
        ch = fgets(read_line, sizeof(read_line), stdin);

        // Clear out an input that is too long
        if (ch == NULL) {
            pthread_mutex_lock(&print_lock);
            fprintf(stderr, "%s\n", "Cannot Read the Input");
            pthread_mutex_unlock(&print_lock);
            continue;
        }

        // Flush the input
        if (read_line[strlen(read_line) - 1] != '\n') {
            scanf("%*[^\n]");
            (void)getchar();
        }
        read_line[strlen(read_line) - 1] = '\0';

        // Ensure the first character is a dash
        if (read_line[0] != '-') {
            pthread_mutex_lock(&print_lock);
            fprintf(stderr, "%s\n", "Incorrect format");
            pthread_mutex_unlock(&print_lock);
            continue;
        }

        // Check based off the second character input
        // what action should be taken
        int new_game_number;
        switch (read_line[1]) {
            // 'c' - Create new game
            case 'c':
                create_game_request();
                break;
            // 'j' - Join game
            case 'j':
                new_game_number = atoi(read_line + 3);

                // Ensure the game is valid
                if (new_game_number < 0) {
                    pthread_mutex_lock(&print_lock);
                    fprintf(stderr, "%s\n", "Not a valid game");
                    pthread_mutex_unlock(&print_lock);

                } else {
                    join_room_request(new_game_number);
                }
                break;
            // 'l' - Leave (exit) game
            case 'l':
                leave_game_request();
                break;

            // 'r' - Request open games
            case 'q':
                request_open_games();
                break;

            // 'i' - Display game info
            case 'i':
                get_game_info();
                break;

            // 's' - Start game
            case 's':
                if (gen_ball == 1) {
                    gen_ball = 1;
                    generate_ball();
                } else {
                    gen_ball = 0;
                    stop_generate_ball();
                }
                break;

            // '?' - Help
            case '?':
                printf("\n\n-c : Create new game\n");
                printf("-j < game_number > : Join game\n");
                printf("-l : Leave game\n");
                printf("-q : Query open games\n");
                printf("-i : Display game info\n");
                printf("-s : Start or Stop the game\n\n");
                break;
            default:
                pthread_mutex_lock(&print_lock);
                fprintf(stderr, "%s\n%s", "Unknown Command Entered",
                        "Use -? for help\n\n");
                pthread_mutex_unlock(&print_lock);
                break;
        }
    }
    return NULL;
}

/* Receive Packet
 *
 * When the player gets a packet
 * inspect the header to determine
 * what action will be taken
 */
void receive_packet() {
    // Socket where the packet is from
    struct sockaddr_in from_addr;

    // Length of the address
    socklen_t addrlen = 10;

    // Make a new packet
    packet new_packet;

    while (1) {
        if (recvfrom(sock, &new_packet, sizeof(new_packet), 0,
                     (struct sockaddr *)&from_addr, &addrlen) == -1) {
            pthread_mutex_lock(&print_lock);
            fprintf(stderr, "%s\n",
                    "Ignoring packet that was failed to receive");
            pthread_mutex_unlock(&print_lock);
            continue;
        }

        // Check the header for what action to take
        switch (new_packet.header.msg_type) {
            case 'c':
                create_game_response(&new_packet);
                break;
            case 'j':
                join_game_response(&new_packet);
                break;
            case 'l':
                leave_game_response(&new_packet);
                break;
            case 'u':
                player_connection_updates(&new_packet);
                break;
            case 'r':
                get_open_games(&new_packet);
                break;
            case 'm':
                receive_message(&from_addr, &new_packet);
                break;
            case 'p':
                reply_to_ping(&from_addr);
                break;
            case 'g':
                gen_ball = 0;
                stop_generate_ball();
                break;
            case 'n':
                print_name(&new_packet);
                break;
            default:
                pthread_mutex_lock(&print_lock);
                fprintf(stderr, "%s\n", "Unknown Packet Received");
                pthread_mutex_unlock(&print_lock);
                break;
        }
    }
}

/* Create Game Request
 *
 * Request to make a new game
 */
void create_game_request() {
    // Generate new packet to send to the server
    packet new_packet;
    new_packet.header.msg_type = 'c';
    new_packet.header.msg_error = '\0';
    new_packet.header.msg_length = strlen(name);

    strcpy(new_packet.msg, name);

    // Try to send the packet to the server
    if (sendto(sock, &new_packet, sizeof(new_packet), 0,
               (struct sockaddr *)&server_address,
               sizeof(struct sockaddr_in)) == -1) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%s\n", "Failed to send packet to server");
        pthread_mutex_unlock(&print_lock);
    }
}

/* Join Game Request
 *
 * Request to join a new game
 */
void join_room_request(int new_game_number) {
    // Generate new packet to send to the server
    packet new_packet;
    new_packet.header.msg_type = 'j';
    new_packet.header.msg_error = '\0';
    new_packet.header.game = new_game_number;
    new_packet.header.msg_length = strlen(name);

    printf("Player Name:%s", name);
    strcpy(new_packet.msg, name);

    // Try and send the packet to the server
    if (sendto(sock, &new_packet, sizeof(new_packet), 0,
               (struct sockaddr *)&server_address,
               sizeof(struct sockaddr_in)) == -1) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%s\n", "Failed to send packet to server");
        pthread_mutex_unlock(&print_lock);
    }
}

/* Leave Game Request
 *
 * Try and leave the game
 */
void leave_game_request() {
    // Generate a new packet
    packet new_packet;
    new_packet.header.msg_type = 'l';
    new_packet.header.msg_error = '\0';
    new_packet.header.game = game_number;
    // new_packet.header.msg_length = 0;
    new_packet.header.msg_length = strlen(name);

    strcpy(new_packet.msg, name);

    // Try to send the packet to the server
    if (sendto(sock, &new_packet, sizeof(new_packet.header), 0,
               (struct sockaddr *)&server_address,
               sizeof(struct sockaddr_in)) == -1) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%s\n", "Failed to send packet to server");
        pthread_mutex_unlock(&print_lock);
    }
}

/* Send Message
 *
 * Send a message to all members
 * of the current game
 */
void send_message(char *msg) {
    // If there is no message
    if (msg[0] == '\0') {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%s\n", "There is not message");
        pthread_mutex_unlock(&print_lock);
        return;
    }

    // Wait 1 second before sending
    sleep(1);

    // Generate new packet
    packet new_packet;

    // If we are not drawing new balls
    // and there is not a winner
    if (gen_ball == 0 && has_winner != 1) {
        new_packet.header.msg_type = 'g';
    } else {
        new_packet.header.msg_type = 'm';
    }
    new_packet.header.msg_error = '\0';
    new_packet.header.game = game_number;
    new_packet.header.msg_length = strlen(msg) + 1;
    memcpy(new_packet.msg, msg, new_packet.header.msg_length);

    pthread_mutex_lock(&player_lock);

    // Send the message to every peer in the game
    for (int i = 0; i < peer_num; i++) {
        if (sendto(sock, &new_packet,
                   sizeof(new_packet.header) + new_packet.header.msg_length, 0,
                   (struct sockaddr *)&(peer_list[i]),
                   sizeof(struct sockaddr_in)) == -1) {
            pthread_mutex_lock(&print_lock);
            fprintf(stderr, "%s %d\n", "Failed to send message to peer", i);
            pthread_mutex_unlock(&print_lock);
        }
    }
    pthread_mutex_unlock(&player_lock);
}

/* Request Open Games
 *
 * Request a list of open games from the server
 */
void request_open_games() {
    // Generate new packet
    packet new_packet;
    new_packet.header.msg_type = 'r';
    new_packet.header.msg_error = '\0';
    new_packet.header.game = game_number;
    new_packet.header.msg_length = 0;

    // Try and send the packet to the server
    if (sendto(sock, &new_packet, sizeof(new_packet.header), 0,
               (struct sockaddr *)&server_address,
               sizeof(struct sockaddr_in)) == -1) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%s\n", "Failed to send packet to server");
        pthread_mutex_unlock(&print_lock);
    }
}

// local method that print out a list of all peer in the chatroom

/* Get Game Info
 *
 * Prints out all people that are in this game
 */
void get_game_info() {
    pthread_mutex_lock(&print_lock);

    // If you have no peers
    if (peer_num == 0) {
        fprintf(stderr, "%s\n", "You are not in a game");
    } else {
        printf("%s %d\n", "You are in game:", game_number);
        printf("%s\n", "member(s): ");
        char *ip_addr;
        short port;

        packet new_packet;
        new_packet.header.msg_type = 'n';
        new_packet.header.msg_error = '\0';

        for (int i = 0; i < peer_num; i++) {
            // Get the IP Address
            ip_addr = inet_ntoa(peer_list[i].sin_addr);
            // Get the port number
            port = htons(peer_list[i].sin_port);

            char ip_and_port[20];
            char *print_format = (char *)"%d:%d";
            sprintf(ip_and_port, print_format, ip_addr, port);

            new_packet.header.msg_length = strlen(ip_and_port);
            strcpy(new_packet.msg, ip_and_port);

            if (sendto(sock, &new_packet, sizeof(new_packet), 0,
                       (struct sockaddr *)&server_address,
                       sizeof(struct sockaddr_in)) == -1) {
                pthread_mutex_lock(&print_lock);
                fprintf(stderr, "%s\n", "Failed to get name of person");
                pthread_mutex_unlock(&print_lock);
            }
        }
    }
    pthread_mutex_unlock(&print_lock);
}

/* Create Game Response
 *
 * Try to create a new game
 *
 * If there are no errors create the game
 *
 * If there are errors, print out the error
 */
void create_game_response(packet *new_packet) {
    // If there is an error
    if (new_packet->header.msg_error != '\0') {
        pthread_mutex_lock(&print_lock);

        if (new_packet->header.msg_error == 'o') {
            fprintf(stderr, "%s\n", "Out of the game");

            // If you are already in a game
        } else if (new_packet->header.msg_error == 'e') {
            fprintf(stderr, "%s\n", "You are already in a game");
        } else {
            fprintf(stderr, "%s\n", "Unknown Error");
        }
        pthread_mutex_unlock(&print_lock);
        return;
    }

    pthread_mutex_lock(&player_lock);

    // Set the game number
    game_number = new_packet->header.game;

    // Because they may the game, they are the first person in it
    peer_num = 1;

    // Copy the peer list to memory
    memcpy(peer_list, &my_addr, sizeof(struct sockaddr_in));
    pthread_mutex_unlock(&player_lock);

    pthread_mutex_lock(&print_lock);
    printf("%s %d\n", "You made and joined game", game_number);
    pthread_mutex_unlock(&print_lock);
}

/* Join Game Response
 *
 * If the there is an error to the request
 * print out what kind of error there was
 *
 * If there is not an error, add the player
 * to the game
 */
void join_game_response(packet *new_packet) {
    // If there is an error
    if (new_packet->header.msg_error != '\0') {
        pthread_mutex_lock(&print_lock);

        // If the error is there is no more space
        if (new_packet->header.msg_error == 'f') {
            fprintf(stderr, "%s\n", "Game is full");

            // If the game does not exist
        } else if (new_packet->header.msg_error == 'e') {
            fprintf(stderr, "%s\n", "Game does not exist");

            // If you are alread in the game
        } else if (new_packet->header.msg_error == 'a') {
            fprintf(stderr, "%s\n", "You are already in that game");

            // Other error
        } else {
            fprintf(stderr, "%s\n", "Unkown Error");
        }
        pthread_mutex_unlock(&print_lock);
        return;
    }

    pthread_mutex_lock(&player_lock);

    // Set the current game to be the packets game
    game_number = new_packet->header.game;

    // New number of peers
    peer_num = new_packet->header.msg_length / sizeof(struct sockaddr_in);

    // If there are no peers
    if (peer_num <= 0) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%s\n", "Cannot join new game");
        pthread_mutex_unlock(&print_lock);

        // Remove the player from the game
        game_number = 0;
        peer_num = 0;
    } else {
        // Copy the peer list to memory
        memcpy(peer_list, new_packet->msg,
               peer_num * sizeof(struct sockaddr_in));

        pthread_mutex_lock(&print_lock);
        printf("%s %d\n", "You have joined game: ", game_number);
        pthread_mutex_unlock(&print_lock);
    }
    pthread_mutex_unlock(&player_lock);
}

/* Leave the Game Response
 *
 * Checks to see if there was an error leaving the game
 * If there was not, it removes the player from the game
 */
void leave_game_response(packet *new_packet) {
    // If there is an error
    if (new_packet->header.msg_error != '\0') {
        pthread_mutex_lock(&print_lock);

        // Check to see what type of error it is
        if (new_packet->header.msg_error == 'e') {
            fprintf(stderr, "%s\n", "You are not in a game");
        } else {
            fprintf(stderr, "%s\n", "Unkown Error");
        }
        pthread_mutex_unlock(&print_lock);
        return;

    } else {
        pthread_mutex_lock(&player_lock);
        // Set the current game to be none
        game_number = 0;
        // Set the number of peers to be none
        peer_num = 0;
        pthread_mutex_unlock(&player_lock);

        pthread_mutex_lock(&print_lock);
        printf("%s\n", "You have left the game");
        pthread_mutex_unlock(&print_lock);
    }
}

/* Play Connection Updates
 *
 * Checks to see if players are still connected
 */
void player_connection_updates(packet *new_packet) {
    pthread_mutex_lock(&player_lock);

    int new_peer_num =
        new_packet->header.msg_length / sizeof(struct sockaddr_in);

    // If there are no new peers
    if (new_peer_num <= 0) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%s\n", "Missing Peers");
        pthread_mutex_unlock(&print_lock);
    } else {
        pthread_mutex_lock(&print_lock);
        if (new_peer_num > peer_num) {
            printf("%s\n", "A new player has joined");
        } else {
            printf("%s\n", "A player has left");
        }
        pthread_mutex_unlock(&print_lock);

        // Set the new number of peers
        peer_num = new_peer_num;
        memcpy(peer_list, new_packet->msg,
               peer_num * sizeof(struct sockaddr_in));
    }
    pthread_mutex_unlock(&player_lock);
}

/* Get Open Games
 *
 * Gets a list of all open games
 */
void get_open_games(packet *new_packet) {
    pthread_mutex_lock(&print_lock);
    printf("Room List: \n%s", new_packet->msg);
    pthread_mutex_unlock(&print_lock);
}

/* Recieve Message
 *
 * Gets the message from the other players
 * Checks to see if the body of the message
 * is an int in the bingo board
 */
void receive_message(struct sockaddr_in *from_addr, packet *new_packet) {
    // Check to see if the game number for the
    // packet and the one received are the same
    if (new_packet->header.game == game_number) {
        pthread_mutex_lock(&print_lock);

        // If the message is not that there is a winner
        if (strcmp(new_packet->msg, "BINGO!") != 0) {
            // If the message is a match to the board
            if (is_match(bingo_board, atoi(new_packet->msg)) == 1) {
                // Increase the number of matches
                match_count++;

                printf("Match: %d\n", atoi(new_packet->msg));

                // 4 number of matches is the first time we can
                // have a winner
                if (match_count >= 4) {
                    // Check to see if theplayer is a winner
                    if (is_winner(bingo_board) == 1) {
                        // There is a winner
                        has_winner = 1;
                        printf("Player %d has won\n", my_addr.sin_addr.s_addr);

                        // There is a winner. Don't make new draws
                        stop_generate_ball();
                        return;
                    } else {
                        generate_ball();
                    }
                } else {
                    generate_ball();
                }
                // If there is a match, print out the board
                print_board(bingo_board);
            } else {
                generate_ball();
            }
        }
        printf("%s: %s\n", name, new_packet->msg);
        pthread_mutex_unlock(&print_lock);
    }
}

/* Reply to Ping
 *
 * Responds to a ping from the server
 */
void reply_to_ping(struct sockaddr_in *from_addr) {
    // Make packet to be sent
    packet new_packet;
    new_packet.header.msg_type = 'p';
    new_packet.header.msg_error = '\0';
    new_packet.header.msg_length = 0;

    // Try to send the packet
    if (sendto(sock, &new_packet, sizeof(new_packet.header), 0,
               (struct sockaddr *)from_addr,
               sizeof(struct sockaddr_in)) == -1) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%s\n", "Cound not respond to ping request");
        pthread_mutex_unlock(&print_lock);
    }
}

/* Generate balls
 *
 * Generate balls for the bing board
 */
void generate_ball() {
    if (gen_ball == 1) {
        int ball = call_ball();
        printf("Ball #:%d\n", ball);
        char ball_string[20];
        sprintf(ball_string, "%d", ball);

        send_message(ball_string);
    }
}

/* Stop Generate balls
 *
 * Stop generating new balls
 */
void stop_generate_ball() {
    bingo_board = generate_board_values();
    gen_ball = 0;
    char c[20];
    strcpy(c, "BINGO!");
    send_message(c);
}

/* Print Name
 *
 * Prints out the name that is sent
 * from the server
 */
void print_name(packet *new_packet) { printf("\t%s:\n", new_packet->msg); }