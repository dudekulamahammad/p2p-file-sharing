#include "client.h"

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    if (argc < 3) {
        cerr << "Usage: ./client <IP:PORT> tracker <info.txt>" << endl;
        return 1;
    }

    string peerEndpoint = argv[1];
    size_t colon = peerEndpoint.find(':');
    if (colon == string::npos) {
        cerr << "Invalid peer endpoint. Use IP:PORT." << endl;
        return 1;
    }

    trackerHost = peerEndpoint.substr(0, colon);
    int requestedPeerPort = atoi(peerEndpoint.substr(colon + 1).c_str());

    string infoPath = (argc >= 4) ? argv[3] : argv[2];
    ifstream info(infoPath);
    if (!info) {
        cerr << "Unable to read tracker info file: " << infoPath << endl;
        return 1;
    }

    string number;
    string portText;
    while (info >> number >> portText) {
        if (number == "1") {
            trackerPorts.push_back(atoi(portText.c_str()));
        } else if (number == "2") {
            trackerPorts.push_back(atoi(portText.c_str()));
        }
    }

    if (trackerPorts.empty()) {
        cerr << "No tracker ports found in " << infoPath << endl;
        return 1;
    }

    peerListenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (peerListenSocket < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(peerListenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in peerAddress{};
    peerAddress.sin_family = AF_INET;
    peerAddress.sin_addr.s_addr = INADDR_ANY;
    peerAddress.sin_port = htons(requestedPeerPort);

    if (bind(peerListenSocket,
             reinterpret_cast<sockaddr *>(&peerAddress),
             sizeof(peerAddress)) < 0) {
        cerr << "[CLIENT] Requested peer port " << requestedPeerPort
             << " unavailable, using an automatic port\n";

        peerAddress.sin_port = 0;
        if (bind(peerListenSocket,
                 reinterpret_cast<sockaddr *>(&peerAddress),
                 sizeof(peerAddress)) < 0) {
            perror("peer bind");
            close(peerListenSocket);
            return 1;
        }
    }

    if (listen(peerListenSocket, 64) < 0) {
        perror("listen");
        close(peerListenSocket);
        return 1;
    }

    socklen_t addressLength = sizeof(peerAddress);
    getsockname(peerListenSocket,
                reinterpret_cast<sockaddr *>(&peerAddress),
                &addressLength);
    peerPort = ntohs(peerAddress.sin_port);

    printLine("[CLIENT] My listening IP: 127.0.0.1, Port: " +
              to_string(peerPort) + "\n");

    thread(peerWorker).detach();

    string probe;
    trackerRequest("LIST_GROUPS\t", probe);

    commandLoop();

    running = false;
    shutdown(peerListenSocket, SHUT_RDWR);
    close(peerListenSocket);
    return 0;
}
