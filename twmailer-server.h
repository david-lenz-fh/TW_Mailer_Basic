int persistMessage(char** messageLines);
char* readSingleMessage(char* username, char *messageId);
char* readMessage(char* username);
void signalHandler(int sig);
char** str_split(char* a_str, const char a_delim);
char* joinStrings(char **strings, char sep);
int countUserMessages(char* username);
char* listMessages(char* username);
int deleteMessage(char* username, char* messageId);