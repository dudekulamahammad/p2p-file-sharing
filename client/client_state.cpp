#include "client.h"

map<string, SharedFile> sharedFiles;
set<string> stoppedShares;
mutex sharedMutex;
map<string, shared_ptr<DownloadStatus>> downloads;
mutex downloadsMutex;
string currentUser;
string trackerHost = "127.0.0.1";
vector<int> trackerPorts;
int currentTracker = 0;
int lastAnnouncedTracker = -1;
int peerListenSocket = -1;
int peerPort = 0;
atomic<bool> running(true);
atomic<unsigned long long> sessionGeneration(0);
mutex outputMutex;
