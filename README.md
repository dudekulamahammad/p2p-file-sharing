######   Peer-to-Peer Distributed File Sharing System    #####

## 1. Submission contents

```text
2026202016_A3/
├── Makefile
├── README.md
├── TECHNICAL_REPORT.md
├── client/
│   ├── client.h
│   ├── client_main.cpp
│   ├── client_state.cpp
│   ├── client_network.cpp
│   ├── client_peer.cpp
│   ├── client_download.cpp
│   ├── client_commands.cpp
│   ├── sha1.cpp
│   ├── sha1.h
│   └── info.txt
└── tracker/
    ├── tracker.h
    ├── tracker_main.cpp
    ├── tracker_state.cpp
    ├── tracker_network.cpp
    ├── tracker_sync.cpp
    ├── tracker_server.cpp
    ├── sha1.cpp
    ├── sha1.h
    └── info.txt
```

The implementation is intentionally modular. The public declarations and shared state are kept in `client.h`/`tracker.h`, while networking, state, synchronization, peer transfer, downloading and command handling are separated into focused source files.

The implementation uses C++17, POSIX sockets/file calls, threads, mutexes and the supplied local SHA1 implementation. It does not use `system()`, `popen()`, the `exec*()` family, `std::filesystem`, a database library or an external torrent implementation.

---

## 2. Module organization

### Tracker modules

| File | Responsibility |
|---|---|
| `tracker.h` | Shared structures, global declarations and function interfaces |
| `tracker_main.cpp` | Program entry point, argument parsing and tracker startup |
| `tracker_state.cpp` | Shared tracker state such as users, groups, files and synchronization variables |
| `tracker_network.cpp` | TCP send/receive helpers, validation, peer-address handling and socket timeouts |
| `tracker_sync.cpp` | Primary/backup synchronization, snapshots and synchronization records |
| `tracker_server.cpp` | Tracker/client connection handling, command processing and server accept loop |
| `sha1.cpp`, `sha1.h` | Local SHA-1 implementation used for file metadata/integrity |

### Client modules

| File | Responsibility |
|---|---|
| `client.h` | Shared structures, global declarations and function interfaces |
| `client_main.cpp` | Program entry point, startup, peer server creation and command-loop startup |
| `client_state.cpp` | Current user, tracker state, shared files and download state |
| `client_network.cpp` | TCP helpers, tracker requests, failover and common path/key helpers |
| `client_peer.cpp` | Peer server, piece requests and peer-to-peer piece transfer |
| `client_download.cpp` | Upload metadata creation, multi-threaded downloading, SHA-1 verification and seeder advertisement |
| `client_commands.cpp` | Command parsing, validation, help, download status and share/session commands |
| `sha1.cpp`, `sha1.h` | Local SHA-1 implementation |

This separation is for maintainability only; it does not change the application protocol or required commands.

---

## 3. Build

From the submission directory:

```bash
make clean
make
```

Compiler flags:

```text
g++ -std=c++17 -O2 -Wall -Wextra -pthread
```

The `Makefile` explicitly lists all tracker and client modules, so the complete program is rebuilt whenever a module or shared header changes.

---

## 4. Starting the two trackers

`tracker/info.txt` contains:

```text
1 9001
2 9002
```

The normal command required by the assignment is:

```bash
./tracker/tracker tracker/info.txt tracker no
```

Run it in two terminals. The first process binds `9001` and becomes `PRIMARY`; the second process binds `9002` and becomes `BACKUP`.

Explicit startup is also supported:

```bash
./tracker/tracker tracker/info.txt 1
./tracker/tracker tracker/info.txt 2
```

The tracker console supports:

```text
quit
```

which cleanly shuts down that tracker.

---

## 5. Starting clients

Each client needs a unique peer port on the same machine:

```bash
./client/client 127.0.0.1:7001 tracker tracker/info.txt
./client/client 127.0.0.1:7002 tracker tracker/info.txt
./client/client 127.0.0.1:7003 tracker tracker/info.txt
```

The client first connects to the primary tracker. If it cannot communicate with that tracker, it automatically tries the backup tracker.

---

## 6. Required commands

The professor's underscore-separated command format is supported directly:

```text
create_user <user id> <password>
login <user id> <password>
create_group <group id>
join_group <group id>
leave_group <group id>
list_groups
list_requests <group id>
accept_request <group id> <user id>
logout
upload_file <group id> <file path>
list_files <group id>
download_file <group id> <file name> <destination path>
show_downloads
stop_share <group id> <file name>
```

Additional useful commands:

```text
whoami
group_info <group id>
list_members <group id>
get_owner <group id>
my_groups
help
quit
```

---

## 7. Login/session rules

A client process has one active login session at a time. A user must log out before changing users or creating another account from the same client process. Different client processes may use different users concurrently.

`logout` also removes the client's advertised seeders from the tracker and clears the local seeder table. `quit` closes the client without shutting down the tracker.

---

## 8. User and group management

The client validates command argument counts before sending requests, and the tracker validates protocol fields. The implementation handles duplicate users, wrong passwords, nonexistent users, duplicate groups, duplicate membership, duplicate pending requests and invalid group operations.

The group creator becomes owner and first member. `join_group` creates a pending request, and only the owner can list and accept requests.

### Owner leaving extension

When the owner leaves and another member exists, the next member in join order becomes owner. If there is no successor, the owner cannot leave. The new owner is synchronized to the other tracker.

---

## 9. Upload and file metadata

Files are split into `512 KiB` pieces. The client computes SHA-1 for every piece and the complete file. Only metadata is sent to the tracker; file bytes never pass through the tracker.

Identical content uploaded by another client adds another seeder, while different content under the same filename is rejected. Empty files are supported.

---

## 10. Download and multi-peer transfer

The tracker returns file size, whole-file SHA-1, piece SHA-1 values and active peers. The client uses up to four worker threads with atomic piece allocation. Each received piece is SHA-1 verified before being written with `pwrite()`. Failed or corrupted pieces are retried using available peers.

After all pieces are received, the complete file SHA-1 must match the tracker value. A successful downloader becomes a seeder.

---

## 11. Concurrent downloads and progress

`download_file` starts a background download and returns control to the command loop. `show_downloads` reports progress while downloads are active. Multiple files can be downloaded concurrently.

The download implementation tracks completed pieces, in-progress/complete/failed state, destination path and session generation so an old session cannot re-advertise itself after logout.

---

## 12. Stop sharing and cleanup

`stop_share <group> <file>` removes this client's endpoint from the tracker's seeder list and prevents the local peer server from continuing to serve that file. `logout` stops all local shares. Leaving a group removes that client's shares for the group.

---

## 13. Tracker synchronization and failover

The two trackers synchronize users, groups, membership/order, pending requests, file metadata and seeder lists over a dedicated TCP connection. Synchronization records include:

```text
SYNC_USER
SYNC_GROUP
SYNC_MEMBER
SYNC_PENDING
SYNC_ACCEPT
SYNC_REMOVE_MEMBER
SYNC_OWNER
SYNC_FILE
SYNC_REMOVE_PEER
SYNC_REMOVE_GROUP_PEER
```

The receiving tracker applies synchronization records without forwarding them again, preventing synchronization loops. Snapshot recovery is used when a tracker needs to recover complete metadata after reconnecting.

Clients automatically try the other tracker when the current tracker is unavailable.

---

## 14. Network and concurrency design

TCP sockets are used for:

1. client-to-tracker control messages;
2. tracker-to-tracker synchronization;
3. direct client-to-client piece transfers.

Tracker client connections are handled concurrently. The client runs a peer server and background download workers. Mutexes protect shared maps and status, while atomics protect piece allocation and progress counters.

---

## 15. Input validation and prohibited functionality

The implementation rejects malformed/empty fields, excessive field lengths and protocol-breaking tabs/newlines. It does not pass user file paths or commands to a shell.

The project does not use:

- `system()`
- `popen()`
- `exec*()` family
- `std::filesystem`
- database libraries
- external torrent implementations

---

## 16. Notes for evaluation

The modular source layout makes each major subsystem easy to inspect independently while preserving the same executable behavior and protocol. The `Makefile` shows the exact compilation units used for both tracker and client.
