#include "client.h"

void printLine(const string &message) {
    lock_guard<mutex> lock(outputMutex);
    cout << message << flush;
}

// Send the complete buffer even when TCP performs partial sends.
bool sendAll(int fd, const void *data, size_t size) {
    const char *ptr = static_cast<const char *>(data);

    while (size > 0) {
        ssize_t sent = send(fd, ptr, size, 0);
        if (sent <= 0) {
            return false;
        }

        ptr += sent;
        size -= static_cast<size_t>(sent);
    }

    return true;
}

// Send one newline-delimited control message.
bool sendLine(int fd, const string &message) {
    string data = message + "\n";
    return sendAll(fd, data.data(), data.size());
}

// Receive one complete control line while handling partial TCP receives.
bool recvLine(int fd, string &out) {
    out.clear();
    char ch;

    while (out.size() < MAX_LINE) {
        ssize_t received = recv(fd, &ch, 1, 0);
        if (received <= 0) {
            return false;
        }

        if (ch == '\n') {
            return true;
        }

        out.push_back(ch);
    }

    return false;
}

vector<string> splitTab(const string &text) {
    vector<string> result;
    size_t start = 0;

    while (true) {
        size_t pos = text.find('\t', start);
        if (pos == string::npos) {
            result.push_back(text.substr(start));
            break;
        }

        result.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }

    return result;
}

vector<string> splitComma(const string &text) {
    vector<string> result;
    if (text.empty()) {
        return result;
    }

    size_t start = 0;
    while (true) {
        size_t pos = text.find(',', start);
        if (pos == string::npos) {
            result.push_back(text.substr(start));
            break;
        }

        result.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }

    return result;
}

string joinComma(const vector<string> &values) {
    string result;

    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            result += ',';
        }
        result += values[i];
    }

    return result;
}

string fileKey(const string &group, const string &name) {
    return group + "\n" + name;
}

string downloadKey(const string &group, const string &name, const string &destination) {
    return group + "\n" + name + "\n" + destination;
}

string baseName(const string &path) {
    size_t slash = path.find_last_of('/');
    if (slash == string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

// Simple shell-like tokenization. Quotes allow paths/group names containing spaces.
vector<string> shellWords(const string &line) {
    vector<string> words;
    string current;
    bool quoted = false;
    char quote = 0;

    for (char ch : line) {
        if (quoted) {
            if (ch == quote) {
                quoted = false;
            } else {
                current += ch;
            }
        } else if (ch == '\'' || ch == '"') {
            quoted = true;
            quote = ch;
        } else if (ch == ' ' || ch == '\t') {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        } else {
            current += ch;
        }
    }

    if (quoted) {
        return {};
    }

    if (!current.empty()) {
        words.push_back(current);
    }

    return words;
}

// Convert the older space-separated command spelling to the required underscore form.
vector<string> normalizeCommand(vector<string> args) {
    if (args.empty()) {
        return args;
    }

    if (args[0] == "create" && args.size() >= 2) {
        if (args[1] == "user") args[0] = "create_user";
        else if (args[1] == "group") args[0] = "create_group";
    } else if (args[0] == "join" && args.size() >= 2 && args[1] == "group") {
        args[0] = "join_group";
    } else if (args[0] == "leave" && args.size() >= 2 && args[1] == "group") {
        args[0] = "leave_group";
    } else if (args[0] == "list" && args.size() >= 2) {
        if (args[1] == "groups") args[0] = "list_groups";
        else if (args[1] == "requests") args[0] = "list_requests";
        else if (args[1] == "files") args[0] = "list_files";
        else if (args[1] == "members") args[0] = "list_members";
    } else if (args[0] == "accept" && args.size() >= 2 && args[1] == "request") {
        args[0] = "accept_request";
    } else if (args[0] == "upload" && args.size() >= 2 && args[1] == "file") {
        args[0] = "upload_file";
    } else if (args[0] == "download" && args.size() >= 2 && args[1] == "file") {
        args[0] = "download_file";
    } else if (args[0] == "show" && args.size() >= 2 && args[1] == "downloads") {
        args[0] = "show_downloads";
    } else if (args[0] == "stop" && args.size() >= 2 && args[1] == "share") {
        args[0] = "stop_share";
    } else if (args[0] == "group" && args.size() >= 2 && args[1] == "info") {
        args[0] = "group_info";
    } else if (args[0] == "get" && args.size() >= 2 && args[1] == "owner") {
        args[0] = "get_owner";
    } else if ((args[0] == "show_group" || args[0] == "group_details") && args.size() >= 2) {
        args[0] = "group_info";
    } else if ((args[0] == "group_members" || args[0] == "who_are_members") && args.size() >= 2) {
        args[0] = "list_members";
    } else if ((args[0] == "group_owner" || args[0] == "who_is_owner") && args.size() >= 2) {
        args[0] = "get_owner";
    } else if (args[0] == "pending_requests" && args.size() >= 2) {
        args[0] = "list_requests";
    }

    return args;
}

void setSocketTimeout(int fd, int seconds) {
    timeval timeout{};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

int connectTo(const string &host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    setSocketTimeout(fd, 15);

    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

// Send a request to the active tracker. If it is down, automatically try the other tracker.
bool trackerRequest(const string &request, string &response) {
    if (trackerPorts.empty()) {
        return false;
    }

    for (size_t attempt = 0; attempt < trackerPorts.size(); ++attempt) {
        int index = (currentTracker + static_cast<int>(attempt)) % trackerPorts.size();

        if (lastAnnouncedTracker != index) {
            printLine("[CLIENT] Trying tracker " + trackerHost + ":" +
                      to_string(trackerPorts[index]) + "\n");
        }

        int fd = connectTo(trackerHost, trackerPorts[index]);
        if (fd < 0) {
            continue;
        }

        if (sendLine(fd, request) && recvLine(fd, response)) {
            close(fd);
            currentTracker = index;

            if (lastAnnouncedTracker != index) {
                string role = (index == 0) ? "PRIMARY" : "BACKUP";
                printLine("[CLIENT] Connected to tracker at " + trackerHost + ":" +
                          to_string(trackerPorts[index]) + "\n");
                printLine("[CLIENT] Connected to " + role + " tracker at " +
                          trackerHost + ":" + to_string(trackerPorts[index]) + "\n");
                lastAnnouncedTracker = index;
            }

            return true;
        }

        close(fd);
    }

    return false;
}

