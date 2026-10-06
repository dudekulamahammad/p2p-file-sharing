#include "client.h"

void printDownloadCompletion(const DownloadStatus &status) {
    if (status.failed) {
        printLine("\n[DOWNLOAD] [" + status.group + "] " + status.name +
                  " download failed\n");
    } else {
        printLine("\n[DOWNLOAD] [" + status.group + "] " + status.name +
                  " completed successfully\n");
    }
}

// Advertise a completed download as a new seeder, as required for peer-to-peer chaining.
bool advertiseSeeder(const string &seederUser,
                     const string &group,
                     const string &name,
                     const string &path,
                     long long size,
                     const vector<string> &pieceHashes) {
    string response;
    string peer = "127.0.0.1:" + to_string(peerPort);
    string request = "UPLOAD\t" + seederUser + "\t" + group + "\t" + name +
                     "\t" + to_string(size) + "\t" + sha1_file(path) +
                     "\t" + joinComma(pieceHashes) + "\t" + peer;

    if (!trackerRequest(request, response)) {
        return false;
    }

    return response == "OK UPLOAD";
}

// Download one file using up to four workers. Each piece is assigned only once.
void downloadThread(shared_ptr<DownloadStatus> status,
                    const string &seederUser,
                    const string &group,
                    const string &name,
                    const string &destination,
                    long long size,
                    const string &wholeHash,
                    const vector<string> &pieceHashes,
                    const vector<string> &peers,
                    unsigned long long generation) {
    int output = open(destination.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);

    if (output < 0 || ftruncate(output, size) < 0) {
        if (output >= 0) {
            close(output);
        }

        status->failed = true;
        status->finished = true;
        printDownloadCompletion(*status);
        return;
    }

    if (pieceHashes.empty()) {
        // Empty files are valid. Their complete SHA1 is still verified below.
    }

    atomic<int> nextPiece(0);
    int workerCount = min(4, static_cast<int>(pieceHashes.size()));
    if (workerCount == 0) {
        workerCount = 1;
    }

    vector<thread> workers;

    for (int worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&, worker]() {
            while (true) {
                int index = nextPiece.fetch_add(1);
                if (index >= static_cast<int>(pieceHashes.size())) {
                    break;
                }

                bool pieceDone = false;

                // Try every advertised peer. A failed/corrupt piece is retried
                // so a temporary failure from one seeder does not abort the file.
                for (int attempt = 0;
                     attempt < MAX_PIECE_RETRIES && !pieceDone;
                     ++attempt) {
                    for (size_t peerOffset = 0;
                         peerOffset < peers.size() && !pieceDone;
                         ++peerOffset) {
                        size_t peerIndex =
                            (peerOffset + static_cast<size_t>(worker) +
                             static_cast<size_t>(attempt)) % peers.size();

                        pieceDone = receivePiece(
                            peers[peerIndex], group, name, index,
                            pieceHashes[index], output,
                            static_cast<long long>(index) * PIECE_SIZE);
                    }
                }

                if (pieceDone) {
                    status->completedPieces++;
                } else {
                    status->failed = true;
                }
            }
        });
    }

    for (thread &worker : workers) {
        worker.join();
    }

    if (!status->failed) {
        if (sha1_file(destination) != wholeHash) {
            status->failed = true;
        }
    }

    close(output);
    status->finished = true;

    bool allowReshare = false;
    if (!status->failed && !seederUser.empty() &&
        sessionGeneration.load() == generation) {
        lock_guard<mutex> lock(sharedMutex);
        allowReshare = (stoppedShares.count(fileKey(group, name)) == 0);
    }

    if (allowReshare) {
        // Rebuild the piece hash list locally for the new seeder. The list is
        // already known from the tracker, so no full-file buffering is needed.
        SharedFile downloaded;
        downloaded.path = destination;
        downloaded.group = group;
        downloaded.name = name;
        downloaded.size = size;
        downloaded.pieceHashes = pieceHashes;

        {
            lock_guard<mutex> lock(sharedMutex);
            sharedFiles[fileKey(group, name)] = downloaded;
        }

        advertiseSeeder(seederUser, group, name, destination, size, pieceHashes);
    }

    printDownloadCompletion(*status);
}

// Read an upload file in 512 KiB chunks and compute each piece hash.
bool makeUpload(const string &group, const string &path, SharedFile &result) {
    struct stat fileStat{};

    if (stat(path.c_str(), &fileStat) < 0 || !S_ISREG(fileStat.st_mode)) {
        return false;
    }

    int input = open(path.c_str(), O_RDONLY);
    if (input < 0) {
        return false;
    }

    result.path = path;
    result.group = group;
    result.name = baseName(path);
    result.size = fileStat.st_size;
    result.pieceHashes.clear();

    unsigned char buffer[PIECE_SIZE];

    while (true) {
        ssize_t count = read(input, buffer, PIECE_SIZE);

        if (count < 0) {
            close(input);
            return false;
        }

        if (count == 0) {
            break;
        }

        result.pieceHashes.push_back(sha1_buffer(buffer, static_cast<size_t>(count)));
    }

    close(input);
    return true;
}

