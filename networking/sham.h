// sham.h - shared header for the SHAM protocol (reliable UDP)

#ifndef SHAM_H
#define SHAM_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

#define DATA_SIZE         1024
#define WINDOW_SIZE       10    // max unacked packets in flight
#define RTO_MS            500   // retransmission timeout
#define RECV_BUF_SIZE     (1024 * 64)

// flags
#define FLAG_SYN    0x0001
#define FLAG_ACK    0x0002
#define FLAG_FIN    0x0004

// the packet header that goes on the wire
typedef struct {
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t flags;
    uint16_t window_size;
} __attribute__((packed)) sham_hdr_t;

// in-memory packet (header + payload)
typedef struct {
    sham_hdr_t header;
    char       data[DATA_SIZE];
    uint16_t   data_len;
} sham_pkt_t;

// for buffering out of order packets on receiver side
typedef struct {
    uint32_t seq;
    char     data[DATA_SIZE];
    uint16_t len;
    int      valid;
} recv_entry_t;

// sender's sliding window slot
typedef struct {
    uint32_t       seq;
    char           data[DATA_SIZE];
    uint16_t       len;
    struct timeval  sent_at;
    int            acked;
} send_entry_t;

// --- logging stuff ---
// controlled by env RUDP_LOG=1

static FILE *log_fp = NULL;
static int logging_on = 0;

static inline void log_init(const char *fname)
{
    const char *env = getenv("RUDP_LOG");
    if (env && strcmp(env, "1") == 0) {
        log_fp = fopen(fname, "w");
        if (log_fp) logging_on = 1;
    }
}

static inline void log_close(void)
{
    if (log_fp) { fclose(log_fp); log_fp = NULL; logging_on = 0; }
}

// log with timestamp (microsecond precision)
static inline void sham_log(const char *fmt, ...)
{
    if (!logging_on || !log_fp) return;

    char tbuf[30];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t cur = tv.tv_sec;
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&cur));

    fprintf(log_fp, "[%s.%06ld] [LOG] ", tbuf, (long)tv.tv_usec);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_fp, fmt, ap);
    va_end(ap);

    fprintf(log_fp, "\n");
    fflush(log_fp);
}

// --- helpers ---

// pack a sham_pkt into a byte buffer for sendto()
static inline int pkt_pack(const sham_pkt_t *p, char *buf, size_t bufsz)
{
    size_t total = sizeof(sham_hdr_t) + sizeof(uint16_t) + p->data_len;
    if (total > bufsz) return -1;

    memcpy(buf, &p->header, sizeof(sham_hdr_t));
    uint16_t nl = htons(p->data_len);
    memcpy(buf + sizeof(sham_hdr_t), &nl, sizeof(uint16_t));
    if (p->data_len > 0)
        memcpy(buf + sizeof(sham_hdr_t) + sizeof(uint16_t), p->data, p->data_len);
    return (int)total;
}

// unpack bytes from recvfrom() into sham_pkt
static inline int pkt_unpack(const char *buf, int n, sham_pkt_t *p)
{
    if (n < (int)(sizeof(sham_hdr_t) + sizeof(uint16_t))) return -1;

    memcpy(&p->header, buf, sizeof(sham_hdr_t));
    uint16_t nl;
    memcpy(&nl, buf + sizeof(sham_hdr_t), sizeof(uint16_t));
    p->data_len = ntohs(nl);

    if (p->data_len > DATA_SIZE) return -1;
    if (n < (int)(sizeof(sham_hdr_t) + sizeof(uint16_t) + p->data_len)) return -1;

    if (p->data_len > 0)
        memcpy(p->data, buf + sizeof(sham_hdr_t) + sizeof(uint16_t), p->data_len);
    return 0;
}

static inline long ms_since(struct timeval *t)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - t->tv_sec) * 1000L + (now.tv_usec - t->tv_usec) / 1000L;
}

static inline int drop_packet(double rate)
{
    if (rate <= 0.0) return 0;
    return ((double)rand() / RAND_MAX) < rate;
}

#endif
