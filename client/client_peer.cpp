#include "client.h"

void printPrompt() {
    if (!running) {
        return;
    }

    lock_guard<mutex> lock(outputMutex);
    cout << (currentUser.empty() ? "client" : currentUser) << "> " << flush;
}

// Serve one requested piece directly to another client.
void servePiece(int fd) {
    setSocketTimeout(fd, 15);
    string line;

    if (!recvLine(fd, line)) {
        close(fd);
        return;
    }

    vector<string> fields = splitTab(line);
    if (fields.size() != 4 || fields[0] != "GETPIECE") {
        sendLine(fd, "ERROR\tbad request");
        close(fd);
        return;
    }

    int pieceIndex = atoi(fields[3].c_str());
    SharedFile file;
    bool found = false;

    {
        lock_guard<mutex> lock(sharedMutex);
        string key = fileKey(fields[1], fields[2]);

        // stop_share must immediately disable this client as a seeder.
        // Do not serve a piece even if another client cached this peer
        // endpoint before stop_share was issued.
        if (stoppedShares.count(key) != 0) {
            found = false;
        } else {
            auto it = sharedFiles.find(key);
            if (it != sharedFiles.end()) {
                file = it->second;
                found = true;
            }
        }
    }

    if (!found || pieceIndex < 0 || pieceIndex >= static_cast<int>(file.pieceHashes.size())) {
        sendLine(fd, "ERROR\tpiece unavailable");
        close(fd);
        return;
    }

    int input = open(file.path.c_str(), O_RDONLY);
    if (input < 0) {
        sendLine(fd, "ERROR\tpiece unavailable");
        close(fd);
        return;
    }

    off_t offset = static_cast<off_t>(pieceIndex) * PIECE_SIZE;
    long long remaining = file.size - static_cast<long long>(offset);
    if (remaining < 0) {
        close(input);
        sendLine(fd, "ERROR\tpiece unavailable");
        close(fd);
        return;
    }

    size_t wanted = static_cast<size_t>(min<long long>(PIECE_SIZE, remaining));
    vector<unsigned char> buffer(wanted);
    size_t received = 0;

    while (received < wanted) {
        ssize_t count = pread(input, buffer.data() + received,
                              wanted - received, offset + received);
        if (count <= 0) {
            break;
        }
        received += static_cast<size_t>(count);
    }

    close(input);

    if (received != wanted) {
        sendLine(fd, "ERROR\tpiece unavailable");
        close(fd);
        return;
    }

    if (!sendLine(fd, "PIECE\t" + to_string(wanted)) ||
        !sendAll(fd, buffer.data(), wanted)) {
        close(fd);
        return;
    }

    close(fd);
}

// Accept peer connections and give every piece request its own worker thread.
void peerWorker() {
    while (running) {
        sockaddr_in clientAddress{};
        socklen_t length = sizeof(clientAddress);
        int fd = accept(peerListenSocket,
                        reinterpret_cast<sockaddr *>(&clientAddress),
                        &length);

        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!running) {
                break;
            }
            continue;
        }

        thread(servePiece, fd).detach();
    }
}

// Request one piece from one peer and verify its SHA1 before writing it.
bool receivePiece(const string &endpoint,
                  const string &group,
                  const string &name,
                  int pieceIndex,
                  const string &expectedHash,
                  int output,
                  long long offset) {
    size_t colon = endpoint.find(':');
    if (colon == string::npos) {
        return false;
    }

    string host = endpoint.substr(0, colon);
    int port = atoi(endpoint.substr(colon + 1).c_str());
    if (port <= 0 || port > 65535) {
        return false;
    }

    int fd = connectTo(host, port);
    if (fd < 0) {
        return false;
    }

    if (!sendLine(fd, "GETPIECE\t" + group + "\t" + name + "\t" +
                       to_string(pieceIndex))) {
        close(fd);
        return false;
    }

    string header;
    if (!recvLine(fd, header)) {
        close(fd);
        return false;
    }

    vector<string> fields = splitTab(header);
    if (fields.size() != 2 || fields[0] != "PIECE") {
        close(fd);
        return false;
    }

    unsigned long long rawSize = strtoull(fields[1].c_str(), nullptr, 10);
    if (rawSize > PIECE_SIZE) {
        close(fd);
        return false;
    }

    size_t pieceSize = static_cast<size_t>(rawSize);
    vector<unsigned char> buffer(pieceSize);
    size_t received = 0;

    while (received < pieceSize) {
        ssize_t count = recv(fd, buffer.data() + received,
                             pieceSize - received, 0);
        if (count <= 0) {
            close(fd);
            return false;
        }
        received += static_cast<size_t>(count);
    }

    close(fd);

    if (sha1_buffer(buffer.data(), buffer.size()) != expectedHash) {
        // The corrupted piece is discarded. The caller chooses another peer
        // or retries the same peer rather than writing bad bytes to disk.
        return false;
    }

    size_t written = 0;
    while (written < buffer.size()) {
        ssize_t count = pwrite(output, buffer.data() + written,
                               buffer.size() - written,
                               offset + written);
        if (count <= 0) {
            return false;
        }
        written += static_cast<size_t>(count);
    }

    return true;
}

