# Custom Shell & Reliable UDP Protocol

A systems programming project built from scratch in C — covers shell implementation and network protocol design.
## Author 
Name:G.Nischal Rollno:2024102070
## What's in here

**Shell** — A working interactive shell (like a simpler bash) with piping, redirection, background processes, job control, and signal handling.

**Networking** — A reliable transport protocol (SHAM) on top of UDP. Basically reimplementing the important parts of TCP: handshakes, sliding window, retransmission, flow control, etc.

Both are written in C99 for Linux/WSL. No external libraries except OpenSSL for MD5 checksums in the networking part.

---

## Shell (`shell/`)

An interactive POSIX shell that handles most of what you'd expect from a real shell:

- Prompt shows `<user@host:~/path>` with home dir substitution
- Built-in commands: `hop` (cd), `reveal` (ls), `log` (history), `activities`, `ping`, `fg`, `bg`
- I/O redirection with `<`, `>`, `>>`
- Multi-stage piping (`cmd1 | cmd2 | cmd3`)
- Background processes with `&`, sequential execution with `;`
- Ctrl-C forwards to foreground process, Ctrl-Z stops it, Ctrl-D exits
- Persistent command history saved to `.shell_log`
- Background job tracking with zombie reaping

The parser uses a recursive descent approach based on a formal CFG. The executor handles fork/exec with proper process groups so job control actually works.

```
shell/
├── include/          # headers for each module
│   ├── shell.h       # global state struct
│   ├── parser.h      # command grammar structs
│   ├── executor.h    # execution interface
│   ├── builtins.h    # built-in commands
│   ├── signals.h     # signal setup
│   ├── jobs.h        # job tracking
│   └── prompt.h      # prompt display
├── src/              # implementation
│   ├── main.c        # REPL loop
│   ├── parser.c      # tokenizer + parser
│   ├── executor.c    # fork/exec, pipes, redirection
│   ├── builtins.c    # hop, reveal, log, fg, bg, etc.
│   ├── signals.c     # SIGINT, SIGTSTP, SIGCHLD
│   ├── jobs.c        # background job management
│   └── prompt.c      # prompt rendering
└── Makefile
```

### Build and run

```bash
cd shell
make clean && make all
./shell.out
```

### Quick demo

```
<nischal@laptop:~> echo hello > test.txt
<nischal@laptop:~> cat test.txt | wc -w
1
<nischal@laptop:~> sleep 30 &
[1] 12345
<nischal@laptop:~> activities
[12345] : sleep 30 - Running
<nischal@laptop:~> hop /tmp
<nischal@laptop:/tmp> hop -
<nischal@laptop:~> log
echo hello > test.txt
cat test.txt | wc -w
sleep 30
```

---

## Networking (`networking/`)

SHAM is a reliable protocol built on UDP sockets. It handles everything that makes TCP reliable, implemented by hand.

What it does:
- **Connection setup** — 3-way handshake (SYN, SYN-ACK, ACK)
- **Connection teardown** — 4-way FIN handshake
- **Reliable transfer** — Sliding window with cumulative ACKs
- **Retransmission** — Timeout-based resending of lost packets
- **Flow control** — Receiver advertises available buffer space
- **File transfer** — Sends files reliably, server prints MD5 hash to verify
- **Chat mode** — Bidirectional messaging with `select()` for I/O multiplexing
- **Packet loss testing** — Pass a drop rate to simulate unreliable networks
- **Logging** — Set `RUDP_LOG=1` for timestamped packet-level logs

```
networking/
├── sham.h      # packet format, serialization helpers, logging
├── server.c    # receives files / runs chat mode
├── client.c    # sends files / runs chat mode
└── Makefile
```

### Build and run

```bash
cd networking
sudo apt install libssl-dev   # needed for MD5
make

# send a file
./server 8080 &
./client 127.0.0.1 8080 myfile.txt output.txt

# chat mode
./server 9090 --chat &
./client 127.0.0.1 9090 --chat

# test with 10% packet loss
./server 8080 0.1 &
./client 127.0.0.1 8080 myfile.txt output.txt 0.1
```

---

## Requirements

```bash
sudo apt install build-essential libssl-dev
```

- Linux or WSL
- GCC with C99 support
- libssl-dev (for networking MD5)

## What I learned building this

- How shells actually work under the hood — process groups, terminal control, signal forwarding
- The difference between `fork+exec` and just `fork`
- Why TCP needs all those mechanisms (sequence numbers, ACKs, retransmission) — building them from scratch makes it click
- How `select()` enables non-blocking I/O multiplexing
- Parsing with recursive descent based on a formal grammar
- Managing zombie processes and background jobs properly
