#include "client.h"

void printResponse(const string &response) {
    vector<string> fields = splitTab(response);
    if (fields.empty()) {
        return;
    }

    if (fields[0] == "GROUPS") {
        printLine("[TRACKER RESPONSE] OK Groups:\n");
        if (fields.size() == 1) {
            printLine(" - None\n");
        }
        for (size_t i = 1; i < fields.size(); ++i) {
            printLine(" - " + fields[i] + "\n");
        }
    } else if (fields[0] == "REQUESTS") {
        printLine("[TRACKER RESPONSE] OK Pending requests:\n");
        if (fields.size() == 1) {
            printLine(" - None\n");
        }
        for (size_t i = 1; i < fields.size(); ++i) {
            printLine(" - " + fields[i] + "\n");
        }
    } else if (fields[0] == "FILES") {
        printLine("[TRACKER RESPONSE] OK Files:\n");
        if (fields.size() == 1) {
            printLine(" - None\n");
        }
        for (size_t i = 1; i + 1 < fields.size(); i += 2) {
            printLine(" - " + fields[i] + " (" + fields[i + 1] + " bytes)\n");
        }
    } else if (fields[0] == "GROUP_INFO" && fields.size() >= 5) {
        printLine("[TRACKER RESPONSE] OK Group: " + fields[1] + "\n");
        printLine(" - Owner: " + fields[2] + "\n");
        printLine(" - Members:\n");

        vector<string> members = splitComma(fields[3]);
        if (members.empty()) {
            printLine("   - None\n");
        } else {
            for (const string &member : members) {
                printLine("   - " + member + "\n");
            }
        }

        printLine(" - Pending requests:\n");
        vector<string> pending = splitComma(fields[4]);
        if (pending.empty()) {
            printLine("   - None\n");
        } else {
            for (const string &request : pending) {
                printLine("   - " + request + "\n");
            }
        }
    } else if (fields[0] == "MEMBERS" && fields.size() >= 3) {
        printLine("[TRACKER RESPONSE] OK Members of " + fields[1] + ":\n");
        vector<string> members = splitComma(fields[2]);
        if (members.empty()) {
            printLine(" - None\n");
        } else {
            for (const string &member : members) {
                printLine(" - " + member + "\n");
            }
        }
    } else if (fields[0] == "OWNER" && fields.size() >= 3) {
        printLine("[TRACKER RESPONSE] OK Owner of " + fields[1] + ": " + fields[2] + "\n");
    } else if (fields[0] == "MY_GROUPS") {
        printLine("[TRACKER RESPONSE] OK My groups:\n");
        if (fields.size() == 1) {
            printLine(" - None\n");
        }
        for (size_t i = 1; i < fields.size(); ++i) {
            printLine(" - " + fields[i] + "\n");
        }
    } else if (response == "OK USER") {
        printLine("[TRACKER RESPONSE] OK User created: " + currentUser + "\n");
    } else if (response == "OK LOGIN") {
        printLine("[TRACKER RESPONSE] OK Login successful\n");
    } else if (response == "OK GROUP") {
        printLine("[TRACKER RESPONSE] OK Group created\n");
    } else if (response == "OK REQUEST") {
        printLine("[TRACKER RESPONSE] OK Join request sent\n");
    } else if (response == "OK LEFT") {
        printLine("[TRACKER RESPONSE] OK Left group\n");
    } else if (fields[0] == "OK LEFT" && fields.size() >= 2) {
        printLine("[TRACKER RESPONSE] OK Left group; new owner: " + fields[1] + "\n");
    } else if (response == "OK ACCEPT") {
        printLine("[TRACKER RESPONSE] OK User added to group\n");
    } else if (response == "OK UPLOAD") {
        printLine("[TRACKER RESPONSE] OK File uploaded\n");
    } else if (response == "OK STOP") {
        printLine("[TRACKER RESPONSE] OK Stopped sharing\n");
    } else if (response == "OK LOGOUT") {
        printLine("[CLIENT] Logged out\n");
    } else if (response.rfind("ERROR ", 0) == 0) {
        printLine("[TRACKER RESPONSE] ERR " + response.substr(6) + "\n");
    } else {
        printLine("[TRACKER RESPONSE] " + response + "\n");
    }
}

void printHelp() {
    printLine(
        "Commands:\n"
        " create_user <user> <password>\n"
        " login <user> <password>\n"
        " whoami\n"
        " create_group <group>\n"
        " join_group <group>\n"
        " leave_group <group>\n"
        " list_groups\n"
        " list_requests <group>\n"
        " accept_request <group> <user>\n"
        " group_info <group>\n"
        " list_members <group>\n"
        " get_owner <group>\n"
        " my_groups\n"
        " upload_file <group> <path>\n"
        " list_files <group>\n"
        " download_file <group> <file> <destination>\n"
        " show_downloads\n"
        " stop_share <group> <file>\n"
        " logout\n"
        " quit\n");
}

void showDownloads() {
    lock_guard<mutex> lock(downloadsMutex);

    if (downloads.empty()) {
        printLine("[DOWNLOAD] No downloads\n");
        return;
    }

    for (const auto &entry : downloads) {
        const shared_ptr<DownloadStatus> &download = entry.second;

        ostringstream line;
        line << "[DOWNLOAD] [" << download->group << "] "
             << download->name << " - "
             << download->completedPieces.load() << " pieces ";

        if (!download->finished) {
            line << "in-progress";
        } else if (download->failed) {
            line << "failed";
        } else {
            line << "complete";
        }

        line << '\n';
        printLine(line.str());
    }
}

// Remove every local seeder advertised by this client.
void stopAllShares() {
    vector<SharedFile> toStop;

    {
        lock_guard<mutex> lock(sharedMutex);
        for (const auto &entry : sharedFiles) {
            toStop.push_back(entry.second);
        }
    }

    string peer = "127.0.0.1:" + to_string(peerPort);
    string response;

    for (const SharedFile &file : toStop) {
        trackerRequest("STOP\t" + currentUser + "\t" + file.group + "\t" +
                       file.name + "\t" + peer, response);
    }

    lock_guard<mutex> lock(sharedMutex);
    sharedFiles.clear();
}

// Remove all local seeders for one group when the user leaves it.
void removeGroupShares(const string &group) {
    vector<SharedFile> toStop;

    {
        lock_guard<mutex> lock(sharedMutex);
        for (const auto &entry : sharedFiles) {
            if (entry.second.group == group) {
                toStop.push_back(entry.second);
            }
        }
    }

    string peer = "127.0.0.1:" + to_string(peerPort);
    string response;

    for (const SharedFile &file : toStop) {
        trackerRequest("STOP\t" + currentUser + "\t" + file.group + "\t" +
                       file.name + "\t" + peer, response);
    }

    lock_guard<mutex> lock(sharedMutex);
    for (const SharedFile &file : toStop) {
        sharedFiles.erase(fileKey(file.group, file.name));
    }
}

bool destinationLooksValid(const string &path) {
    if (path.empty()) {
        return false;
    }

    // The destination is intentionally allowed to be absolute or relative.
    // Reject only obvious traversal components rather than restricting normal paths.
    if (path == ".." || path.rfind("../", 0) == 0 || path.find("/../") != string::npos) {
        return false;
    }

    return true;
}

void commandLoop() {
    string line;

    while (running) {
        printPrompt();

        if (!getline(cin, line)) {
            break;
        }

        vector<string> args = normalizeCommand(shellWords(line));
        if (args.empty()) {
            continue;
        }

        // Every command has an explicit argument-count check so malformed input
        // is rejected without indexing past the vector.
        const string &command = args[0];

        if (command == "quit") {
            if (!currentUser.empty()) {
                stopAllShares();
                string response;
                trackerRequest("LOGOUT\t" + currentUser, response);
                currentUser.clear();
            }

            running = false;
            break;
        }

        if (command == "help") {
            if (args.size() != 1) {
                printLine("[CLIENT] Usage: help\n");
            } else {
                printHelp();
            }
            continue;
        }

        if (command == "whoami") {
            if (args.size() != 1) {
                printLine("[CLIENT] Usage: whoami\n");
            } else if (currentUser.empty()) {
                printLine("[CLIENT] No user logged in\n");
            } else {
                printLine("[CLIENT] Current user: " + currentUser + "\n");
            }
            continue;
        }

        if (command == "create_user") {
            if (args.size() != 3) {
                printLine("[CLIENT] Usage: create_user <user> <password>\n");
                continue;
            }

            if (!currentUser.empty()) {
                printLine("[CLIENT] Already logged in as " + currentUser +
                          ". Logout before creating another user.\n");
                continue;
            }

            string response;
            if (trackerRequest("CREATE_USER\t" + args[1] + "\t" + args[2], response)) {
                // printResponse uses currentUser for the success text, so temporarily
                // use the created ID without establishing a login session.
                string oldUser = currentUser;
                currentUser = args[1];
                printResponse(response);
                currentUser = oldUser;
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }

            continue;
        }

        if (command == "login") {
            if (args.size() != 3) {
                printLine("[CLIENT] Usage: login <user> <password>\n");
                continue;
            }

            if (!currentUser.empty()) {
                if (currentUser == args[1]) {
                    printLine("[CLIENT] Already logged in as " + currentUser + "\n");
                } else {
                    printLine("[CLIENT] User " + currentUser +
                              " is already logged in. Logout before logging in as " +
                              args[1] + ".\n");
                }
                continue;
            }

            string response;
            if (!trackerRequest("LOGIN\t" + args[1] + "\t" + args[2] + "\t127.0.0.1:" +
                                to_string(peerPort), response)) {
                printLine("[CLIENT] ERROR tracker unavailable\n");
                continue;
            }

            printResponse(response);
            if (response == "OK LOGIN") {
                currentUser = args[1];
                ++sessionGeneration;
            }

            continue;
        }

        if (command == "logout") {
            if (args.size() != 1) {
                printLine("[CLIENT] Usage: logout\n");
                continue;
            }

            if (currentUser.empty()) {
                printLine("[CLIENT] No user logged in\n");
                continue;
            }

            stopAllShares();
            string response;
            trackerRequest("LOGOUT\t" + currentUser, response);
            currentUser.clear();
            ++sessionGeneration;
            printLine("[CLIENT] Logged out\n");
            continue;
        }

        // All remaining commands require an authenticated local session.
        if (currentUser.empty()) {
            printLine("[CLIENT] ERROR login required\n");
            continue;
        }

        string response;

        if (command == "list_groups") {
            if (args.size() != 1) {
                printLine("[CLIENT] Usage: list_groups\n");
            } else if (trackerRequest("LIST_GROUPS\t" + currentUser, response)) {
                printResponse(response);
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }
        } else if (command == "create_group") {
            if (args.size() != 2) {
                printLine("[CLIENT] Usage: create_group <group>\n");
            } else if (trackerRequest("CREATE_GROUP\t" + currentUser + "\t" + args[1], response)) {
                printResponse(response);
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }
        } else if (command == "join_group") {
            if (args.size() != 2) {
                printLine("[CLIENT] Usage: join_group <group>\n");
            } else if (trackerRequest("JOIN_GROUP\t" + currentUser + "\t" + args[1], response)) {
                printResponse(response);
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }
        } else if (command == "leave_group") {
            if (args.size() != 2) {
                printLine("[CLIENT] Usage: leave_group <group>\n");
            } else {
                string request = "LEAVE_GROUP\t" + currentUser + "\t" + args[1] +
                                 "\t127.0.0.1:" + to_string(peerPort);
                if (trackerRequest(request, response)) {
                    if (response.rfind("OK LEFT", 0) == 0) {
                        removeGroupShares(args[1]);
                    }
                    printResponse(response);
                } else {
                    printLine("[CLIENT] ERROR tracker unavailable\n");
                }
            }
        } else if (command == "list_requests") {
            if (args.size() != 2) {
                printLine("[CLIENT] Usage: list_requests <group>\n");
            } else if (trackerRequest("LIST_REQUESTS\t" + currentUser + "\t" + args[1], response)) {
                printResponse(response);
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }
        } else if (command == "accept_request") {
            if (args.size() != 3) {
                printLine("[CLIENT] Usage: accept_request <group> <user>\n");
            } else if (trackerRequest("ACCEPT\t" + currentUser + "\t" + args[1] + "\t" + args[2], response)) {
                printResponse(response);
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }
        } else if (command == "group_info") {
            if (args.size() != 2) {
                printLine("[CLIENT] Usage: group_info <group>\n");
            } else if (trackerRequest("GROUP_INFO\t" + currentUser + "\t" + args[1], response)) {
                printResponse(response);
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }
        } else if (command == "list_members") {
            if (args.size() != 2) {
                printLine("[CLIENT] Usage: list_members <group>\n");
            } else if (trackerRequest("LIST_MEMBERS\t" + currentUser + "\t" + args[1], response)) {
                printResponse(response);
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }
        } else if (command == "get_owner") {
            if (args.size() != 2) {
                printLine("[CLIENT] Usage: get_owner <group>\n");
            } else if (trackerRequest("GET_OWNER\t" + currentUser + "\t" + args[1], response)) {
                printResponse(response);
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }
        } else if (command == "my_groups") {
            if (args.size() != 1) {
                printLine("[CLIENT] Usage: my_groups\n");
            } else if (trackerRequest("MY_GROUPS\t" + currentUser, response)) {
                printResponse(response);
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }
        } else if (command == "upload_file") {
            if (args.size() != 3) {
                printLine("[CLIENT] Usage: upload_file <group> <path>\n");
                continue;
            }

            SharedFile file;
            if (!makeUpload(args[1], args[2], file)) {
                printLine("[CLIENT] ERROR cannot read file\n");
                continue;
            }

            string wholeHash = sha1_file(file.path);
            string peer = "127.0.0.1:" + to_string(peerPort);
            string request = "UPLOAD\t" + currentUser + "\t" + file.group +
                             "\t" + file.name + "\t" + to_string(file.size) +
                             "\t" + wholeHash + "\t" + joinComma(file.pieceHashes) +
                             "\t" + peer;

            if (!trackerRequest(request, response)) {
                printLine("[CLIENT] ERROR tracker unavailable\n");
                continue;
            }

            if (response == "OK UPLOAD") {
                lock_guard<mutex> lock(sharedMutex);
                stoppedShares.erase(fileKey(file.group, file.name));
                sharedFiles[fileKey(file.group, file.name)] = file;
            }

            printResponse(response);
        } else if (command == "list_files") {
            if (args.size() != 2) {
                printLine("[CLIENT] Usage: list_files <group>\n");
            } else if (trackerRequest("LIST_FILES\t" + currentUser + "\t" + args[1], response)) {
                printResponse(response);
            } else {
                printLine("[CLIENT] ERROR tracker unavailable\n");
            }
        } else if (command == "download_file") {
            if (args.size() != 4) {
                printLine("[CLIENT] Usage: download_file <group> <file> <destination>\n");
                continue;
            }

            if (!destinationLooksValid(args[3])) {
                printLine("[CLIENT] ERROR invalid destination path\n");
                continue;
            }

            if (!trackerRequest("GET_META\t" + currentUser + "\t" + args[1] + "\t" + args[2], response)) {
                printLine("[CLIENT] ERROR tracker unavailable\n");
                continue;
            }

            vector<string> metadata = splitTab(response);
            if (metadata.size() < 7 || metadata[0] != "META") {
                printResponse(response);
                continue;
            }

            vector<string> pieceHashes = splitComma(metadata[5]);
            vector<string> peers = splitComma(metadata[6]);

            if (peers.empty() && !pieceHashes.empty()) {
                printLine("[CLIENT] ERROR no active seeders for this file\n");
                continue;
            }

            auto status = make_shared<DownloadStatus>();
            status->group = args[1];
            status->name = args[2];
            status->destination = args[3];
            status->totalPieces = static_cast<int>(pieceHashes.size());

            {
                lock_guard<mutex> lock(downloadsMutex);
                downloads[downloadKey(args[1], args[2], args[3])] = status;
            }

            printLine("[CLIENT] Download started: " + args[2] + " -> " + args[3] + "\n");

            unsigned long long generation = sessionGeneration.load();
            thread(downloadThread, status, currentUser, args[1], args[2], args[3],
                   atoll(metadata[3].c_str()), metadata[4], pieceHashes, peers, generation).detach();
        } else if (command == "show_downloads") {
            if (args.size() != 1) {
                printLine("[CLIENT] Usage: show_downloads\n");
            } else {
                showDownloads();
            }
        } else if (command == "stop_share") {
            if (args.size() != 3) {
                printLine("[CLIENT] Usage: stop_share <group> <file>\n");
                continue;
            }

            string peer = "127.0.0.1:" + to_string(peerPort);
            string request = "STOP\t" + currentUser + "\t" + args[1] +
                             "\t" + args[2] + "\t" + peer;

            if (!trackerRequest(request, response)) {
                printLine("[CLIENT] ERROR tracker unavailable\n");
                continue;
            }

            if (response == "OK STOP") {
                lock_guard<mutex> lock(sharedMutex);
                string key = fileKey(args[1], args[2]);
                stoppedShares.insert(key);
                sharedFiles.erase(key);
            }

            printResponse(response);
        } else {
            printLine("[CLIENT] ERROR unknown command\n");
        }
    }
}

