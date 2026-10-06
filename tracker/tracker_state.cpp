#include "tracker.h"

map<string, string> users;
map<string, Group> groups;
map<string, map<string, FileMeta>> files;
mutex stateMutex;
int myPort = 0;
int peerPort = 0;
string peerHost = "127.0.0.1";
atomic<bool> running(true);
map<string, string> activeClients;
mutex activeClientsMutex;
