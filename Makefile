CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread

TRACKER_SRCS = tracker/tracker_state.cpp tracker/tracker_network.cpp tracker/tracker_sync.cpp tracker/tracker_server.cpp tracker/tracker_main.cpp tracker/sha1.cpp
CLIENT_SRCS = client/client_state.cpp client/client_network.cpp client/client_peer.cpp client/client_download.cpp client/client_commands.cpp client/client_main.cpp client/sha1.cpp

all: tracker/tracker client/client

tracker/tracker: $(TRACKER_SRCS) tracker/tracker.h tracker/sha1.h
	$(CXX) $(CXXFLAGS) $(TRACKER_SRCS) -o tracker/tracker

client/client: $(CLIENT_SRCS) client/client.h client/sha1.h
	$(CXX) $(CXXFLAGS) $(CLIENT_SRCS) -o client/client

# The test script rebuilds first and then performs an end-to-end run.
test: all
	bash tests/run_tests.sh

clean:
	rm -f tracker/tracker client/client
