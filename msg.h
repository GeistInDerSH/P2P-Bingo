/* Message Header
 *
 * Contains information on
 * The type of message
 * The type of error (if there is any)
 * The game number the mesage is related to
 * The length of the message
 */
typedef struct msg_header {
    char msg_type;
    char msg_error;
    unsigned int game;
    unsigned int msg_length;
} message_header;

/* Packet
 *
 * This is the packet that is sent and
 * recieved between users
 *
 * msg is the message that is being sent
 */
typedef struct packet_t {
    struct msg_header header;
    char msg[1000];
} packet;