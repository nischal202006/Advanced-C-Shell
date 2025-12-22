// client.c - SHAM protocol client
// usage: ./client <ip> <port> <in_file> <out_name> [loss_rate]
//        ./client <ip> <port> --chat [loss_rate]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>

#include "sham.h"

static int sockfd;
static struct sockaddr_in srv_addr;
static socklen_t srv_len = sizeof(struct sockaddr_in);
static double loss_rate = 0.0;
static int chat_mode = 0;

static uint32_t my_seq = 0;
static uint32_t their_seq = 0;
static uint16_t their_window = RECV_BUF_SIZE;

static void send_pkt(sham_pkt_t *p)
{
    char buf[2048];
    int len = pkt_pack(p, buf, sizeof(buf));
    if (len > 0)
        sendto(sockfd, buf, (size_t)len, 0,
               (struct sockaddr *)&srv_addr, srv_len);
}

// client initiates 3-way handshake
static int handshake(void)
{
    my_seq = (uint32_t)(rand() % 10000);

    sham_pkt_t syn;
    memset(&syn, 0, sizeof(syn));
    syn.header.seq_num = my_seq;
    syn.header.flags = FLAG_SYN;
    syn.header.window_size = RECV_BUF_SIZE;
    syn.data_len = 0;
    send_pkt(&syn);
    sham_log("SND SYN SEQ=%u", my_seq);

    // wait for SYN-ACK
    char buf[2048];
    sham_pkt_t pkt;
    struct timeval tv = {5, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
    if (n <= 0) { fprintf(stderr, "Handshake timeout\n"); return -1; }
    if (pkt_unpack(buf, n, &pkt) != 0) return -1;

    if ((pkt.header.flags & (FLAG_SYN | FLAG_ACK)) != (FLAG_SYN | FLAG_ACK))
        return -1;

    their_seq = pkt.header.seq_num;
    their_window = pkt.header.window_size;
    sham_log("RCV SYN-ACK SEQ=%u ACK=%u", their_seq, pkt.header.ack_num);

    my_seq++;
    their_seq++;

    // send ACK to complete handshake
    sham_pkt_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.header.seq_num = my_seq;
    ack.header.ack_num = their_seq;
    ack.header.flags = FLAG_ACK;
    ack.header.window_size = RECV_BUF_SIZE;
    ack.data_len = 0;
    send_pkt(&ack);
    sham_log("SND ACK FOR SYN");

    // clear timeout
    tv.tv_sec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return 0;
}

// 4-way FIN from client side
static void do_fin(void)
{
    char buf[2048];
    sham_pkt_t pkt;

    sham_pkt_t fin;
    memset(&fin, 0, sizeof(fin));
    fin.header.seq_num = my_seq;
    fin.header.flags = FLAG_FIN;
    fin.data_len = 0;
    send_pkt(&fin);
    sham_log("SND FIN SEQ=%u", my_seq);

    struct timeval tv = {5, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // wait ACK
    int n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
    if (n > 0 && pkt_unpack(buf, n, &pkt) == 0 && (pkt.header.flags & FLAG_ACK))
        sham_log("RCV ACK FOR FIN");

    // wait FIN from server
    n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
    if (n > 0 && pkt_unpack(buf, n, &pkt) == 0 && (pkt.header.flags & FLAG_FIN)) {
        sham_log("RCV FIN SEQ=%u", pkt.header.seq_num);
        // send final ACK
        sham_pkt_t a;
        memset(&a, 0, sizeof(a));
        a.header.seq_num = my_seq;
        a.header.ack_num = pkt.header.seq_num + 1;
        a.header.flags = FLAG_ACK;
        a.data_len = 0;
        send_pkt(&a);
        sham_log("SND ACK=%u", pkt.header.seq_num + 1);
    }

    tv.tv_sec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// send file with sliding window, retransmission, flow control
static void send_file(const char *infile)
{
    FILE *fp = fopen(infile, "rb");
    if (!fp) { perror("fopen"); return; }

    send_entry_t win[WINDOW_SIZE];
    memset(win, 0, sizeof(win));

    uint32_t base = my_seq;
    uint32_t next = my_seq;
    int eof_reached = 0;
    int win_used = 0;

    while (1) {
        // fill the window with new data
        while (!eof_reached && win_used < WINDOW_SIZE) {
            uint32_t inflight = next - base;
            if (inflight >= their_window) break; // flow control

            char data[DATA_SIZE];
            size_t nr = fread(data, 1, DATA_SIZE, fp);
            if (nr == 0) { eof_reached = 1; break; }

            sham_pkt_t p;
            memset(&p, 0, sizeof(p));
            p.header.seq_num = next;
            p.header.ack_num = their_seq;
            p.header.flags = FLAG_ACK;
            p.header.window_size = RECV_BUF_SIZE;
            p.data_len = (uint16_t)nr;
            memcpy(p.data, data, nr);
            send_pkt(&p);
            sham_log("SND DATA SEQ=%u LEN=%zu", next, nr);

            // find free slot
            for (int i = 0; i < WINDOW_SIZE; i++) {
                if (win[i].acked || win[i].len == 0) {
                    win[i].seq = next;
                    win[i].len = (uint16_t)nr;
                    memcpy(win[i].data, data, nr);
                    gettimeofday(&win[i].sent_at, NULL);
                    win[i].acked = 0;
                    break;
                }
            }

            next += (uint32_t)nr;
            win_used++;
        }

        // done?
        if (eof_reached && base == next) break;

        // wait for ack or timeout
        struct timeval tv = {0, RTO_MS * 1000};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);

        int ready = select(sockfd + 1, &fds, NULL, NULL, &tv);
        if (ready > 0) {
            char buf[2048];
            sham_pkt_t ack;
            int n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);

            if (n > 0 && pkt_unpack(buf, n, &ack) == 0 && (ack.header.flags & FLAG_ACK)) {
                uint32_t a = ack.header.ack_num;
                sham_log("RCV ACK=%u", a);

                // update flow control
                if (ack.header.window_size != their_window) {
                    their_window = ack.header.window_size;
                    sham_log("FLOW WIN UPDATE=%u", their_window);
                }

                // mark packets as acked
                if (a > base) {
                    for (int i = 0; i < WINDOW_SIZE; i++) {
                        if (!win[i].acked && win[i].len > 0 &&
                            win[i].seq + win[i].len <= a) {
                            win[i].acked = 1;
                            win_used--;
                        }
                    }
                    base = a;
                    my_seq = a;
                }
            }
        }

        // check for retransmission timeouts
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (!win[i].acked && win[i].len > 0 && ms_since(&win[i].sent_at) >= RTO_MS) {
                sham_log("TIMEOUT SEQ=%u", win[i].seq);

                sham_pkt_t p;
                memset(&p, 0, sizeof(p));
                p.header.seq_num = win[i].seq;
                p.header.ack_num = their_seq;
                p.header.flags = FLAG_ACK;
                p.header.window_size = RECV_BUF_SIZE;
                p.data_len = win[i].len;
                memcpy(p.data, win[i].data, win[i].len);
                send_pkt(&p);
                sham_log("RETX DATA SEQ=%u LEN=%u", win[i].seq, win[i].len);

                gettimeofday(&win[i].sent_at, NULL);
            }
        }
    }

    fclose(fp);
    my_seq = next;
    do_fin();
}

// chat mode
static void do_chat(void)
{
    char buf[2048], input[DATA_SIZE];
    sham_pkt_t pkt;
    printf("Chat mode active. Type /quit to exit.\n");

    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(sockfd, &fds);
        int mx = (sockfd > STDIN_FILENO) ? sockfd : STDIN_FILENO;

        if (select(mx + 1, &fds, NULL, NULL, NULL) <= 0) continue;

        if (FD_ISSET(sockfd, &fds)) {
            int n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
            if (n > 0 && pkt_unpack(buf, n, &pkt) == 0) {
                if (pkt.header.flags & FLAG_FIN) {
                    sham_log("RCV FIN SEQ=%u", pkt.header.seq_num);
                    // respond with ACK + our FIN
                    sham_pkt_t a;
                    memset(&a, 0, sizeof(a));
                    a.header.seq_num = my_seq;
                    a.header.ack_num = pkt.header.seq_num + 1;
                    a.header.flags = FLAG_ACK;
                    a.data_len = 0;
                    send_pkt(&a);
                    sham_log("SND ACK FOR FIN");

                    sham_pkt_t f;
                    memset(&f, 0, sizeof(f));
                    f.header.seq_num = my_seq;
                    f.header.flags = FLAG_FIN;
                    f.data_len = 0;
                    send_pkt(&f);
                    sham_log("SND FIN SEQ=%u", my_seq);

                    // try to get the last ACK
                    struct timeval t = {2, 0};
                    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
                    recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
                    printf("Remote disconnected.\n");
                    break;
                }
                if (pkt.data_len > 0) {
                    sham_log("RCV DATA SEQ=%u LEN=%u", pkt.header.seq_num, pkt.data_len);
                    pkt.data[pkt.data_len] = '\0';
                    printf("Remote: %s", pkt.data);
                    fflush(stdout);
                    their_seq = pkt.header.seq_num + pkt.data_len;

                    sham_pkt_t a;
                    memset(&a, 0, sizeof(a));
                    a.header.seq_num = my_seq;
                    a.header.ack_num = their_seq;
                    a.header.flags = FLAG_ACK;
                    a.header.window_size = RECV_BUF_SIZE;
                    a.data_len = 0;
                    send_pkt(&a);
                    sham_log("SND ACK=%u WIN=%u", their_seq, (unsigned)RECV_BUF_SIZE);
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            if (!fgets(input, sizeof(input), stdin)) break;
            if (strncmp(input, "/quit", 5) == 0) { do_fin(); break; }

            sham_pkt_t msg;
            memset(&msg, 0, sizeof(msg));
            msg.header.seq_num = my_seq;
            msg.header.ack_num = their_seq;
            msg.header.flags = FLAG_ACK;
            msg.header.window_size = RECV_BUF_SIZE;
            msg.data_len = (uint16_t)strlen(input);
            memcpy(msg.data, input, msg.data_len);
            send_pkt(&msg);
            sham_log("SND DATA SEQ=%u LEN=%u", my_seq, msg.data_len);
            my_seq += msg.data_len;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <ip> <port> <file> <outname> [loss]\n", argv[0]);
        fprintf(stderr, "       %s <ip> <port> --chat [loss]\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char *infile = NULL;

    // parse args
    if (argc >= 4) {
        if (strcmp(argv[3], "--chat") == 0) {
            chat_mode = 1;
            if (argc >= 5) loss_rate = atof(argv[4]);
        } else {
            infile = argv[3];
            // argv[4] is output name (used by server)
            if (argc >= 6) loss_rate = atof(argv[5]);
        }
    }

    srand((unsigned)time(NULL) ^ getpid());
    log_init("client_log.txt");
    signal(SIGPIPE, SIG_IGN);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &srv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Bad address: %s\n", ip);
        close(sockfd); return 1;
    }

    printf("Connecting to %s:%d...\n", ip, port);
    if (handshake() != 0) {
        fprintf(stderr, "Handshake failed\n");
        close(sockfd); return 1;
    }
    printf("Connection established.\n");

    if (chat_mode) do_chat();
    else {
        if (!infile) { fprintf(stderr, "No input file\n"); close(sockfd); return 1; }
        send_file(infile);
    }

    log_close();
    close(sockfd);
    return 0;
}
