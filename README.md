<div align="center">
  <h1>Advanced Custom C-Shell & Reliable UDP Protocol (SHAM)</h1>
  <p><strong>A Systems Programming Capstone Project Built From Scratch in C</strong></p>
</div>

## Overview

This repository hosts a comprehensive systems programming project featuring two major components: a fully functional **Interactive POSIX Shell** and a custom **Reliable Transport Protocol (SHAM)** built over UDP. 

Developed entirely in **C99** for Linux/WSL without relying on heavy external libraries (except OpenSSL for MD5 checksums), this project demonstrates deep, low-level understanding of operating systems, process management, formal grammar parsing, socket programming, and network protocols.

---

## Key Features

### The Custom Shell (`shell/`)
An interactive, robust command-line interpreter featuring advanced process management and I/O redirection.

*   **Process & Job Control**: Full background job tracking with zombie reaping. Implements process groups, foregrounding (`fg`), backgrounding (`bg`), and native signal forwarding (`Ctrl-C`, `Ctrl-Z`, `Ctrl-D`).
*   **Recursive Descent Parser**: Commands are parsed utilizing a custom recursive descent approach based on a formal Context-Free Grammar (CFG).
*   **Advanced Execution Environment**: Supports multi-stage piping (`cmd1 | cmd2 | cmd3`), background processes (`&`), sequential execution (`cmd1 ; cmd2`), and extensive I/O redirection (`<`, `>`, `>>`).
*   **Custom Built-ins**: Features built-in commands natively handled by the shell, including `hop` (cd), `reveal` (ls), `log` (history), and `activities` (job tracking).

### The Reliable UDP Protocol (`networking/`)
SHAM is a custom transport layer protocol built directly over UDP sockets, essentially re-implementing the core reliability mechanisms of TCP by hand.

*   **Connection State Management**: Implements a 3-way handshake (SYN, SYN-ACK, ACK) for connection setup and a 4-way FIN handshake for graceful teardown.
*   **Reliable Data Transfer**: Utilizes a sliding window mechanism with cumulative ACKs and timeout-based retransmission of lost packets.
*   **Flow Control**: Receiver constantly advertises available buffer space to prevent network congestion.
*   **Loss Simulation & Chat**: Supports simulated packet drops for robustness testing, alongside a bidirectional `select()` based multiplexed chat mode.

---

## Development Timeline

This project was built iteratively over a two-week period in December 2025. Below is the exact progression of the development from the commit history:

| Date | Commit Detail |
| :--- | :--- |
| **Dec 10, 2025** | Initialized project structure and architecture setup. |
| **Dec 11, 2025** | Designed the core shell REPL and main execution loop. |
| **Dec 12, 2025** | Implemented custom prompting and OS-level signal handling. |
| **Dec 14, 2025** | Built the recursive descent tokenizer and parser for shell commands. |
| **Dec 15, 2025** | Implemented the executor module for `fork`/`exec`, pipes, and redirection. |
| **Dec 17, 2025** | Engineered background process and job state management. |
| **Dec 18, 2025** | Finalized shell built-in commands and Makefile pipeline. |
| **Dec 19, 2025** | Initialized reliable UDP protocol structures and headers. |
| **Dec 21, 2025** | Developed the UDP server-side handshake, flow control, and sliding logic. |
| **Dec 22, 2025** | Implemented the UDP client-side serialization, dispatch, and timeout mechanisms. |
| **Dec 23, 2025** | Integrated networking Makefile and finalized the reliable transport layer. |
| **Dec 24, 2025** | Comprehensive documentation and README polish. |

---

## Build and Usage Instructions

### Prerequisites
*   Linux or WSL environment
*   GCC (C99 support)
*   `sudo apt install build-essential libssl-dev`

### Running the Shell
```bash
cd shell
make clean && make all
./shell.out
```

### Running the Reliable Protocol
```bash
cd networking
make

# Reliable File Transfer
./server 8080 &
./client 127.0.0.1 8080 myfile.txt output.txt

# Chat Mode (Bidirectional Multiplexing)
./server 9090 --chat &
./client 127.0.0.1 9090 --chat
```

---

## Technical Learnings
*   **Operating Systems**: Deepened understanding of process groups, terminal control structures, and signal forwarding natively at the kernel level.
*   **Networking**: Internalized the necessity and complexity of TCP mechanisms (sequence numbers, ACKs, retransmission) by building them entirely from scratch over stateless UDP.
*   **Asynchronous I/O**: Mastered `select()` to enable non-blocking I/O multiplexing across multiple file descriptors.
*   **Parsers**: Gained practical experience implementing a recursive descent parser based on formal grammar rules.

<div align="center">
  <i>Developed by G. Nischal</i>
</div>
