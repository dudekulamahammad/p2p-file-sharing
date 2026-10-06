#ifndef CLIENT_H
#define CLIENT_H

#include "sha1.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
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
#include <memory>
#include <atomic>
#include <fstream>
#include <algorithm>

using namespace std;

constexpr size_t PIECE_SIZE = 512 * 1024;
constexpr int MAX_PIECE_RETRIES = 3;
constexpr size_t MAX_LINE = 8 * 1024 * 1024;

struct SharedFile {
    string path;
    string group;
    string name;
    long long size = 0;
    vector<string> pieceHashes;
};

struct DownloadStatus {
    string group;
    string name;
    string destination;
    int totalPieces = 0;
    atomic<int> completedPieces{0};
    atomic<bool> finished{false};
    atomic<bool> failed{false};
};

extern map<string, SharedFile> sharedFiles;
extern set<string> stoppedShares;
extern mutex sharedMutex;
extern map<string, shared_ptr<DownloadStatus>> downloads;
extern mutex downloadsMutex;
extern string currentUser;
extern string trackerHost;
extern vector<int> trackerPorts;
extern int currentTracker;
extern int lastAnnouncedTracker;
extern int peerListenSocket;
extern int peerPort;
extern atomic<bool> running;
extern atomic<unsigned long long> sessionGeneration;
extern mutex outputMutex;

void printLine(const string &message);
bool sendAll(int fd, const void *data, size_t size);
bool sendLine(int fd, const string &message);
bool recvLine(int fd, string &out);
vector<string> splitTab(const string &text);
vector<string> splitComma(const string &text);
string joinComma(const vector<string> &values);
string fileKey(const string &group, const string &name);
string downloadKey(const string &group, const string &name, const string &destination);
string baseName(const string &path);
vector<string> shellWords(const string &line);
vector<string> normalizeCommand(vector<string> args);
void setSocketTimeout(int fd, int seconds);
int connectTo(const string &host, int port);
bool trackerRequest(const string &request, string &response);
void printPrompt();
void servePiece(int fd);
void peerWorker();
bool receivePiece(const string &endpoint, const string &group, const string &name, int pieceIndex, const string &expectedHash, int output, long long offset);
void printDownloadCompletion(const DownloadStatus &status);
bool advertiseSeeder(const string &seederUser, const string &group, const string &name, const string &path, long long size, const vector<string> &pieceHashes);
void downloadThread(shared_ptr<DownloadStatus> status, const string &seederUser, const string &group, const string &name, const string &destination, long long size, const string &wholeHash, const vector<string> &pieceHashes, const vector<string> &peers, unsigned long long generation);
bool makeUpload(const string &group, const string &path, SharedFile &result);
void printResponse(const string &response);
void printHelp();
void showDownloads();
void stopAllShares();
void removeGroupShares(const string &group);
bool destinationLooksValid(const string &path);
void commandLoop();

#endif
