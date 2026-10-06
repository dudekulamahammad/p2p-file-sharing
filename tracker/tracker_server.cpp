#include "tracker.h"

void handleConnection(int fd) {
    string line;

    if (!recvLine(fd, line)) {
        close(fd);
        return;
    }

    if (line == "SNAPSHOT_REQUEST") {
        sendSnapshotResponse(fd);
        close(fd);
        return;
    }

    if (line == "SYNC_ONE") {
        string record;
        if (recvLine(fd, record)) {
            applyRecord(record);
        }
        close(fd);
        return;
    }

    vector<string> fields = splitTab(line);
    string operation = fields.empty() ? "" : fields[0];
    string response;

    if (operation == "CREATE_USER" && fields.size() == 3) {
        if (!validUserOrPassword(fields[1]) || !validUserOrPassword(fields[2])) {
            response = makeError("Invalid user or password");
        } else {
            lock_guard<mutex> lock(stateMutex);

            if (users.count(fields[1])) {
                response = makeError("User already exists");
            } else {
                users[fields[1]] = fields[2];
                response = "OK USER";
            }
        }
    } else if (operation == "LOGIN" && (fields.size() == 3 || fields.size() == 4)) {
        lock_guard<mutex> lock(stateMutex);

        if (!users.count(fields[1])) {
            response = makeError("No such user");
        } else if (users[fields[1]] != fields[2]) {
            response = makeError("Invalid credentials");
        } else {
            response = "OK LOGIN";
        }

        if (response == "OK LOGIN") {
            string endpoint = getPeerAddress(fd);
            if (fields.size() >= 4 && !fields[3].empty()) {
                endpoint = endpoint.substr(0, endpoint.find_last_of(':') + 1) + fields[3].substr(fields[3].find_last_of(':') + 1);
            }

            lock_guard<mutex> lock(activeClientsMutex);
            activeClients[fields[1]] = endpoint;
            cout << "[TRACKER] Client connected: user=" << fields[1]
                 << " endpoint=" << endpoint << '\n' << flush;
        }
    } else if (operation == "LOGOUT" && fields.size() >= 2) {
        // Logout is deliberately idempotent. File seeder removal is performed
        // separately by the client using STOP for every locally shared file.
        string endpoint = "unknown";
        {
            lock_guard<mutex> lock(activeClientsMutex);
            auto it = activeClients.find(fields[1]);
            if (it != activeClients.end()) {
                endpoint = it->second;
                activeClients.erase(it);
            }
        }

        cout << "[TRACKER] Client disconnected: user=" << fields[1]
             << " endpoint=" << endpoint << '\n' << flush;
        response = "OK LOGOUT";
    } else if (operation == "CREATE_GROUP" && fields.size() == 3) {
        if (!validGroupName(fields[2])) {
            response = makeError("Invalid group name");
        } else {
            lock_guard<mutex> lock(stateMutex);

            if (!users.count(fields[1])) {
                response = makeError("Unknown user");
            } else if (groups.count(fields[2])) {
                response = makeError("Group already exists");
            } else {
                Group group;
                group.owner = fields[1];
                group.members.insert(fields[1]);
                group.memberOrder.push_back(fields[1]);
                groups[fields[2]] = group;
                response = "OK GROUP";
            }
        }
    } else if (operation == "JOIN_GROUP" && fields.size() == 3) {
        lock_guard<mutex> lock(stateMutex);

        if (!groups.count(fields[2])) {
            response = makeError("Group does not exist");
        } else if (groups[fields[2]].members.count(fields[1])) {
            response = makeError("Already a member");
        } else if (groups[fields[2]].pending.count(fields[1])) {
            response = makeError("Request already pending");
        } else {
            groups[fields[2]].pending.insert(fields[1]);
            response = "OK REQUEST";
        }
    } else if (operation == "LEAVE_GROUP" && (fields.size() == 3 || fields.size() == 4)) {
        lock_guard<mutex> lock(stateMutex);

        if (!groups.count(fields[2]) || !groups[fields[2]].members.count(fields[1])) {
            response = makeError("Not a member");
        } else {
            Group &group = groups[fields[2]];
            bool ownerLeaving = (group.owner == fields[1]);

            if (ownerLeaving) {
                auto it = find(group.memberOrder.begin(), group.memberOrder.end(), fields[1]);

                if (it == group.memberOrder.end() || it + 1 == group.memberOrder.end()) {
                    response = makeError("Owner cannot leave because there is no next member");
                } else {
                    string newOwner = *(it + 1);
                    group.members.erase(fields[1]);
                    group.memberOrder.erase(it);
                    group.owner = newOwner;

                    if (fields.size() == 4) {
                        auto fileGroup = files.find(fields[2]);
                        if (fileGroup != files.end()) {
                            for (auto fileIt = fileGroup->second.begin();
                                 fileIt != fileGroup->second.end();) {
                                fileIt->second.peers.erase(fields[3]);
                                if (fileIt->second.peers.empty()) {
                                    fileIt = fileGroup->second.erase(fileIt);
                                } else {
                                    ++fileIt;
                                }
                            }
                        }
                    }

                    response = "OK LEFT\t" + newOwner;
                }
            } else {
                group.members.erase(fields[1]);
                group.memberOrder.erase(
                    remove(group.memberOrder.begin(), group.memberOrder.end(), fields[1]),
                    group.memberOrder.end());

                if (fields.size() == 4) {
                    auto fileGroup = files.find(fields[2]);
                    if (fileGroup != files.end()) {
                        for (auto fileIt = fileGroup->second.begin();
                             fileIt != fileGroup->second.end();) {
                            fileIt->second.peers.erase(fields[3]);
                            if (fileIt->second.peers.empty()) {
                                fileIt = fileGroup->second.erase(fileIt);
                            } else {
                                ++fileIt;
                            }
                        }
                    }
                }

                response = "OK LEFT";
            }
        }
    } else if (operation == "LIST_GROUPS" && fields.size() >= 2) {
        lock_guard<mutex> lock(stateMutex);
        ostringstream output;
        output << "GROUPS";

        for (const auto &entry : groups) {
            output << '\t' << entry.first;
        }

        response = output.str();
    } else if (operation == "LIST_REQUESTS" && fields.size() == 3) {
        lock_guard<mutex> lock(stateMutex);

        if (!groups.count(fields[2])) {
            response = makeError("Group does not exist");
        } else if (groups[fields[2]].owner != fields[1]) {
            response = makeError("Only owner can view requests");
        } else {
            ostringstream output;
            output << "REQUESTS";
            for (const string &request : groups[fields[2]].pending) {
                output << '\t' << request;
            }
            response = output.str();
        }
    } else if (operation == "ACCEPT" && fields.size() == 4) {
        lock_guard<mutex> lock(stateMutex);

        if (!groups.count(fields[2])) {
            response = makeError("Group does not exist");
        } else if (groups[fields[2]].owner != fields[1]) {
            response = makeError("Only owner can accept requests");
        } else if (!groups[fields[2]].pending.count(fields[3])) {
            response = makeError("No such request");
        } else if (!users.count(fields[3])) {
            response = makeError("No such user");
        } else {
            Group &group = groups[fields[2]];
            group.pending.erase(fields[3]);
            group.members.insert(fields[3]);
            group.memberOrder.push_back(fields[3]);
            response = "OK ACCEPT";
        }
    } else if (operation == "GROUP_INFO" && fields.size() == 3) {
        lock_guard<mutex> lock(stateMutex);

        if (!groups.count(fields[2])) {
            response = makeError("Group does not exist");
        } else {
            const Group &group = groups[fields[2]];
            ostringstream output;
            output << "GROUP_INFO\t" << fields[2] << '\t' << group.owner << '\t';

            for (size_t i = 0; i < group.memberOrder.size(); ++i) {
                if (i != 0) output << ',';
                output << group.memberOrder[i];
            }

            output << '\t';
            bool first = true;
            for (const string &pending : group.pending) {
                if (!first) output << ',';
                first = false;
                output << pending;
            }

            response = output.str();
        }
    } else if (operation == "LIST_MEMBERS" && fields.size() == 3) {
        lock_guard<mutex> lock(stateMutex);

        if (!groups.count(fields[2])) {
            response = makeError("Group does not exist");
        } else if (!groups[fields[2]].members.count(fields[1])) {
            response = makeError("Not a group member");
        } else {
            ostringstream output;
            output << "MEMBERS\t" << fields[2] << '\t';
            bool first = true;

            for (const string &member : groups[fields[2]].memberOrder) {
                if (!first) output << ',';
                first = false;
                output << member;
            }

            response = output.str();
        }
    } else if (operation == "GET_OWNER" && fields.size() == 3) {
        lock_guard<mutex> lock(stateMutex);

        if (!groups.count(fields[2])) {
            response = makeError("Group does not exist");
        } else {
            response = "OWNER\t" + fields[2] + "\t" + groups[fields[2]].owner;
        }
    } else if (operation == "MY_GROUPS" && fields.size() == 2) {
        lock_guard<mutex> lock(stateMutex);
        ostringstream output;
        output << "MY_GROUPS";

        for (const auto &entry : groups) {
            if (entry.second.members.count(fields[1])) {
                output << '\t' << entry.first;
            }
        }

        response = output.str();
    } else if (operation == "UPLOAD" && fields.size() == 8) {
        lock_guard<mutex> lock(stateMutex);
        string groupName = fields[2];
        string fileName = fields[3];

        if (!groups.count(groupName) || !groups[groupName].members.count(fields[1])) {
            response = makeError("Not a group member");
        } else if (!validField(fileName, 4096)) {
            response = makeError("Invalid file name");
        } else {
            FileMeta file;
            file.size = atoll(fields[4].c_str());
            file.whole = fields[5];
            file.pieces = splitComma(fields[6]);
            file.peers.insert(fields[7]);

            auto it = files[groupName].find(fileName);
            if (it != files[groupName].end()) {
                // Same metadata means another seeder for the same logical file.
                if (it->second.size != file.size ||
                    it->second.whole != file.whole ||
                    it->second.pieces != file.pieces) {
                    response = makeError("File name already exists with different content");
                } else {
                    it->second.peers.insert(fields[7]);
                    response = "OK UPLOAD";
                }
            } else {
                files[groupName][fileName] = file;
                response = "OK UPLOAD";
            }
        }
    } else if (operation == "LIST_FILES" && fields.size() == 3) {
        lock_guard<mutex> lock(stateMutex);

        if (!groups.count(fields[2]) || !groups[fields[2]].members.count(fields[1])) {
            response = makeError("Not a group member");
        } else {
            ostringstream output;
            output << "FILES";

            for (const auto &entry : files[fields[2]]) {
                output << '\t' << entry.first << '\t' << entry.second.size;
            }

            response = output.str();
        }
    } else if (operation == "GET_META" && fields.size() == 4) {
        lock_guard<mutex> lock(stateMutex);

        if (!groups.count(fields[2]) || !groups[fields[2]].members.count(fields[1])) {
            response = makeError("Not a group member");
        } else if (!files[fields[2]].count(fields[3])) {
            response = makeError("File not found");
        } else {
            const FileMeta &file = files[fields[2]][fields[3]];
            vector<string> peers(file.peers.begin(), file.peers.end());
            response = "META\t" + fields[2] + "\t" + fields[3] + "\t" +
                       to_string(file.size) + "\t" + file.whole + "\t" +
                       joinComma(file.pieces) + "\t" + joinComma(peers);
        }
    } else if (operation == "STOP" && fields.size() == 5) {
        lock_guard<mutex> lock(stateMutex);
        string groupName = fields[2];
        string fileName = fields[3];
        string peer = fields[4];

        if (!groups.count(groupName) || !groups[groupName].members.count(fields[1])) {
            response = makeError("Not a group member");
        } else if (!files[groupName].count(fileName)) {
            response = makeError("File not found");
        } else if (!files[groupName][fileName].peers.count(peer)) {
            response = makeError("File is not shared by this client");
        } else {
            files[groupName][fileName].peers.erase(peer);
            if (files[groupName][fileName].peers.empty()) {
                files[groupName].erase(fileName);
            }
            response = "OK STOP";
        }
    } else {
        response = makeError("Bad request");
    }

    sendLine(fd, response);
    close(fd);

    // Only successful state-changing operations are replicated.
    if (response.rfind("OK ", 0) != 0) {
        return;
    }

    if (operation == "CREATE_USER") {
        syncRecord("SYNC_USER\t" + fields[1] + "\t" + fields[2]);
    } else if (operation == "CREATE_GROUP") {
        syncRecord("SYNC_GROUP\t" + fields[2] + "\t" + fields[1]);
    } else if (operation == "JOIN_GROUP") {
        syncRecord("SYNC_PENDING\t" + fields[2] + "\t" + fields[1]);
    } else if (operation == "LEAVE_GROUP") {
        syncRecord("SYNC_REMOVE_MEMBER\t" + fields[2] + "\t" + fields[1]);

        if (fields.size() == 4) {
            syncRecord("SYNC_REMOVE_GROUP_PEER\t" + fields[2] + "\t" + fields[3]);
        }

        if (response.rfind("OK LEFT\t", 0) == 0) {
            syncRecord("SYNC_OWNER\t" + fields[2] + "\t" + response.substr(8));
        }
    } else if (operation == "ACCEPT") {
        syncRecord("SYNC_ACCEPT\t" + fields[2] + "\t" + fields[3]);
    } else if (operation == "UPLOAD") {
        lock_guard<mutex> lock(stateMutex);
        const FileMeta &file = files[fields[2]][fields[3]];
        vector<string> peers(file.peers.begin(), file.peers.end());

        syncRecord("SYNC_FILE\t" + fields[2] + "\t" + fields[3] + "\t" +
                   to_string(file.size) + "\t" + file.whole + "\t" +
                   joinComma(file.pieces) + "\t" + joinComma(peers));
    } else if (operation == "STOP") {
        syncRecord("SYNC_REMOVE_PEER\t" + fields[2] + "\t" + fields[3] + "\t" + fields[4]);
    }
}

void serverLoop(int serverSocket) {
    while (running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);
        FD_SET(STDIN_FILENO, &readSet);

        int maxFd = max(serverSocket, STDIN_FILENO);
        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(maxFd + 1, &readSet, nullptr, nullptr, &timeout);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ready == 0) {
            continue;
        }

        // Tracker console commands are handled here so `quit` can stop the
        // tracker even while it is waiting for client connections.
        if (FD_ISSET(STDIN_FILENO, &readSet)) {
            string command;
            if (!getline(cin, command)) {
                running = false;
                break;
            }

            if (command == "quit") {
                cout << "\n[TRACKER] Shutting down...\n" << flush;
                running = false;
                break;
            }

            if (!command.empty()) {
                cout << "[TRACKER] Unknown command: " << command
                     << ". Use: quit\n" << flush;
            }
        }

        if (FD_ISSET(serverSocket, &readSet) && running) {
            sockaddr_in clientAddress{};
            socklen_t length = sizeof(clientAddress);
            int clientSocket = accept(
                serverSocket,
                reinterpret_cast<sockaddr *>(&clientAddress),
                &length);

            if (clientSocket < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }

            setSocketTimeout(clientSocket, 15);
            thread(handleConnection, clientSocket).detach();
        }
    }
}

bool readPorts(const string &path, int &p1, int &p2) {
    ifstream input(path);
    if (!input) {
        return false;
    }

    string number;
    string port;

    while (input >> number >> port) {
        if (number == "1") {
            p1 = atoi(port.c_str());
        } else if (number == "2") {
            p2 = atoi(port.c_str());
        }
    }

    return p1 > 0 && p2 > 0;
}

int makeServer(int port) {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        return -1;
    }

    int reuse = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        close(serverSocket);
        return -1;
    }

    if (listen(serverSocket, 64) < 0) {
        close(serverSocket);
        return -1;
    }

    return serverSocket;
}

