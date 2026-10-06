# Technical Report #
## Peer-to-Peer Distributed File Sharing System

**Language:** C++17  
**Networking:** TCP sockets  
**Concurrency:** POSIX/C++ threads and mutexes  
**Integrity:** SHA1

---

## 1. Objective

The system implements a distributed peer-to-peer file sharing architecture with two synchronized trackers and multiple clients.

The trackers maintain the control-plane metadata. Clients maintain the actual file bytes and communicate directly with one another for piece transfer.

The implementation is designed around the assignment constraints: C/C++, TCP communication, direct peer transfer, 512 KiB pieces, SHA1 integrity checking, concurrent transfers and operation while one tracker is unavailable.

---

## 2. System architecture

```text
                         metadata synchronization
                  +------------------------------+
                  |                              |
                  v                              v
          +---------------+              +---------------+
          | Tracker 1     | <----------> | Tracker 2     |
          | PRIMARY :9001 |              | BACKUP :9002  |
          +---------------+              +---------------+
                 ^                              ^
                 | control metadata            | control metadata
                 |                              |
          +------+-------+                +-----+--------+
          | Client A     | <------------> | Client B     |
          | peer :7001   |  file pieces   | peer :7002   |
          +--------------+                +--------------+
```

### Tracker responsibilities

- user registration and authentication checks;
- group and membership metadata;
- pending requests;
- ownership information;
- file metadata;
- piece hashes;
- complete-file hashes;
- active peer/seeder endpoints;
- tracker-to-tracker synchronization.

### Client responsibilities

- local command processing;
- user session state;
- peer listening socket;
- upload hashing;
- direct piece serving;
- concurrent piece downloads;
- immediate SHA1 verification;
- complete-file SHA1 verification;
- local seeder management;
- tracker failover.

---


## 3. Modular source organization

The implementation is divided into focused compilation units. This improves readability and maintainability without changing the protocol, required commands, data structures, or runtime behavior.

### 3.1 Tracker modules

| Module | Responsibility |
|---|---|
| `tracker.h` | Shared structures, global state declarations and function interfaces |
| `tracker_main.cpp` | Entry point and tracker startup |
| `tracker_state.cpp` | Users, groups, file metadata, synchronization state and shared variables |
| `tracker_network.cpp` | TCP send/receive helpers, field validation, peer-address handling and socket timeouts |
| `tracker_sync.cpp` | Primary/backup synchronization, snapshots and synchronization records |
| `tracker_server.cpp` | Client/tracker connection handling, command dispatch and accept loop |
| `sha1.cpp` / `sha1.h` | SHA-1 implementation |

### 3.2 Client modules

| Module | Responsibility |
|---|---|
| `client.h` | Shared structures, global state declarations and function interfaces |
| `client_main.cpp` | Entry point, startup and peer-server initialization |
| `client_state.cpp` | Current session, tracker state, shared files and download state |
| `client_network.cpp` | TCP helpers, tracker requests, failover and common helpers |
| `client_peer.cpp` | Peer server and direct piece requests/transfers |
| `client_download.cpp` | Upload metadata creation, multi-threaded downloads, SHA-1 verification and seeder advertisement |
| `client_commands.cpp` | Command parsing, validation, help, download status and share/session operations |
| `sha1.cpp` / `sha1.h` | SHA-1 implementation |

### 3.3 Build organization

The `Makefile` explicitly lists all modules:

```text
TRACKER_SRCS = tracker_state.cpp tracker_network.cpp tracker_sync.cpp tracker_server.cpp tracker_main.cpp sha1.cpp
CLIENT_SRCS  = client_state.cpp client_network.cpp client_peer.cpp client_download.cpp client_commands.cpp client_main.cpp sha1.cpp
```

Both executables are built with C++17, optimization, warnings and pthread support:

```text
g++ -std=c++17 -O2 -Wall -Wextra -pthread
```

The modularization is structural only: the same TCP protocol, tracker synchronization, file piece format, SHA-1 checks, commands, failover behavior and concurrency model are preserved.

## 3. Main data structures

### 3.1 Tracker `Group`

```cpp
struct Group {
    string owner;
    set<string> members;
    set<string> pending;
    vector<string> memberOrder;
};
```

`memberOrder` is required by the requested owner-transfer extension. It records the order in which users become members.

### 3.2 Tracker `FileMeta`

```cpp
struct FileMeta {
    long long size;
    string whole;
    vector<string> pieces;
    set<string> peers;
};
```

The tracker therefore knows what file exists, how large it is, the expected SHA1 values and which clients can serve it.

### 3.3 Client `SharedFile`

Stores the local path and metadata required to serve pieces to other clients.

### 3.4 Client `DownloadStatus`

Contains atomic counters and flags used by `show_downloads` while the download thread is still active.

---

## 4. Function-level design

The following functions are distributed across the modules listed above rather than being kept in one monolithic source file.

### Tracker

| Function | Purpose |
|---|---|
| `sendAll()` | `tracker_network.cpp` | Handles partial TCP sends |
| `recvLine()` | `tracker_network.cpp` | Reassembles newline-delimited TCP messages |
| `validField()` / validation helpers | `tracker_network.cpp` | Validates protocol fields and input |
| `makeSnapshot()` | `tracker_sync.cpp` | Builds complete tracker state for synchronization recovery |
| `applySnapshot()` | `tracker_sync.cpp` | Applies recovered tracker state |
| `syncRecord()` | `tracker_sync.cpp` | Sends one metadata update to the peer tracker |
| `applyRecord()` | `tracker_sync.cpp` | Applies a synchronization update without forwarding it |
| `handleConnection()` | `tracker_server.cpp` | Handles tracker/client commands and tracker sync messages |
| `serverLoop()` | `tracker_server.cpp` | Accepts concurrent TCP connections |
| `readPorts()` | `tracker_server.cpp` | Reads the two tracker ports from `info.txt` |
| `makeServer()` | `tracker_server.cpp` | Creates/binds/listens on a tracker TCP socket |

### Client

| Function | Purpose |
|---|---|
| `trackerRequest()` | `client_network.cpp` | Sends a request and performs primary/backup failover |
| `servePiece()` | `client_peer.cpp` | Handles one incoming peer piece request |
| `peerWorker()` | `client_peer.cpp` | Accepts concurrent peer connections |
| `receivePiece()` | `client_peer.cpp` | Downloads and SHA1-verifies one piece |
| `downloadThread()` | `client_download.cpp` | Performs concurrent piece download and final hash verification |
| `makeUpload()` | `client_download.cpp` | Reads a local file in 512 KiB chunks and computes piece hashes |
| `advertiseSeeder()` | `client_download.cpp` | Registers a successfully downloaded file as a new seeder |
| `stopAllShares()` | `client_commands.cpp` | Removes all local seeders during logout/quit |
| `removeGroupShares()` | `client_commands.cpp` | Removes seeders belonging to a group when leaving it |
| `showDownloads()` | `client_commands.cpp` | Displays current piece-level download progress |
| `commandLoop()` | `client_commands.cpp` | Validates and executes user commands |

---

## 5. Authentication and session behavior

The client maintains exactly one active local user string.

The command layer rejects:

```text
login <same user>
login <different user>
create_user <another user>
```

while a user is already logged in.

The user must execute:

```text
logout
```

before another login or registration operation can be made from that client.

This is a client-session rule, not a global single-user restriction: multiple client processes can simultaneously use different users, which is required for peer-to-peer tests.

The tracker verifies credentials on every authenticated operation using the username supplied by the client protocol.

---

## 6. Group algorithm

### Create group

1. Verify the user exists.
2. Verify the group ID is unused.
3. Create the group.
4. Set creator as owner.
5. Insert creator into members and `memberOrder`.
6. Synchronize the creation to the other tracker.

### Join group

1. Check that group exists.
2. Reject existing members.
3. Reject duplicate pending requests.
4. Insert the user into `pending`.
5. Synchronize the pending request.

### Accept request

1. Verify the caller is the owner.
2. Verify the requested user has a pending request.
3. Remove the request from `pending`.
4. Add the user to `members` and `memberOrder`.
5. Synchronize the membership change.

### Owner leave extension

The owner is located in `memberOrder`. The following member becomes owner. The old owner is removed from membership and ordering.

If the owner is the final member, the leave is rejected because there is no successor.

---

## 7. Upload algorithm

For a file of size `S`:

```text
piece 0: bytes [0, 512 KiB)
piece 1: bytes [512 KiB, 1024 KiB)
...
last piece: remaining bytes
```

The client uses a fixed 512 KiB buffer and calculates:

```text
H_i = SHA1(piece_i)
H_file = SHA1(complete file)
```

The tracker receives the metadata but never receives the file contents.

Memory use therefore does not grow linearly with file size.

---

## 8. Download algorithm

The tracker supplies the piece hash list and peer list.

An atomic counter is shared by up to four workers:

```text
index = nextPiece.fetch_add(1)
```

Each worker then:

1. chooses an advertised peer;
2. sends `GETPIECE`;
3. receives one piece;
4. calculates SHA1;
5. rejects corrupted data;
6. retries failed/corrupt requests;
7. writes verified data using `pwrite()`.

The atomic index prevents two workers in the same download from intentionally processing the same piece.

At completion:

```text
SHA1(destination) == trackerWholeFileSHA1
```

must hold before the download is reported successful.

---

## 9. Multi-peer behavior

Suppose a file has seeders:

```text
127.0.0.1:7001
127.0.0.1:7002
127.0.0.1:7003
```

Different workers can request different pieces from different peers. If one peer becomes unavailable, the worker rotates through the remaining peers.

This allows the same large file to be downloaded concurrently from multiple clients.

A successful downloader is also added to the tracker as a seeder, enabling peer-to-peer propagation.

---

## 10. Tracker synchronization protocol

State-changing tracker operations generate records such as:

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

The receiving tracker calls `applyRecord()` directly. It does not call `syncRecord()` again, so synchronization does not loop between the two trackers.

When a tracker reconnects after a failure, it sends:

```text
SNAPSHOT_REQUEST
```

and receives:

```text
SNAP_BEGIN
...
SNAP_END
```

The snapshot contains users, groups, memberships, requests and file metadata.

---

## 11. Failover algorithm

The client stores both tracker ports.

For each tracker request:

```text
current tracker
        |
        | connection fails
        v
other tracker
```

The successful tracker becomes the current tracker for subsequent requests.

This permits continued operation when one tracker is offline.

---

## 12. Failure handling

### Tracker failure

Client attempts the other tracker.

### Peer failure

The current piece is retried using another peer.

### Corrupt piece

SHA1 mismatch causes the bytes to be discarded and the piece to be requested again.

### All peers unavailable

The retry limit is reached and the download reports failure rather than hanging forever.

### Invalid destination

The output file cannot be opened or resized, so the download reports failure.

### Invalid command

The command loop prints a usage/error message instead of accessing missing arguments.

---

## 13. Concurrency and synchronization

The tracker handles client sockets in separate threads. A mutex protects the shared metadata maps.

The client has:

- a peer accept thread;
- one worker thread per incoming peer piece request;
- one background download thread per active file;
- up to four piece workers inside each download.

Mutexes protect shared local maps, and atomics protect download counters and piece allocation.

---

## 14. Network protocol

The control protocol uses newline-delimited messages with tab-separated fields.

Example:

```text
LOGIN\tu1\tpass1\t127.0.0.1:7001
```

File piece transfer uses:

```text
GETPIECE\tg1\tfile.bin\t5
```

Response header:

```text
PIECE\t524288
```

followed immediately by the raw piece bytes.

`sendAll()` and `recvLine()` explicitly account for TCP partial transmission.

Socket receive/send timeouts are configured so an unresponsive connection cannot block a worker forever.

This protocol intentionally does not implement an unrelated magic-number/checksum frame header; integrity of file data is provided by the per-piece and complete-file SHA1 hashes.

---

## 15. Resource management

Files are opened only for the operation that needs them and closed on success and error paths.

Peer sockets and tracker sockets are closed after each request.

Download output is pre-sized with `ftruncate()` and pieces are written at their correct offsets.

The peer server reads only the requested piece rather than loading an entire shared file.

---

## 16. Security/input handling decisions

The tracker rejects empty fields and protocol-breaking tabs/newlines in user/password/file fields.

User/password fields have a practical 4096-character limit. This prevents accidental multi-megabyte metadata allocation while still allowing normal credentials.

Passwords are never echoed in tracker status output.

The client does not pass command strings to a shell, so shell command injection through file paths is not used.

---


## 19. Conclusion

The final implementation provides a readable two-tracker P2P architecture with direct client-to-client transfer, piece-level SHA1 validation, concurrent downloads, tracker failover, metadata synchronization, session control, owner transfer, automatic seeding and error handling.
