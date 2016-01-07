#include <irc.h>
#include <network.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errors.h>

#include <errno.h>

int currentRoom = -1;

/**
 * @brief Structure used to store an IRC message.
 */
struct IrcMessage {
    char *prefix;   /**< The prefix of the message.            */
    char *command;  /**< The command sent in this message.     */
    char *trailing; /**< The trailing part of the IRC message. */
};

/**
 * @brief Private function to get the next IRC message and parse it to generate
 *        a \ref IrcMessage structure that stores it.
 *        It is meant to be used by the irc parser to retrieve a new message
 *        and do some basic processing on it.
 *
 * -----------------------------
 * The Backus-Naur form of the message is:
 *
 * message =  [ ":" prefix SPACE ] command [ params ] crlf
 *
 * The prefix is used to indicate the origin of the message.
 * The clients should not send a prefix.
 *
 * The command must be either an IRC command or a three digit value.
 *
 * The maximum size of a message is 512 bytes.
 * -----------------------------
 * [extract form RFC 2812, section 2.3]
 *
 * @param str The message to be parsed. MUST BE DELETED USING deleteMessage().
 */
struct IrcMessage *getNextMessage()
{

    char str[MAX_MESSAGE_SIZE];
    network_readLine(str, MAX_MESSAGE_SIZE);

    char *prefixEnd;
    int prefixLen = 0;

    if (str[0] == ':') {
        prefixEnd = strchr(str, ' ') - 1;
        prefixLen = prefixEnd - str - 1;
    }

    char *trailingStart = strchr(prefixEnd + 2, ' ') + 1; //parsing trailing
    int   trailingLen   = strlen(trailingStart) - 1;

    char *commandStart  = prefixEnd + 2; //parsing command
    char *commandEnd    = trailingStart - 2;
    int   commandLen    = commandEnd - commandStart + 1;

    struct IrcMessage *message = malloc(sizeof(struct IrcMessage));
    message->prefix            = malloc(prefixLen + 1);
    message->command           = malloc(commandLen + 1);
    message->trailing          = malloc(trailingLen + 1);

    strncpy(message->prefix,   str + 1,       prefixLen);
    strncpy(message->command,  commandStart,  commandLen);
    strncpy(message->trailing, trailingStart, trailingLen);

    message->prefix[prefixLen]     = '\0';
    message->command[commandLen]   = '\0';
    message->trailing[trailingLen] = '\0';

    return message;
}

/**
 * @brief Private function to delete a \ref IrcMessage object returned by
 *        \ref parseMessage.
 */
void deleteMessage(struct IrcMessage **message)
{
    free((*message)->prefix);
    free((*message)->command);
    free((*message)->trailing);
    free( *message);
    *message = NULL;
}


/**
 * Use network_connect to open a connection to IRC server.
 *
 * After, sends "PASS *" command (just a convention, because 
 * lobby doesn't have a password), then sends "NICK <name>" 
 * and "USER <name> 8 * :<name>" commands to set the player name and nick,
 * where <name> is the name of the player and "8 *" is the user mode. 
 *
 * At last, it sends "JOIN <channel>" command to join the lobby, 
 * where <channel> is the lobby channel name.
 *
 * ALL COMMANDS MUST BE TERMINATED IN "\r\n".
 */
int irc_connect(char *name)
{
    int nameSize = strlen(name);
    if (nameSize == 0 || nameSize > 9)
        return PARAMETER_OUT_OF_RANGE;

    // Connect to IRC server and test for errors.
    int connectRet = network_connect(IRC_SERVER, IRC_PORT);
    if (connectRet != NO_ERROR) {
        return connectRet;
    }

    // Allocate memory for our variables.
    char *nickCommand = malloc(COMMAND_SIZE + strlen(name));
    char *userCommand = malloc(COMMAND_SIZE + strlen(name) * 2);
    char *joinCommand = malloc(COMMAND_SIZE + strlen(LOBBY_CHANNEL));

    // Prepare commands.
    sprintf(nickCommand, "NICK %s\r\n", name);
    sprintf(userCommand, "USER %s 8 * :%s\r\n", name, name);
    sprintf(joinCommand, "JOIN %s\r\n", LOBBY_CHANNEL);

    // Send commands to the server.
    int commandSequenceLength = 8 + strlen(nickCommand) +
                                    strlen(userCommand) +
                                    strlen(joinCommand);

    char *commandSequence = malloc(commandSequenceLength);

    sprintf(commandSequence, "PASS *\r\n%s%s%s",
                             nickCommand, userCommand, joinCommand);

    int sendRet = network_send(commandSequence, commandSequenceLength);

    if (sendRet != NO_ERROR)
        return sendRet;

    // Free our variables.
    free(nickCommand);
    free(userCommand);
    free(joinCommand);
    free(commandSequence);

    return NO_ERROR;
}

/**
 * It simply sends the "QUIT" command and use network_disconnect
 * to close the connection.
 */
int irc_disconnect()
{
    // Send QUIT command and disconnect from server.
    int sendRet = network_send("QUIT\r\n", 6);

    if (sendRet != NO_ERROR)
        return sendRet;

    int disconnRet = network_disconnect();

    if (disconnRet != NO_ERROR) {
        return disconnRet;
    }

    return NO_ERROR;
}

/**
 * Takes room number as argument and generates room name using the
 * following format: "#cruce-gameXXX" where XXX is the number of room.
 * After, it sends "JOIN #cruce-gameXXX" command to server to join the room.
 */
int irc_joinRoom(int roomNumber)
{
    if (roomNumber < 0 || roomNumber > 999)
        return PARAMETER_OUT_OF_RANGE;

    // Prepare room name.
    char roomName[strlen(ROOM_FORMAT) + 3];
    sprintf(roomName, ROOM_FORMAT, roomNumber);

    // Prepare join command.
    char joinCommand[COMMAND_SIZE + strlen(roomName)];
    sprintf(joinCommand, "JOIN %s\r\n", roomName);

    // Send join command and test if there is any error.
    int sendRet = network_send(joinCommand, strlen(joinCommand));
    if (sendRet != NO_ERROR) {
        return sendRet;
    }

    // Set current room to room we joined a moment ago.
    currentRoom = roomNumber;

    return NO_ERROR;
}

/**
 * First test if player has joined in any room. If not, return -1, else
 * generate room name using current room number and send "PART #cruce-gameXXX"
 * command to leave the room.
 */
int irc_leaveRoom()
{
    // Test if player is in any room.
    if (currentRoom < 0)
        return LEAVE_ROOM_ERROR;

    char roomName[strlen(ROOM_FORMAT) + 3];

    // Prepare room name.
    sprintf(roomName, ROOM_FORMAT, currentRoom);

    // Prepare leave command.
    char partCommand[COMMAND_SIZE + strlen(roomName)];
    sprintf(partCommand, "PART %s\r\n", roomName);

    // Send leave command and test for errors.
    int sendRet = network_send(partCommand, strlen(partCommand));
    if (sendRet != NO_ERROR) {
        return sendRet;
    }

    // Reset current room to default value.
    currentRoom = -1;

    return NO_ERROR;
}

/**
 * Send "PRIVMSG <channel> <message>" command for sending 
 * a message to lobby, where <channel> is name of the lobby
 * channel and <message> is message to send.
 */
int irc_sendLobbyMessage(char *message)
{
    if (strlen(message) > 512)
        return MESSAGE_TOO_LONG;

    // Allocate memory for message command and prepare it.
    char lobbyMessageCommand[COMMAND_SIZE
                             + strlen(LOBBY_CHANNEL)
                             + strlen(message)];

    sprintf(lobbyMessageCommand, "PRIVMSG %s %s\r\n", LOBBY_CHANNEL, message);

    // Send message command.
    return network_send(lobbyMessageCommand, strlen(lobbyMessageCommand));
}

/**
 * Generate room name using its ID and send "TOPIC <channel>"
 * command to get the topic of the channel. After read it
 * and if it is "topic not set", return 0. If it is 
 * "WAITING" return 1 or "PLAYING" return 2.
 */
int irc_toggleRoomStatus(int roomNumber)
{
    if (roomNumber < 0 || roomNumber > 999)
        return PARAMETER_OUT_OF_RANGE;

    // Prepare room name.
    char roomName[strlen(ROOM_FORMAT) + 3];
    sprintf(roomName, ROOM_FORMAT, roomNumber);

    // Prepare topic command.
    char fetchTopicCommand[COMMAND_SIZE + strlen(roomName)];
    sprintf(fetchTopicCommand, "TOPIC %s\r\n", roomName);

    // Send command and test for errors.
    int sendRet = network_send(fetchTopicCommand, strlen(fetchTopicCommand));
    if (sendRet != NO_ERROR) {
        printf("Send failed\n");
        return sendRet;
    }

    // Read the channel's status response and test for errors.
    char recvBuffer[512];
    int readRet = network_read(recvBuffer, 512);
    if (readRet < 0) {
        return readRet;
    }
    
    // Change the topic of the channel
    char newTopicCommand[COMMAND_SIZE + strlen(roomName)];
    if (strstr(recvBuffer, "WAITING")) {
        sprintf(newTopicCommand, "TOPIC %s PLAYING\r\n", roomName);
    } else if (strstr(recvBuffer, "PLAYING")) {
        sprintf(newTopicCommand, "TOPIC %s WAITING\r\n", roomName);
    } else {
        return TOGGLE_ROOM_STATUS_ERROR;
    }

    sendRet = network_send(newTopicCommand, strlen(newTopicCommand));

    return NO_ERROR;
}

/**
 * Use irc_toggleRoomStatus to see if channel is used or
 * not and return index of a free channel, or -1 if all
 * channels are used.
 */
int irc_getAvailableRoom()
{
    // Value of isUsed is 1 if channel is in use.
    int isUsed;

    // Check all 1000 channels for a free one
    for (int i = 0; i <= 999; i++) {
        isUsed = irc_toggleRoomStatus(i);

        // If there is a free channel return its index
        if (!isUsed) {
            return i;
        }
    }
    
    // Return -1 if all channels are used
    return -1;
}

/**
 * Use irc_getAvailableRoom function to get first free channel ID,
 * after generates room name using the ID and join it with 
 * "JOIN <channel>" command. After joining, it sends "TOPIC <channel> WAITING"
 * command to set the topic of the channel to WAITING.
 */
int irc_createRoom()
{
    // Get number of a free room.
    int roomNumber = irc_getAvailableRoom();

    // If there is no free room.
    if (roomNumber == -1) {
        return -1;
    }
    
    // If user is already in a room, return error.
    if (currentRoom != -1) {
        return -1;
    }
    
    // Prepare room name.
    char roomName[strlen(ROOM_FORMAT) + 3];
    sprintf(roomName, ROOM_FORMAT, roomNumber);

    // Prepare join command.
    char joinCommand[COMMAND_SIZE + strlen(roomName)];
    sprintf(joinCommand, "JOIN %s\r\n", roomName);

    // Prepare topic command.
    char topicCommand[COMMAND_SIZE + strlen(roomName) + 7];
    sprintf(topicCommand, "TOPIC %s %s\r\n", roomName, "WAITING");

    // Send join command and test for errors.
    int sendRet = network_send(joinCommand, strlen(joinCommand));
    if (sendRet != NO_ERROR) {
        return sendRet;
    }
    
    // Send topic command and test for errors.
    sendRet = network_send(topicCommand, strlen(topicCommand));
    if (sendRet != NO_ERROR) {
        return sendRet;
    }

    return roomNumber;
}


/**
 * Send "NAMES <channel>" command and read the response
 * containing the names list. Response format is:
 * "Users on <channel>: user1 user2 user3 ... userN".
 */
char *irc_getNames(int isRoom)
{
    // Buffer for response of the server.
    char buffer[512];

    char *namesCommand;

    // If isRoom is 1, get names from joined room.
    if (isRoom) {
        // If user didn't join any room, return error.
        if (currentRoom == -1) {
            printf("Here1");
            return NULL;
        }

        // Prepare room name.
        char roomName[strlen(ROOM_FORMAT) + 3];
        sprintf(roomName, ROOM_FORMAT, currentRoom);

        // Prepare names command.
        namesCommand = malloc(COMMAND_SIZE + strlen(roomName));
        sprintf(namesCommand, "NAMES %s\r\n", roomName);
    } else {
        // Prepare names command.
        namesCommand = malloc(COMMAND_SIZE + strlen(LOBBY_CHANNEL));
        sprintf(namesCommand, "NAMES %s\r\n", LOBBY_CHANNEL);
    }

    // Send command and test for errors.
    int sendRet = network_send(namesCommand, strlen(namesCommand));
    free(namesCommand);
    if (sendRet != NO_ERROR) {
        return NULL;
    }

    // Read response and test for errors.
    int readRet = network_read(buffer, 512);
    if (readRet < 0) {
        return NULL;
    }

    // Allocate space for names list and create it.
    char *namesListStart = strchr(buffer, ':');
    char *names = malloc(strlen(namesListStart));
    strcpy(names, namesListStart);

    return names;
}

/**
* Test if the nickname is in the lobby and send "INVITE <channel> <nick>"
* command to invite the user to current room.
*/
int irc_invite(char *nickname)
{
// If user didn't join any room.
    if (currentRoom == -1) {
        return -1;
    }

    // Get names in the lobby and test for errors.
    char *channelNames = irc_getNames(0);
    if (channelNames == NULL) {
        return -2;
    }
    // If nickname isn't in the lobby, return error.
    if (strstr(channelNames, nickname) == NULL) {
        free(channelNames);
        return -3;
    }

    // Free names list.
    free(channelNames);

    // Prepare room name.
    char roomName[strlen(ROOM_FORMAT) + 3];
    sprintf(roomName, ROOM_FORMAT, currentRoom);

    // Prepare invite command.
    char inviteCommand[COMMAND_SIZE + strlen(roomName) + strlen(nickname)];
    sprintf(inviteCommand, "INVITE %s %s\r\n", roomName, nickname);
    
    // Send invite command and test for errors.
    int sendRet = network_send(inviteCommand, strlen(inviteCommand));
    if (sendRet != NO_ERROR) {
        return sendRet;
    }

    return 0;
}
