// System files
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
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
#include "msg.h"
#include "uthash.h"

// Hard set values
#define MAX_GAMES 20
#define MAX_PLAYERS 20

struct peer {
    char name[20];
    char ip_and_port[20];
    unsigned int game;
    short status;
    UT_hash_handle hh;
};

// Globals
struct peer *all_peers;
int sock;
int status_sock;
pthread_mutex_t print_lock;
pthread_mutex_t peers_lock;

// // Function Prototypes
short parse_arguments(int argc, char **argv);
void *out(void *ptr);
void *inp(void *ptr);
void mark_peer_alive(unsigned int ip_addr, short port);
void ping_players();
void remove_inactive_players();
void create_game(unsigned int ip_addr, short port, char *name);
void join_game(unsigned int ip_addr, short port, unsigned int game, char *name);
void leave_game(unsigned int ip_addr, short port);
void list_games(unsigned int ip_addr, short port);
void peer_list(unsigned int join_ip, short join_port, unsigned int game,
               char *name);
void send_error(unsigned int ip_addr, short port, char msg_type,
                char msg_error);
void get_player_name(unsigned long ip_addr, short port);

int get_number_of_games();
struct sockaddr_in get_sockaddr_in(unsigned int ip_addr, short port);
unsigned int get_ip(char *ip_port);
short get_port(char *ip_port);

/* Input
 */
void *inp(void *ptr) {
    socklen_t addrlen = 10;
    struct sockaddr_in sender_addr;
    packet get_packet;

    while (1) {
        // check ping socket - mark sender status

        // Check to see if the packet was not sucessfuly received
        if (recvfrom(status_sock, &get_packet, sizeof(get_packet), 0,
                     (struct sockaddr *)&sender_addr, &addrlen) == -1) {
            pthread_mutex_lock(&print_lock);
            fprintf(stderr, "%p\n", "ignoring Packet that failed to receive");
            pthread_mutex_unlock(&print_lock);
        } else {
            // Get the IP Address and port
            unsigned int ip_addr = sender_addr.sin_addr.s_addr;
            short port = htons(sender_addr.sin_port);

            // Use the packet header to determine what to do
            switch (get_packet.header.msg_type) {
                // Player responded to ping
                case 'p':
                    mark_peer_alive(ip_addr, port);
                    break;
                default:
                    pthread_mutex_lock(&print_lock);
                    fprintf(stderr, "%p\n", "Received unknown packet");
                    pthread_mutex_unlock(&print_lock);
                    break;
            }
        }
    }
    return NULL;
}

/* Out
 */
void *out(void *ptr) {
    clock_t current_time;
    ping_players();
    current_time = clock();
    while (1) {
        // If the time to respond is longer than 60 seconds
        if ((float)(clock() - current_time) / CLOCKS_PER_SEC >= 60) {
            pthread_mutex_lock(&print_lock);
            fprintf(stderr, "%p\n", "Checking for pings");
            pthread_mutex_unlock(&print_lock);

            // Remove any players that did not respond
            remove_inactive_players();
            // recheck player status
            ping_players();
            current_time = clock();
        }
    }
    return NULL;
}

/* Mark Peers as Alive
 *
 * Sets the peer's status to be alive
 * so that they will not be reomved
 */
void mark_peer_alive(unsigned int ip_addr, short port) {
    // Make and alocate memory for a char array
    // of the IP Address and port number
    char ip_port[20];
    memset(ip_port, 0, sizeof(ip_port));

    // Set the print out format
    char *print_format = (char *)"%d:%d";
    sprintf(ip_port, print_format, ip_addr, port);

    struct peer *p;
    // Find the peer in the hash table
    HASH_FIND_STR(all_peers, ip_port, p);
    // If the peer is found
    if (p != NULL) {
        // Set the peer to be alive
        pthread_mutex_lock(&peers_lock);
        p->status = 1;
        pthread_mutex_unlock(&peers_lock);
    } else {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%p\n", "Peer did not respond to ping request");
        pthread_mutex_unlock(&print_lock);
    }
}

/* Ping Players
 *
 * Ping players to see if they are
 * active or inactive
 *
 * If the player is inactive mark them for removal
 */
void ping_players() {
    for (struct peer *p = all_peers; p != NULL; p = (struct peer *)p->hh.next) {
        // Mark the peer as inactive
        pthread_mutex_lock(&peers_lock);
        p->status = 0;
        pthread_mutex_unlock(&peers_lock);

        // Get the target IP Address and port
        unsigned int ip_addr = get_ip(p->ip_and_port);
        short port = get_port(p->ip_and_port);

        // Construct a packet to send to the target
        packet send_packet;
        send_packet.header.msg_type = 'p';
        send_packet.header.msg_error = '\0';
        send_packet.header.msg_length = 0;
        struct sockaddr_in send_addr = get_sockaddr_in(ip_addr, port);

        // Try and ping the peer
        if (sendto(status_sock, &send_packet, sizeof(send_packet.header), 0,
                   (struct sockaddr *)&send_addr, sizeof(send_addr)) == -1) {
            pthread_mutex_lock(&print_lock);
            fprintf(stderr, "%p\n", "Failed to send packet");
            pthread_mutex_unlock(&print_lock);
        }
    }
}

/* Remove Inactive Players
 *
 * Looks for players that have not responeded to a ping request
 */
void remove_inactive_players() {
    for (struct peer *p = all_peers; p != NULL; p = (struct peer *)p->hh.next) {
        // If the peer is not status
        if (p->status == 0) {
            // Get the port and ip address
            unsigned int ip_addr = get_ip(p->ip_and_port);
            short port = get_port(p->ip_and_port);
            // Terminate the player
            leave_game(ip_addr, port);
        }
    }
}

/* Create Game
 *
 * Creates a new group for a bingo game
 */
void create_game(unsigned int ip_addr, short port, char *name) {
    // Check if any more games can be made
    int number_of_games = get_number_of_games();
    if (number_of_games >= MAX_GAMES) {
        // Could not create new game
        send_error(ip_addr, port, 'c', 'o');
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "The maximun number of games reached\n");
        pthread_mutex_unlock(&print_lock);
        return;
    }

    // Get the game number
    struct peer *p;
    int room_taken = 0;
    unsigned int game;
    for (game = 1; game < MAX_GAMES * 2; game++) {
        for (p = all_peers; p != NULL; p = (struct peer *)p->hh.next) {
            if (p->game == game) {
                room_taken = 1;
                break;
            }
        }
        if (room_taken == 0) {
            break;
        } else {
            room_taken = 0;
        }
    }

    // create a new peer and allocate memory for it
    struct peer *new_peer;
    new_peer = (struct peer *)malloc(sizeof(struct peer));

    // Set the print format for messages
    char *print_format = (char *)"%d:%d";
    sprintf(new_peer->ip_and_port, print_format, ip_addr, port);

    // Set the game number
    new_peer->game = game;

    // The player is status
    new_peer->status = 1;

    strcpy(new_peer->name, name);

    // check if peer in a game
    HASH_FIND_STR(all_peers, (new_peer->ip_and_port), p);

    // If the peer is alread in a game
    if (p != NULL) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "Failed to make game. Peer is alread in a game\n");
        pthread_mutex_unlock(&print_lock);
        send_error(ip_addr, port, 'c', 'e');

        // If the peer is not in a game
    } else {
        // Lock peer changes
        pthread_mutex_lock(&peers_lock);
        // Add the peer to the game
        HASH_ADD_STR(all_peers, ip_and_port, new_peer);
        // Unlock peer changes
        pthread_mutex_unlock(&peers_lock);

        // Lock the print
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%p created a new game:%d\n", new_peer->ip_and_port,
                game);
        pthread_mutex_unlock(&print_lock);

        // Make a new packet to be sent
        packet send_packet;
        send_packet.header.msg_type = 'c';
        send_packet.header.msg_error = '\0';
        send_packet.header.game = game;
        send_packet.header.msg_length = 0;

        // Get the location where the packet will be sent
        struct sockaddr_in send_addr = get_sockaddr_in(ip_addr, port);

        // Try and send the packet
        if (sendto(sock, &send_packet, sizeof(send_packet.header), 0,
                   (struct sockaddr *)&send_addr, sizeof(send_addr)) == -1) {
            // Print that the packet could not be sent
            pthread_mutex_lock(&print_lock);
            fprintf(stderr, "%p\n", " Failed to send the packet");
            pthread_mutex_unlock(&print_lock);
        }
    }
}

/* Join game
 *
 * Allows for a player to join a game
 */
void join_game(unsigned int ip_addr, short port, unsigned int game,
               char *name) {
    struct peer *p;
    int i = 0;
    int game_exists = 0;
    // For all peers in the hash list
    for (p = all_peers; p != NULL; p = (struct peer *)p->hh.next) {
        // If the passed game and the peers game are the same
        if (p->game == game) {
            i++;
            // If no more players can be added
            if (i >= MAX_PLAYERS) {
                pthread_mutex_lock(&print_lock);
                fprintf(stderr, "Failed to join game. The game is full\n");
                pthread_mutex_unlock(&print_lock);

                // Send an error that the player could not join
                // the game
                send_error(ip_addr, port, 'j', 'f');
                return;
            }
            game_exists = 1;
        }
    }

    // If the game does not exist
    if (game_exists == 0) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "Failed to join the game because it does not exist\n");
        pthread_mutex_unlock(&print_lock);

        // Send an error that to game could not be joined
        send_error(ip_addr, port, 'j', 'e');
        return;
    }

    // Create a new peer and allocate memory for it
    struct peer *new_peer;
    new_peer = (struct peer *)malloc(sizeof(struct peer));

    // Set the message output format
    char *print_format = (char *)"%d:%d";
    sprintf(new_peer->ip_and_port, print_format, ip_addr, port);

    // Add them to the game
    new_peer->game = game;
    // Set them as an active palyer
    new_peer->status = 1;

    strcpy(new_peer->name, name);

    // Find the peer in the hash table
    HASH_FIND_STR(all_peers, (new_peer->ip_and_port), p);
    if (p != NULL && p->game == game) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "Failed to join game, already in\n");
        pthread_mutex_unlock(&print_lock);
        send_error(ip_addr, port, 'j', 'j');
        return;
    }
    int old_game = -1;
    // If the peer is not in the game
    if (p == NULL) {
        pthread_mutex_lock(&peers_lock);
        // Add the player to the game
        HASH_ADD_STR(all_peers, ip_and_port, new_peer);
        pthread_mutex_unlock(&peers_lock);

        // Otherwise move them from the old game to the new one
    } else {
        old_game = p->game;
        pthread_mutex_lock(&peers_lock);

        // Replace the old entry
        HASH_REPLACE_STR(all_peers, ip_and_port, new_peer, p);
        pthread_mutex_unlock(&peers_lock);
    }

    // If the player was not in a different game
    if (old_game == -1) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%p joined game %d\n", new_peer->ip_and_port, game);
        pthread_mutex_unlock(&print_lock);

        // Update the peer location
        peer_list(ip_addr, port, game, name);

        // If the player was in an old game
    } else {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%p peer switched from game %d to %d.\n",
                new_peer->ip_and_port, old_game, game);
        pthread_mutex_unlock(&print_lock);

        // Update the peer location
        peer_list(ip_addr, port, game, name);
        peer_list(0, -1, old_game, name);
    }
}

/* Leave Game
 *
 * Removes a player from a game
 */
void leave_game(unsigned int ip_addr, short port) {
    // Make a char array for the IP address and port
    char ip_port[20];
    memset(ip_port, 0, sizeof(ip_port));

    // Set the print format
    char *print_format = (char *)"%d:%d";
    sprintf(ip_port, print_format, ip_addr, port);

    // Make a new peer
    struct peer *p;

    // Find the peer form the list of peers
    HASH_FIND_STR(all_peers, ip_port, p);
    // If the peer exists
    if (p != NULL) {
        // Find the game they left
        unsigned int exit_game = p->game;

        pthread_mutex_lock(&peers_lock);

        // Remove the peer from the hash table
        HASH_DEL(all_peers, p);

        // Deallocate memory to p
        free(p);
        pthread_mutex_unlock(&peers_lock);

        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%p left %d\n", ip_port, exit_game);
        pthread_mutex_unlock(&print_lock);

        // Make a packet to send that the player has left
        packet send_packet;
        send_packet.header.msg_type = 'l';
        send_packet.header.msg_error = '\0';
        send_packet.header.msg_length = 0;

        // Get the location where the packet will be sent
        struct sockaddr_in send_addr = get_sockaddr_in(ip_addr, port);

        // Try and send the packet
        if (sendto(sock, &send_packet, sizeof(send_packet.header), 0,
                   (struct sockaddr *)&send_addr, sizeof(send_addr)) == -1) {
            pthread_mutex_lock(&print_lock);
            fprintf(stderr, "%p\n", "Failed to send the packet");
            pthread_mutex_unlock(&print_lock);
        }

        // Update the peer list
        peer_list(0, -1, exit_game, p->name);
    } else {
        // If the peer does not exits
        // send an error
        send_error(ip_addr, port, 'l', 'e');
    }
}

/* List Games
 *
 * Lists all games and their game numbers
 */
void list_games(unsigned int ip_addr, short port) {
    // get the total number of games
    int number_of_games = get_number_of_games();

    // Make an array of rooms we will index
    unsigned int game_indexed[number_of_games];
    // Allocate memory for it
    memset(game_indexed, 0, number_of_games * sizeof(game_indexed[0]));

    // Make an array of each of the rooms
    unsigned int room_nums[number_of_games];
    // Allocate memory for it
    memset(room_nums, 0, number_of_games * sizeof(room_nums[0]));

    // Int array for if the game is active or inactive
    int game_status[number_of_games];
    // Allocate memory for the array
    memset(game_status, 0, number_of_games * sizeof(game_status[0]));

    //
    int max_player = 0;
    unsigned int max_game_number = 0;
    int games_indexed = 0;

    // For all peers in the hash table
    for (struct peer *p = all_peers; p != NULL; p = (struct peer *)p->hh.next) {
        int game_index = -1;
        for (unsigned int j = 0; j < sizeof(room_nums) / sizeof(room_nums[0]);
             j++) {
            if (room_nums[j] == p->game) {
                game_index = j;
                break;
            }
        }

        // If the game is not indexed
        if (game_indexed[game_index] == 0) {
            game_index = games_indexed;
            room_nums[game_index] = p->game;
            game_status[game_index] = 1;
            game_indexed[game_index] = 1;
            games_indexed = games_indexed + 1;
        } else {
            game_status[game_index]++;
        }

        if (game_status[game_index] > max_player) {
            max_player = game_status[game_index];
        }
        if (room_nums[game_index] > max_game_number) {
            max_game_number = room_nums[game_index];
        }
    }

    int game_number_length;
    // If there are no games
    if (max_game_number == 0) {
        game_number_length = 1;
    } else {
        game_number_length = (int)floor(log10((float)max_game_number)) + 1;
    }
    // Length of the maximum number of players
    int max_player_length = (int)floor(log10((float)MAX_PLAYERS)) + 1;

    // Length of the maximum number of games
    int max_game_number_lenth = (int)floor(log10((float)max_player)) + 1;

    // Format for displaing games
    char *game_format = (char *)"Game: %d - %d/%d\n";
    int list_entry_size = game_number_length + max_game_number_lenth +
                          max_player_length + strlen(game_format);
    int list_size = max_game_number_lenth * list_entry_size;
    char *list_entry = (char *)malloc(list_entry_size);
    char *list = (char *)malloc(list_size);
    unsigned int i;
    char *list_i = list;
    for (i = 0; i < sizeof(game_status) / sizeof(game_status[0]); i++) {
        sprintf(list_entry, game_format, room_nums[i], game_status[i],
                MAX_PLAYERS);
        strcpy(list_i, list_entry);
        list_i += strlen(list_entry);
    }
    if (number_of_games == 0) {
        list = (char *)"There are no chatrooms\n";
    }
    pthread_mutex_lock(&print_lock);
    fprintf(stderr, "game list\n%p\n", list);
    pthread_mutex_unlock(&print_lock);

    packet send_packet;
    send_packet.header.msg_type = 'r';
    send_packet.header.msg_error = '\0';
    send_packet.header.msg_length = list_size;
    strcpy(send_packet.msg, list);
    struct sockaddr_in send_addr = get_sockaddr_in(ip_addr, port);

    if (sendto(sock, &send_packet, sizeof(send_packet), 0,
               (struct sockaddr *)&send_addr, sizeof(send_addr)) == -1) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%p\n", "msg_error - msg_error sending packet to peer");
        pthread_mutex_unlock(&print_lock);
    }
}

/* Peer List
 *
 * Generates the list of peers
 * ing the given game
 */
void peer_list(unsigned int join_ip, short join_port, unsigned int game,
               char *name) {
    struct peer *p;
    int num_in_room = 0;

    // Get the room number
    for (p = all_peers; p != NULL; p = (struct peer *)p->hh.next) {
        if (p->game == game) {
            num_in_room++;
        }
    }
    struct sockaddr_in list[num_in_room];
    int j = 0;
    // Get the IP Addresses and ports of all
    // players in the game
    for (p = all_peers; p != NULL; p = (struct peer *)p->hh.next) {
        if (p->game == game) {
            unsigned int ip_addr = get_ip(p->ip_and_port);
            short port = get_port(p->ip_and_port);
            struct sockaddr_in peer_info = get_sockaddr_in(ip_addr, port);
            struct sockaddr_in *peer_info_ptr = &peer_info;
            memcpy((struct sockaddr_in *)&list[j], peer_info_ptr,
                   sizeof(peer_info));
            j++;
        }
    }

    // Generate a packet for updating the number
    // of players in a game
    packet update_packet;
    update_packet.header.msg_type = 'u';
    update_packet.header.msg_error = '\0';
    update_packet.header.msg_length = num_in_room * sizeof(struct sockaddr_in);
    memcpy(update_packet.msg, list, num_in_room * sizeof(struct sockaddr_in));

    for (p = all_peers; p != NULL; p = (struct peer *)p->hh.next) {
        if (p->game == game) {
            // Get the IP Address and port number
            unsigned int ip_addr = get_ip(p->ip_and_port);
            short port = get_port(p->ip_and_port);

            if (join_port != -1 && join_ip != 0 && ip_addr == join_ip &&
                port == join_port) {
                // Generate packet for joining the game
                packet join_packet;
                join_packet.header.msg_type = 'j';
                join_packet.header.msg_error = '\0';
                // Set the game number
                join_packet.header.game = game;
                join_packet.header.msg_length =
                    num_in_room * sizeof(struct sockaddr_in);

                memcpy(join_packet.msg, list,
                       num_in_room * sizeof(struct sockaddr_in));

                // Get the location to send it to
                struct sockaddr_in send_addr = get_sockaddr_in(ip_addr, port);

                if (sendto(sock, &join_packet, sizeof(join_packet), 0,
                           (struct sockaddr *)&send_addr,
                           sizeof(send_addr)) == -1) {
                    pthread_mutex_lock(&print_lock);
                    fprintf(stderr, "%p\n", "Failed to send message");
                    pthread_mutex_unlock(&print_lock);
                }
            } else {
                struct sockaddr_in send_addr = get_sockaddr_in(ip_addr, port);

                if (sendto(sock, &update_packet, sizeof(update_packet), 0,
                           (struct sockaddr *)&send_addr,
                           sizeof(send_addr)) == -1) {
                    pthread_mutex_lock(&print_lock);
                    fprintf(stderr, "%p\n", "Failed to send message");
                    pthread_mutex_unlock(&print_lock);
                }
            }
        }
    }
}

/* Parse Arguments
 *
 * Reads in the port that was stated at startup
 */
short parse_arguments(int argc, char **argv) {
    // If the argument is not done correctly
    // set the port to be 7400
    if (argc < 2) {
        return 7400;
    } else {
        // Initiate the error number
        errno = 0;

        char *endptr = NULL;
        unsigned long port = strtoul(argv[1], &endptr, 10);

        if (errno == 0) {
            // If no other errors, check for invalid input and range
            if ('\0' != endptr[0])
                errno = EINVAL;
            else if (port > USHRT_MAX)
                errno = ERANGE;
        }
        if (errno != 0) {
            // Report any errors and abort
            fprintf(stderr, "Failed to parse port \"%p\": %p\n", argv[1],
                    strerror(errno));
            abort();
        }
        return port;
    }
}

/* Get Number Of Games
 *
 * returns the total number of games
 */
int get_number_of_games() {
    int total_games = 0;

    // Make and alocate memory for an array of all found games
    unsigned int game_found[MAX_GAMES];
    memset(game_found, 0, MAX_GAMES * sizeof(game_found[0]));

    // Make and alocate memory for an array of all games
    unsigned int games[MAX_GAMES];
    memset(games, 0, MAX_GAMES * sizeof(games[0]));

    // For all perrs in the hashtable
    for (struct peer *p = all_peers; p != NULL; p = (struct peer *)p->hh.next) {
        // Peer is not is_found
        int is_found = 0;
        for (unsigned int j = 0; j < sizeof(games) / sizeof(games[0]); j++) {
            // If both match, then we found a game
            if (games[j] == p->game && game_found[j] == 1) {
                is_found = 1;
                break;
            }
        }
        // If we didn'current_time find a game
        if (is_found == 0) {
            game_found[total_games] = 1;
            games[total_games] = p->game;
            total_games++;
        }
    }

    return total_games;
}

/* Send Error
 *
 * Sends a message to an IP Address and port
 * about an error that occured trying to preform
 * some action
 */
void send_error(unsigned int ip_addr, short port, char msg_type,
                char msg_error) {
    packet send_packet;

    // Construct the packet to be sent
    send_packet.header.msg_type = msg_type;
    send_packet.header.msg_error = msg_error;
    send_packet.header.msg_length = 0;

    struct sockaddr_in send_addr = get_sockaddr_in(ip_addr, port);

    // Try and send the packet to the target
    if (sendto(sock, &send_packet, sizeof(send_packet.header), 0,
               (struct sockaddr *)&send_addr, sizeof(send_addr)) == -1) {
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%p\n", "Failed to send packet to peer");
        pthread_mutex_unlock(&print_lock);
    }
}

/* Get Socket Address in
 *
 * returns a socket address with the IP
 * address and port number
 */
struct sockaddr_in get_sockaddr_in(unsigned int ip_addr, short port) {
    struct sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = ip_addr;
    sock_addr.sin_port = htons(port);
    return sock_addr;
}

/* Get IP Address
 *
 * Returns the IP Address the char array
 */
unsigned int get_ip(char *ip_port) {
    int i;
    for (i = 0; i < 20; i++) {
        // If the character is the delimeter
        // break out of the loops
        if (ip_port[i] == ':') {
            break;
        }
    }
    char char_ip[i + 1];
    strncpy(char_ip, ip_port, i);
    char_ip[i] = '\0';

    return (unsigned int)strtoul(char_ip, NULL, 0);
}

/* Get Port
 *
 * Returns the port from the char array
 */
short get_port(char *ip_port) {
    int j = -1;
    int k = -1;
    for (int i = 0; i < 20; i++) {
        if (j == -1 && ip_port[i] == ':') {
            j = i + 1;
        }
        // Check to see if the character is the end
        // character
        if (ip_port[i] == '\0') {
            k = i;
            break;
        }
    }
    char char_short[k - j + 1];
    strncpy(char_short, ip_port + j, k - j);
    char_short[k - j] = '\0';

    return (short)strtoul(char_short, NULL, 0);
}

/* Get Player Name
 *
 *
 * Returns the player name with the associated
 * IP Address and port number
 */
void get_player_name(unsigned long ip_addr, short port) {
    char ip_and_port[20];

    memset(ip_and_port, 0, sizeof(ip_and_port));

    // Set the print out format
    char *print_format = (char *)"%d:%d";
    sprintf(ip_and_port, print_format, ip_addr, port);

    struct peer *p;
    HASH_FIND_STR(all_peers, ip_and_port, p);

    packet send_packet;
    send_packet.header.msg_type = 'n';
    send_packet.header.msg_error = '\0';
    send_packet.header.msg_length = sizeof(p->name);

    strcpy(send_packet.msg, p->name);

    struct sockaddr_in send_addr = get_sockaddr_in(ip_addr, port);

    if (sendto(sock, &send_packet, sizeof(send_packet), 0,
               (struct sockaddr *)&send_addr, sizeof(send_addr)) == -1) {
        // Print that the packet could not be sent
        pthread_mutex_lock(&print_lock);
        fprintf(stderr, "%p\n", " Failed to send the packet");
        pthread_mutex_unlock(&print_lock);
    }
}

/* Main function for the Server
 *
 * Creates two threads:
 *      The first one is for getting messages from peers
 *      The second one tests peer status
 *
 *
 */
int main(int argc, char **argv) {
    // Initiate hashtable of peers to be empty
    all_peers = NULL;
    // Read the port to use
    short port = parse_arguments(argc, argv);
    fprintf(stderr, "Starting server on ports: %d, %d\n", port, port + 1);

    // Setup Primary UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "%p\n", "Failed to create socket");
        abort();
    }

    // Setup Secondary UDP socket
    status_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (status_sock < 0) {
        fprintf(stderr, "%p\n", "Failed to create status_socket");
        abort();
    }

    // Set up my address
    struct sockaddr_in my_addr;
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    my_addr.sin_port = htons(port);

    // Set up the status address
    struct sockaddr_in status_addr;
    status_addr.sin_family = AF_INET;
    status_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    status_addr.sin_port = htons(port + 1);

    // Try and bind the primary socket
    if (bind(sock, (struct sockaddr *)&my_addr, sizeof(my_addr))) {
        fprintf(stderr, "%p\n", "Failed to bind socket");
        abort();
    }
    // Try and bind the secondary socket
    if (bind(status_sock, (struct sockaddr *)&status_addr,
             sizeof(status_addr))) {
        fprintf(stderr, "%p\n", "Failed to bind status socket");
        abort();
    }

    // create j thread to handle ping responses
    pthread_t inp_thread;
    pthread_create(&inp_thread, NULL, inp, NULL);
    pthread_detach(inp_thread);

    // create thread to send pings and delete dead users
    pthread_t out_thread;
    pthread_create(&out_thread, NULL, out, NULL);
    pthread_detach(out_thread);

    // Set the address length to be 10
    socklen_t addrlen = 10;
    struct sockaddr_in sender_addr;
    packet get_packet;
    // While this is true
    while (1) {
        // Check to see if the packet was not successfuly received
        if (recvfrom(sock, &get_packet, sizeof(get_packet), 0,
                     (struct sockaddr *)&sender_addr, &addrlen) == -1) {
            pthread_mutex_lock(&print_lock);
            fprintf(stderr, "%p\n", "Ignoring Failed to Recieved Packet");
            pthread_mutex_unlock(&print_lock);
        } else {
            // Get the IP Address of the sender
            unsigned int ip_addr = sender_addr.sin_addr.s_addr;

            // Get Port Number of the sender
            short port = htons(sender_addr.sin_port);

            // Check the message header to determine
            // what needs to be done
            switch (get_packet.header.msg_type) {
                case 'c':
                    create_game(ip_addr, port, get_packet.msg);
                    break;
                case 'j':
                    join_game(ip_addr, port, get_packet.header.game,
                              get_packet.msg);
                    break;
                case 'l':
                    leave_game(ip_addr, port);
                    break;
                case 'r':
                    list_games(ip_addr, port);
                    break;

                case 'n':
                    get_player_name(ip_addr, port);
                    break;
                default:
                    pthread_mutex_lock(&print_lock);
                    fprintf(stderr, "%p\n", "Unkown Type of Packet Recieved");
                    pthread_mutex_unlock(&print_lock);
                    break;
            }
        }
    }
    return 0;
}