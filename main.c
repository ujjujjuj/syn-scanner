#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

in_addr_t local_ip;
in_port_t local_port = 42424;
struct sockaddr_in dst_addr;

int packets_sent = 0;
int include_closed = 0;

struct ptcphdr {
    in_addr_t src_ip;
    in_addr_t dst_ip;
    unsigned char dummy;
    unsigned char proto;
    unsigned short seg_len;
};

struct tcp_checksum_data {
    struct ptcphdr ptcph;
    struct tcphdr tcph;
    unsigned char *data;
};

void set_local_ip() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("1.1.1.1");
    server.sin_port = htons(53);

    if (connect(sock, (const struct sockaddr *)&server, sizeof(server))) {
        perror("Cannot connect to the internet");
        exit(1);
    }
    struct sockaddr_in local_name;
    socklen_t name_len = sizeof(local_name);
    if (getsockname(sock, (struct sockaddr *)&local_name, &name_len)) {
        perror("Cannot get socket name");
        exit(1);
    }

    local_ip = local_name.sin_addr.s_addr;
}
void *recv_data(void *vargp) {
    int raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (raw_sock < 0) {
        perror("Cannot create raw socket");
        exit(1);
    }
    unsigned char buf[sizeof(struct iphdr) + sizeof(struct tcphdr)];

    struct iphdr *iph = (struct iphdr *)buf;
    struct tcphdr *tcph = (struct tcphdr *)(buf + sizeof(struct iphdr));
    while (!packets_sent) {
    recv:
        if (recvfrom(raw_sock, buf, sizeof(buf), 0, NULL, NULL) < 0) {
            perror("Failed to recv packet");
            exit(1);
        }
        if (iph->saddr == dst_addr.sin_addr.s_addr && iph->protocol == IPPROTO_TCP) {
            if (tcph->syn == 1 && tcph->ack == 1) {
                printf("Port %d is open\n", htons(tcph->source));
            } else if (tcph->rst == 1 && tcph->ack == 1 && include_closed) {
                printf("Port %d is closed\n", htons(tcph->source));
            }
        }
    }

    close(raw_sock);

    return NULL;
}

unsigned short tcp_checksum(const struct tcp_checksum_data *checksum_dat) {
    unsigned long sum = 0;
    unsigned short *buf = (unsigned short *)checksum_dat;
    for (int i = 0; i < 16; i++) {
        sum += *buf;
        buf++;
    }
    // implementation for checksumming the data not required for this project
    sum = (sum >> 16) + (sum & 0xffff);

    return (unsigned short)~sum;
}

void send_data() {
    int raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (raw_sock < 0) {
        perror("Cannot create raw socket");
        exit(1);
    }
    int one = 1;
    if (setsockopt(raw_sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) != 0) {
        perror("Error setting IPH_HDRINCL sockopt");
        exit(1);
    }

    unsigned char buf[sizeof(struct iphdr) + sizeof(struct tcphdr)];
    struct iphdr *iph = (struct iphdr *)buf;
    struct tcphdr *tcph = (struct tcphdr *)(buf + sizeof(struct iphdr));

    iph->daddr = dst_addr.sin_addr.s_addr;
    iph->saddr = local_ip;
    iph->protocol = IPPROTO_TCP;
    iph->version = 4;
    iph->tot_len = sizeof(struct iphdr) + sizeof(struct tcphdr);
    iph->ihl = 5;
    iph->tos = 0;
    iph->id = htons(rand() % 65536);
    iph->frag_off = htons(16384);
    iph->ttl = 64;
    iph->check = 0;

    memset(tcph, 0, sizeof(struct tcphdr));
    tcph->source = htons(local_port);
    tcph->dest = 0;
    tcph->seq = htonl(0);
    tcph->doff = 5;
    tcph->syn = 1;
    tcph->window = htons(14600);
    tcph->ack_seq = 0;
    tcph->fin = 0;
    tcph->rst = 0;
    tcph->psh = 0;
    tcph->ack = 0;
    tcph->urg = 0;
    tcph->urg_ptr = 0;

    struct tcp_checksum_data checksum_dat;
    checksum_dat.ptcph.src_ip = local_ip;
    checksum_dat.ptcph.dst_ip = dst_addr.sin_addr.s_addr;
    checksum_dat.ptcph.dummy = 0;
    checksum_dat.ptcph.proto = IPPROTO_TCP;
    checksum_dat.ptcph.seg_len = htons(sizeof(struct tcphdr));

    for (int port = 1; port < 65536; port++) {
        dst_addr.sin_port = htons(port);
        tcph->dest = htons(port);
        tcph->source = local_port;
        tcph->check = 0;
        memcpy(&checksum_dat.tcph, tcph, sizeof(struct tcphdr));
        tcph->check = tcp_checksum(&checksum_dat);

        if (sendto(raw_sock, buf, sizeof(buf), 0, (const struct sockaddr *)&dst_addr, sizeof(dst_addr)) < 0) {
            perror("Error sending SYN packets");
            exit(1);
        }
    }

    close(raw_sock);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: %s [target_host]\n", argv[0]);
        exit(1);
    }

    if (inet_pton(AF_INET, argv[1], &dst_addr.sin_addr) == 0) {
        fprintf(stderr, "Error in parsing hostname: Address is invalid\n");
        exit(1);
    }

    if (argc >= 3 && strcmp(argv[2], "--include-closed") == 0) {
        include_closed = 1;
    }

    srand(time(0));
    set_local_ip();

    pthread_t recv_thread_id;
    pthread_create(&recv_thread_id, NULL, recv_data, NULL);

    printf("sending packets...\n");
    send_data();

    sleep(5);
    packets_sent = 1;

    pthread_join(recv_thread_id, NULL);
    printf("done.\n");

    return 0;
}
