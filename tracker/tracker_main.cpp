#include "tracker.h"

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    int primaryPort = 0;
    int backupPort = 0;
    int serverSocket = -1;
    int role = 0;
    bool automaticRole = false;

    if (argc >= 3 && isdigit(static_cast<unsigned char>(argv[1][0]))) {
        primaryPort = atoi(argv[1]);
        backupPort = atoi(argv[2]);
        role = (primaryPort <= backupPort) ? 1 : 2;
    } else if (argc >= 2) {
        if (!readPorts(argv[1], primaryPort, backupPort)) {
            cerr << "Usage: ./tracker <port> <peer-port> OR ./tracker <info.txt> [1|2]" << endl;
            return 1;
        }

        if (argc >= 3 && (string(argv[2]) == "1" || string(argv[2]) == "2")) {
            role = atoi(argv[2]);
        } else if (argc >= 4 && (string(argv[3]) == "1" || string(argv[3]) == "2")) {
            role = atoi(argv[3]);
        } else {
            automaticRole = true;
        }

        if (automaticRole) {
            serverSocket = makeServer(primaryPort);
            if (serverSocket >= 0) {
                role = 1;
            } else {
                serverSocket = makeServer(backupPort);
                if (serverSocket < 0) {
                    perror("bind");
                    return 1;
                }
                role = 2;
            }
        }
    } else {
        cerr << "Usage: ./tracker <port> <peer-port> OR ./tracker <info.txt> [1|2]" << endl;
        return 1;
    }

    myPort = (role == 2) ? backupPort : primaryPort;
    peerPort = (role == 2) ? primaryPort : backupPort;

    if (serverSocket < 0) {
        serverSocket = makeServer(myPort);
        if (serverSocket < 0) {
            perror("bind");
            return 1;
        }
    }

    cout << "[TRACKER] Tracker running at 127.0.0.1:" << myPort
         << ", peer at 127.0.0.1:" << peerPort << '\n';
    cout << "[TRACKER] Role set to " << (role == 1 ? "PRIMARY" : "BACKUP") << '\n';
    cout << "[SYNC] Listening for sync on port " << myPort << '\n';
    cout << "[SYNC] Waiting for peer connection...\n" << flush;

    thread([&]() {
        while (running) {
            int fd = -1;
            if (connectPeer(fd)) {
                close(fd);
                cout << "\n[SYNC] Connected to peer at 127.0.0.1:" << peerPort << '\n' << flush;
                requestSnapshot();
                break;
            }
            sleep(1);
        }
    }).detach();

    serverLoop(serverSocket);

    running = false;
    close(serverSocket);
    return 0;
}
