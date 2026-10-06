#ifndef TRACKER_H
#define TRACKER_H

#include "sha1.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <fstream>
#include <algorithm>
#include <cctype>

using namespace std;

struct Group {
    string owner;
    set<string> members;
    set<string> pending;
    vector<string> memberOrder;
};

struct FileMeta {
    long long size = 0;
    string whole;
    vector<string> pieces;
    set<string> peers;
};

extern map<string, string> users;
extern map<string, Group> groups;
extern map<string, map<string, FileMeta>> files;
extern mutex stateMutex;
extern int myPort;
extern int peerPort;
extern string peerHost;
extern atomic<bool> running;
extern map<string, string> activeClients;
extern mutex activeClientsMutex;

constexpr size_t MAX_LINE = 8 * 1024 * 1024;

bool sendAll(int fd, const void *data, size_t size);
bool sendLine(int fd, const string &message);
bool recvLine(int fd, string &out);
vector<string> splitTab(const string &text);
vector<string> splitComma(const string &text);
string joinComma(const vector<string> &values);
string makeError(const string &message);
string getPeerAddress(int fd);
bool containsWhitespace(const string &text);
bool validField(const string &text, size_t maxLength);
bool validUserOrPassword(const string &text);
bool validGroupName(const string &text);
void setSocketTimeout(int fd, int seconds);
bool connectPeer(int &fd);
void makeSnapshot(vector<string> &out);
void applySnapshot(const vector<string> &records);
void requestSnapshot();
void syncRecord(const string &record);
void applyRecord(const string &record);
void sendSnapshotResponse(int fd);
void handleConnection(int fd);
void serverLoop(int serverSocket);
bool readPorts(const string &path, int &p1, int &p2);
int makeServer(int port);

#endif
