#include "tracker.h"

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

// Receive one complete line while correctly handling partial TCP receives.
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

string makeError(const string &message) {
    return "ERROR " + message;
}

string getPeerAddress(int fd) {
    sockaddr_in address{};
    socklen_t length = sizeof(address);

    if (getpeername(fd, reinterpret_cast<sockaddr *>(&address), &length) == 0) {
        char ip[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip)) != nullptr) {
            return string(ip) + ":" + to_string(ntohs(address.sin_port));
        }
    }

    return "unknown";
}

bool containsWhitespace(const string &text) {
    for (unsigned char ch : text) {
        if (isspace(ch)) {
            return true;
        }
    }
    return false;
}

// User/group/file names are protocol fields, so tabs/newlines are rejected.
bool validField(const string &text, size_t maxLength) {
    return !text.empty() && text.size() <= maxLength &&
           text.find('\t') == string::npos &&
           text.find('\n') == string::npos &&
           text.find('\r') == string::npos;
}

bool validUserOrPassword(const string &text) {
    return validField(text, 4096) && !containsWhitespace(text);
}

bool validGroupName(const string &text) {
    return validField(text, 4096);
}

void setSocketTimeout(int fd, int seconds) {
    timeval timeout{};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

