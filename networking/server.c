// server.c - SHAM protocol server
// usage: ./server <port> [--chat] [loss_rate]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <openssl/md5.h>
#include <signal.h>

#include "sham.h"

static int sockfd;
static struct sockaddr_in cli_addr;
static socklen_t cli_len = sizeof(struct sockaddr_in);
static double loss_rate = 0.0;
static int chat_mode = 0;

static uint32_t my_seq = 0;
static uint32_t their_seq = 0;
static uint16_t my_window = RECV_BUF_SIZE;

static void send_pkt(sham_pkt_t *p)
{
    char buf[2048];
    int len = pkt_pack(p, buf, sizeof(buf));
    if (len > 0)
        sendto(sockfd, buf, (size_t)len, 0,
               (struct sockaddr *)&cli_addr, cli_len);
}

static void send_ack(uint32_t ack)
{
    sham_pkt_t p;
    memset(&p, 0, sizeof(p));
    p.header.seq_num = my_seq;
    p.header.ack_num = ack;
    p.header.flags = FLAG_ACK;
    p.header.window_size = my_window;
    p.data_len = 0;
    send_pkt(&p);
    sham_log("SND ACK=%u WIN=%u", ack, my_window);
}

// 3-way handshake from server perspective
static int handshake(void)
{
    char buf[2048];
    sham_pkt_t pkt;

    // wait for SYN
    while (1) {
        int n = recvfrom(sockfd, buf, sizeof(buf), 0,
                         (struct sockaddr *)&cli_addr, &cli_len);
        if (n <= 0) continue;

        if (pkt_unpack(buf, n, &pkt) == 0 && (pkt.header.flags & FLAG_SYN)) {
            their_seq = pkt.header.seq_num;
            sham_log("RCV SYN SEQ=%u", their_seq);

            // send SYN-ACK
            my_seq = (uint32_t)(rand() % 10000);
            sham_pkt_t sa;
            memset(&sa, 0, sizeof(sa));
            sa.header.seq_num = my_seq;
            sa.header.ack_num = their_seq + 1;
            sa.header.flags = FLAG_SYN | FLAG_ACK;
            sa.header.window_size = my_window;
            sa.data_len = 0;
            send_pkt(&sa);
            sham_log("SND SYN-ACK SEQ=%u ACK=%u", my_seq, their_seq + 1);

            // wait for ACK
            struct timeval tv = {5, 0};
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
            if (n > 0 && pkt_unpack(buf, n, &pkt) == 0 &&
                (pkt.header.flags & FLAG_ACK)) {
                sham_log("RCV ACK FOR SYN");
                their_seq++;
                my_seq++;
                tv.tv_sec = 0;
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                return 0;
            }
        }
    }
}

// handle 4-way FIN (we received FIN from client)
static void do_fin_passive(void)
{
    char buf[2048];
    sham_pkt_t pkt;

    send_ack(their_seq + 1);
    sham_log("SND ACK FOR FIN");

    // send our own FIN
    sham_pkt_t fin;
    memset(&fin, 0, sizeof(fin));
    fin.header.seq_num = my_seq;
    fin.header.flags = FLAG_FIN;
    fin.data_len = 0;
    send_pkt(&fin);
    sham_log("SND FIN SEQ=%u", my_seq);

    // wait for their final ACK
    int n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
    if (n > 0 && pkt_unpack(buf, n, &pkt) == 0 && (pkt.header.flags & FLAG_ACK))
        sham_log("RCV ACK=%u", pkt.header.ack_num);
}

// active FIN (we initiate closing)
static void do_fin_active(void)
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

    int n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
    if (n > 0 && pkt_unpack(buf, n, &pkt) == 0 && (pkt.header.flags & FLAG_ACK))
        sham_log("RCV ACK=%u", pkt.header.ack_num);

    n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
    if (n > 0 && pkt_unpack(buf, n, &pkt) == 0 && (pkt.header.flags & FLAG_FIN)) {
        sham_log("RCV FIN SEQ=%u", pkt.header.seq_num);
        send_ack(pkt.header.seq_num + 1);
    }
}

// receive file from client
static void recv_file(void)
{
    FILE *fp = fopen("received_file", "wb");
    if (!fp) { perror("fopen"); return; }

    recv_entry_t rbuf[WINDOW_SIZE * 4];
    memset(rbuf, 0, sizeof(rbuf));
    int rbuf_cnt = 0;
    uint32_t expect = their_seq;

    char buf[2048];
    sham_pkt_t pkt;
    MD5_CTX md5;
    MD5_Init(&md5);

    while (1) {
        int n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
        if (n <= 0) continue;
        if (pkt_unpack(buf, n, &pkt) != 0) continue;

        if (pkt.header.flags & FLAG_FIN) {
            sham_log("RCV FIN SEQ=%u", pkt.header.seq_num);
            their_seq = pkt.header.seq_num;
            do_fin_passive();
            break;
        }

        // simulate drop
        if (drop_packet(loss_rate)) {
            sham_log("DROP DATA SEQ=%u", pkt.header.seq_num);
            continue;
        }

        sham_log("RCV DATA SEQ=%u LEN=%u", pkt.header.seq_num, pkt.data_len);

        if (pkt.header.seq_num == expect) {
            // in order - write it
            fwrite(pkt.data, 1, pkt.data_len, fp);
            MD5_Update(&md5, pkt.data, pkt.data_len);
            expect += pkt.data_len;

            // flush any buffered packets that are now in order
            int found;
            do {
                found = 0;
                for (int i = 0; i < rbuf_cnt; i++) {
                    if (rbuf[i].valid && rbuf[i].seq == expect) {
                        fwrite(rbuf[i].data, 1, rbuf[i].len, fp);
                        MD5_Update(&md5, rbuf[i].data, rbuf[i].len);
                        expect += rbuf[i].len;
                        rbuf[i].valid = 0;
                        found = 1;
                    }
                }
            } while (found);

            my_window = RECV_BUF_SIZE;
        } else if (pkt.header.seq_num > expect) {
            // out of order - buffer it
            for (int i = 0; i < WINDOW_SIZE * 4; i++) {
                if (!rbuf[i].valid) {
                    rbuf[i].seq = pkt.header.seq_num;
                    rbuf[i].len = pkt.data_len;
                    memcpy(rbuf[i].data, pkt.data, pkt.data_len);
                    rbuf[i].valid = 1;
                    if (i >= rbuf_cnt) rbuf_cnt = i + 1;
                    break;
                }
            }
        }

        send_ack(expect);
    }

    fclose(fp);

    // print MD5
    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5_Final(hash, &md5);
    printf("MD5: ");
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) printf("%02x", hash[i]);
    printf("\n");
}

// chat mode using select()
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
                    their_seq = pkt.header.seq_num;
                    do_fin_passive();
                    printf("Remote disconnected.\n");
                    break;
                }
                if (pkt.data_len > 0) {
                    sham_log("RCV DATA SEQ=%u LEN=%u", pkt.header.seq_num, pkt.data_len);
                    pkt.data[pkt.data_len] = '\0';
                    printf("Remote: %s", pkt.data);
                    fflush(stdout);
                    their_seq = pkt.header.seq_num + pkt.data_len;
                    send_ack(their_seq);
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            if (!fgets(input, sizeof(input), stdin)) break;
            if (strncmp(input, "/quit", 5) == 0) { do_fin_active(); break; }

            sham_pkt_t msg;
            memset(&msg, 0, sizeof(msg));
            msg.header.seq_num = my_seq;
            msg.header.ack_num = their_seq;
            msg.header.flags = FLAG_ACK;
            msg.header.window_size = my_window;
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
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port> [--chat] [loss_rate]\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--chat") == 0) chat_mode = 1;
        else loss_rate = atof(argv[i]);
    }

    srand((unsigned)time(NULL));
    log_init("server_log.txt");
    signal(SIGPIPE, SIG_IGN);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    printf("Server listening on port %d...\n", port);

    if (handshake() != 0) {
        fprintf(stderr, "Handshake failed\n");
        close(sockfd); return 1;
    }
    printf("Connection established.\n");

    if (chat_mode) do_chat();
    else           recv_file();

    log_close();
    close(sockfd);
    return 0;
}
