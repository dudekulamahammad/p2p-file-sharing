#include "tracker.h"

bool connectPeer(int &fd) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    setSocketTimeout(fd, 15);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(peerPort);

    if (inet_pton(AF_INET, peerHost.c_str(), &address.sin_addr) <= 0) {
        close(fd);
        fd = -1;
        return false;
    }

    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        close(fd);
        fd = -1;
        return false;
    }

    return true;
}

// Build a complete state snapshot for a tracker that is reconnecting.
void makeSnapshot(vector<string> &out) {
    lock_guard<mutex> lock(stateMutex);
    out.clear();

    for (const auto &entry : users) {
        out.push_back("SNAP_USER\t" + entry.first + "\t" + entry.second);
    }

    for (const auto &entry : groups) {
        const string &groupName = entry.first;
        const Group &group = entry.second;

        out.push_back("SNAP_GROUP\t" + groupName + "\t" + group.owner);

        for (const string &member : group.memberOrder) {
            out.push_back("SNAP_MEMBER\t" + groupName + "\t" + member);
        }

        for (const string &pending : group.pending) {
            out.push_back("SNAP_PENDING\t" + groupName + "\t" + pending);
        }
    }

    for (const auto &groupEntry : files) {
        for (const auto &fileEntry : groupEntry.second) {
            const FileMeta &file = fileEntry.second;
            vector<string> peers(file.peers.begin(), file.peers.end());

            out.push_back("SNAP_FILE\t" + groupEntry.first + "\t" +
                          fileEntry.first + "\t" + to_string(file.size) + "\t" +
                          file.whole + "\t" + joinComma(file.pieces) + "\t" +
                          joinComma(peers));
        }
    }
}

void applySnapshot(const vector<string> &records) {
    lock_guard<mutex> lock(stateMutex);

    for (const string &record : records) {
        vector<string> fields = splitTab(record);
        if (fields.empty()) {
            continue;
        }

        if (fields[0] == "SNAP_USER" && fields.size() >= 3) {
            users[fields[1]] = fields[2];
        } else if (fields[0] == "SNAP_GROUP" && fields.size() >= 3) {
            groups[fields[1]].owner = fields[2];
        } else if (fields[0] == "SNAP_MEMBER" && fields.size() >= 3) {
            Group &group = groups[fields[1]];
            group.members.insert(fields[2]);

            if (find(group.memberOrder.begin(), group.memberOrder.end(), fields[2]) ==
                group.memberOrder.end()) {
                group.memberOrder.push_back(fields[2]);
            }
        } else if (fields[0] == "SNAP_PENDING" && fields.size() >= 3) {
            groups[fields[1]].pending.insert(fields[2]);
        } else if (fields[0] == "SNAP_FILE" && fields.size() >= 7) {
            FileMeta file;
            file.size = atoll(fields[3].c_str());
            file.whole = fields[4];
            file.pieces = splitComma(fields[5]);

            if (!fields[6].empty()) {
                for (const string &peer : splitComma(fields[6])) {
                    file.peers.insert(peer);
                }
            }

            auto it = files[fields[1]].find(fields[2]);
            if (it == files[fields[1]].end()) {
                files[fields[1]][fields[2]] = file;
            } else {
                it->second.peers.insert(file.peers.begin(), file.peers.end());
            }
        }
    }
}

// Ask the peer tracker for a complete state snapshot after reconnecting.
void requestSnapshot() {
    int fd = -1;
    if (!connectPeer(fd)) {
        return;
    }

    if (!sendLine(fd, "SNAPSHOT_REQUEST")) {
        close(fd);
        return;
    }

    vector<string> records;
    string line;

    while (recvLine(fd, line)) {
        if (line == "SNAP_BEGIN") {
            continue;
        }
        if (line == "SNAP_END") {
            break;
        }
        records.push_back(line);
    }

    close(fd);

    if (!records.empty()) {
        applySnapshot(records);
    }
}

// Send one state-changing operation to the other tracker.
void syncRecord(const string &record) {
    int fd = -1;
    if (!connectPeer(fd)) {
        return;
    }

    if (sendLine(fd, "SYNC_ONE")) {
        sendLine(fd, record);
        sendLine(fd, "SYNC_END");
    }

    close(fd);
}

void applyRecord(const string &record) {
    vector<string> fields = splitTab(record);
    if (fields.empty()) {
        return;
    }

    lock_guard<mutex> lock(stateMutex);

    if (fields[0] == "SYNC_USER" && fields.size() >= 3) {
        users[fields[1]] = fields[2];
    } else if (fields[0] == "SYNC_GROUP" && fields.size() >= 3) {
        Group &group = groups[fields[1]];
        group.owner = fields[2];
        group.members.insert(fields[2]);

        if (find(group.memberOrder.begin(), group.memberOrder.end(), fields[2]) ==
            group.memberOrder.end()) {
            group.memberOrder.push_back(fields[2]);
        }
    } else if (fields[0] == "SYNC_MEMBER" && fields.size() >= 3) {
        Group &group = groups[fields[1]];
        group.members.insert(fields[2]);

        if (find(group.memberOrder.begin(), group.memberOrder.end(), fields[2]) ==
            group.memberOrder.end()) {
            group.memberOrder.push_back(fields[2]);
        }
    } else if (fields[0] == "SYNC_PENDING" && fields.size() >= 3) {
        groups[fields[1]].pending.insert(fields[2]);
    } else if (fields[0] == "SYNC_REMOVE_PENDING" && fields.size() >= 3) {
        groups[fields[1]].pending.erase(fields[2]);
    } else if (fields[0] == "SYNC_REMOVE_MEMBER" && fields.size() >= 3) {
        Group &group = groups[fields[1]];
        group.members.erase(fields[2]);
        group.memberOrder.erase(
            remove(group.memberOrder.begin(), group.memberOrder.end(), fields[2]),
            group.memberOrder.end());
    } else if (fields[0] == "SYNC_OWNER" && fields.size() >= 3) {
        groups[fields[1]].owner = fields[2];
    } else if (fields[0] == "SYNC_ACCEPT" && fields.size() >= 3) {
        Group &group = groups[fields[1]];
        group.pending.erase(fields[2]);
        group.members.insert(fields[2]);

        if (find(group.memberOrder.begin(), group.memberOrder.end(), fields[2]) ==
            group.memberOrder.end()) {
            group.memberOrder.push_back(fields[2]);
        }
    } else if (fields[0] == "SYNC_FILE" && fields.size() >= 7) {
        FileMeta file;
        file.size = atoll(fields[3].c_str());
        file.whole = fields[4];
        file.pieces = splitComma(fields[5]);

        if (!fields[6].empty()) {
            for (const string &peer : splitComma(fields[6])) {
                file.peers.insert(peer);
            }
        }

        auto it = files[fields[1]].find(fields[2]);
        if (it == files[fields[1]].end()) {
            files[fields[1]][fields[2]] = file;
        } else {
            it->second.peers.insert(file.peers.begin(), file.peers.end());
        }
    } else if (fields[0] == "SYNC_REMOVE_GROUP_PEER" && fields.size() >= 3) {
        auto groupIt = files.find(fields[1]);
        if (groupIt != files.end()) {
            for (auto fileIt = groupIt->second.begin();
                 fileIt != groupIt->second.end();) {
                fileIt->second.peers.erase(fields[2]);
                if (fileIt->second.peers.empty()) {
                    fileIt = groupIt->second.erase(fileIt);
                } else {
                    ++fileIt;
                }
            }
        }
    } else if (fields[0] == "SYNC_REMOVE_PEER" && fields.size() >= 4) {
        auto groupIt = files.find(fields[1]);
        if (groupIt != files.end()) {
            auto fileIt = groupIt->second.find(fields[2]);
            if (fileIt != groupIt->second.end()) {
                fileIt->second.peers.erase(fields[3]);
                if (fileIt->second.peers.empty()) {
                    groupIt->second.erase(fileIt);
                }
            }
        }
    }
}

void sendSnapshotResponse(int fd) {
    vector<string> records;
    makeSnapshot(records);

    sendLine(fd, "SNAP_BEGIN");
    for (const string &record : records) {
        sendLine(fd, record);
    }
    sendLine(fd, "SNAP_END");
}

// Handle one tracker/client or tracker/tracker TCP connection.
