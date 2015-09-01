#include <irc.h>
#include <network.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SEND_LOBBY_MESSAGE_FAILURE -20
#define MESSAGE_TOO_LONG -21
#define DISCONNECT_ERROR -22
#define PARAMETER_OUT_OF_RANGE -23
#define LEAVE_ROOM_ERROR -24

int currentRoom = -1;

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
    network_send("PASS *\r\n", 8);
    network_send(nickCommand, strlen(nickCommand));
    network_send(userCommand, strlen(userCommand));
    network_send(joinCommand, strlen(joinCommand));

    // Free our variables.
    free(nickCommand);
    free(userCommand);
    free(joinCommand);

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
    // Prepare room name.
    char roomName[strlen(ROOM_FORMAT) + 3];
    sprintf(roomName, ROOM_FORMAT, roomNumber);

    // Prepare topic command.
    char topicCommand[COMMAND_SIZE + strlen(roomName)];
    sprintf(topicCommand, "TOPIC %s\r\n", roomName);

    // Send command and test for errors.
    int sendRet = network_send(topicCommand, strlen(topicCommand));
    if (sendRet != NO_ERROR) {
        return sendRet;
    }

    // Read response and test for errors.
    char recvBuffer[512];
    int readRet = network_read(recvBuffer, 512);
    if (readRet != NO_ERROR) {
        return readRet;
    }
    
    // Check the topic of the channel
    if (strstr(recvBuffer, ":No topic is set")) {
        return 0;
    }

    if (strstr(recvBuffer, "WAITING")) {
        return 1;
    }

    if (strstr(recvBuffer, "PLAYING")) {
        return 2;
    }

    return -1;
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

