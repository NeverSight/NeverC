// RUN: %neverc -g -c %s -o %t.o
// Cross-compile to Windows x64 PE with DWARF v5 (freestanding, no CRT):
//   neverc --target=x86_64-pc-windows-msvc -gdwarf-5 -O1 -nostdlib \
//       -Wl,--entry=main \
//       tests/neverc/debug/test_pe_dwarf.c -o tests/neverc/debug/test_pe_dwarf.exe

struct NetAddr {
    unsigned int ip;
    unsigned short port;
};

struct PacketHeader {
    int type;
    unsigned int seq;
    unsigned int length;
    struct NetAddr src;
    struct NetAddr dst;
};

struct Payload {
    unsigned char data[64];
    unsigned int checksum;
};

struct Packet {
    struct PacketHeader header;
    struct Payload payload;
    unsigned encrypted : 1;
    unsigned compressed : 1;
    unsigned priority : 3;
    unsigned version : 4;
};

struct Stats {
    unsigned long long bytes_sent;
    unsigned long long bytes_recv;
    unsigned int packets_sent;
    unsigned int packets_recv;
    unsigned int errors;
};

static unsigned int compute_checksum(const unsigned char *data, unsigned int len) {
    unsigned int sum = 0;
    for (unsigned int i = 0; i < len; i++) {
        sum = (sum << 3) ^ (sum >> 29) ^ data[i];
    }
    return sum;
}

static void init_packet(struct Packet *pkt, int type, unsigned int seq) {
    pkt->header.type = type;
    pkt->header.seq = seq;
    pkt->header.length = 0;
    pkt->header.src.ip = 0x7F000001;
    pkt->header.src.port = 12345;
    pkt->header.dst.ip = 0xC0A80001;
    pkt->header.dst.port = 80;

    for (int i = 0; i < 64; i++)
        pkt->payload.data[i] = 0;

    pkt->payload.checksum = 0;
    pkt->encrypted = 0;
    pkt->compressed = 0;
    pkt->priority = 3;
    pkt->version = 2;
}

static void fill_payload(struct Packet *pkt, unsigned char pattern, unsigned int len) {
    if (len > 64) len = 64;
    for (unsigned int i = 0; i < len; i++)
        pkt->payload.data[i] = (unsigned char)(pattern ^ (i & 0xFF));
    pkt->header.length = len;
    pkt->payload.checksum = compute_checksum(pkt->payload.data, len);
}

int process_packets(struct Packet *packets, int count, struct Stats *stats) {
    int accepted = 0;

    for (int i = 0; i < count; i++) {
        struct Packet *p = &packets[i];
        stats->packets_recv++;
        stats->bytes_recv += p->header.length;

        unsigned int expected = compute_checksum(p->payload.data, p->header.length);
        if (expected != p->payload.checksum) {
            stats->errors++;
            continue;
        }

        switch (p->header.type) {
        case 1:
            if (p->header.seq == 0) accepted++;
            break;
        case 2:
            if (p->priority >= 2) {
                stats->bytes_sent += p->header.length;
                stats->packets_sent++;
                accepted++;
            }
            break;
        case 3:
            accepted++;
            break;
        case 4:
            return accepted;
        }
    }
    return accepted;
}

int main(int argc, char **argv) {
    struct Packet packets[4];
    struct Stats stats = {0, 0, 0, 0, 0};

    init_packet(&packets[0], 1, 0);
    fill_payload(&packets[0], 0xAA, 16);

    init_packet(&packets[1], 2, 1);
    fill_payload(&packets[1], 0xBB, 32);
    packets[1].encrypted = 1;
    packets[1].priority = 4;

    init_packet(&packets[2], 3, 2);

    init_packet(&packets[3], 2, 3);
    fill_payload(&packets[3], 0xCC, 48);
    packets[3].compressed = 1;
    packets[3].priority = 1;

    int result = process_packets(packets, 4, &stats);

    int exit_code = 0;
    if (result != 3) exit_code = 1;
    if (stats.errors != 0) exit_code = 1;
    if (stats.packets_recv != 4) exit_code = 1;

    for (int i = 0; i < argc && i < 4; i++)
        exit_code += packets[i].version;

    return exit_code;
}
