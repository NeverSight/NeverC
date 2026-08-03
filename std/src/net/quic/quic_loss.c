/*
 * QUIC Loss Detection & Congestion Control (RFC 9002)
 *
 * Implements:
 *   - Packet loss detection via time-based and packet-number-based thresholds
 *   - Probe Timeout (PTO) for tail loss detection
 *   - New Reno congestion control (default)
 *   - Smoothed RTT estimation
 *
 * The loss detector operates per-connection, tracking sent packets in each
 * of the three packet number spaces (Initial, Handshake, Application Data).
 */

#include "_quic_internal.h"

#include <string.h>
#include <stdlib.h>

/* ======================================================================
 * Constants (RFC 9002 §6.2)
 * ====================================================================== */

#define QUIC_INITIAL_RTT_MS            333  /* Default initial RTT estimate */
#define QUIC_MAX_ACK_DELAY_MS          25   /* Default max ack delay */
#define QUIC_TIME_THRESHOLD_FACTOR_NUM 9    /* 9/8 = 1.125x */
#define QUIC_TIME_THRESHOLD_FACTOR_DEN 8
#define QUIC_PACKET_THRESHOLD          3    /* Packets before considered lost */
#define QUIC_GRANULARITY_MS            1    /* Timer granularity */

/* Congestion control */
#define QUIC_INITIAL_WINDOW            14720  /* 10 * max_datagram_size */
#define QUIC_MINIMUM_WINDOW            2400   /* 2 * max_datagram_size */
#define QUIC_LOSS_REDUCTION_FACTOR_NUM 1
#define QUIC_LOSS_REDUCTION_FACTOR_DEN 2     /* 1/2 = halving */
#define QUIC_PERSISTENT_CONGESTION_THRESHOLD 3

static uint64_t quic_saturating_add(uint64_t left, uint64_t right) {
    return left > UINT64_MAX - right ? UINT64_MAX : left + right;
}

static uint64_t quic_saturating_mul(uint64_t left, uint64_t right) {
    return left != 0 && right > UINT64_MAX / left
        ? UINT64_MAX : left * right;
}

void neverc_quic_rtt_init(quic_rtt_t *rtt) {
    memset(rtt, 0, sizeof(*rtt));
    rtt->smoothed_rtt = QUIC_INITIAL_RTT_MS;
    rtt->rttvar = QUIC_INITIAL_RTT_MS / 2;
    rtt->min_rtt = UINT64_MAX;
    rtt->max_ack_delay = QUIC_MAX_ACK_DELAY_MS;
}

void neverc_quic_rtt_update(quic_rtt_t *rtt, uint64_t latest_rtt,
                              uint64_t ack_delay, int handshake_confirmed) {
    rtt->latest_rtt = latest_rtt;

    if (latest_rtt < rtt->min_rtt)
        rtt->min_rtt = latest_rtt;

    if (!rtt->has_sample) {
        rtt->smoothed_rtt = latest_rtt;
        rtt->rttvar = latest_rtt / 2;
        rtt->has_sample = 1;
        return;
    }

    /* Adjust ack_delay: only subtract after handshake, and cap at max_ack_delay */
    uint64_t adjusted_rtt = latest_rtt;
    if (handshake_confirmed) {
        uint64_t ad = ack_delay < rtt->max_ack_delay ? ack_delay : rtt->max_ack_delay;
        if (rtt->min_rtt <= UINT64_MAX - ad &&
            adjusted_rtt > rtt->min_rtt + ad)
            adjusted_rtt -= ad;
    }

    /* EWMA update (RFC 9002 §5.3) */
    uint64_t abs_diff = adjusted_rtt > rtt->smoothed_rtt
                      ? adjusted_rtt - rtt->smoothed_rtt
                      : rtt->smoothed_rtt - adjusted_rtt;
    rtt->rttvar = quic_saturating_add(
        quic_saturating_mul(3, rtt->rttvar), abs_diff) / 4;
    rtt->smoothed_rtt = quic_saturating_add(
        quic_saturating_mul(7, rtt->smoothed_rtt), adjusted_rtt) / 8;
}

/* PTO computation (RFC 9002 §6.2.1) */
uint64_t neverc_quic_pto(const quic_rtt_t *rtt, int include_max_ack_delay) {
    uint64_t pto = quic_saturating_add(
        rtt->smoothed_rtt, quic_saturating_mul(4, rtt->rttvar));
    if (pto < QUIC_GRANULARITY_MS)
        pto = QUIC_GRANULARITY_MS;
    if (include_max_ack_delay)
        pto = quic_saturating_add(pto, rtt->max_ack_delay);
    return pto;
}

void neverc_quic_congestion_init(quic_congestion_t *cc) {
    memset(cc, 0, sizeof(*cc));
    cc->max_datagram_size = 1200;
    cc->congestion_window = QUIC_INITIAL_WINDOW;
    cc->ssthresh = UINT64_MAX;
}

/* Called when a packet is acknowledged */
void neverc_quic_congestion_on_ack(quic_congestion_t *cc,
                                    uint64_t sent_time,
                                    size_t acked_bytes,
                                    uint64_t now_ms) {
    (void)now_ms;
    if (!cc || acked_bytes == 0) return;

    cc->bytes_in_flight = acked_bytes >= cc->bytes_in_flight
        ? 0 : cc->bytes_in_flight - acked_bytes;

    /* Don't increase cwnd during recovery */
    if (cc->in_recovery && sent_time <= cc->recovery_start_time)
        return;

    if (cc->in_recovery) {
        cc->in_recovery = 0;
    }

    if (cc->congestion_window < cc->ssthresh) {
        /* Slow start: increase by acked_bytes */
        cc->congestion_window = quic_saturating_add(
            cc->congestion_window, acked_bytes);
    } else {
        /* Congestion avoidance: increase by MSS per cwnd */
        uint64_t increase = cc->congestion_window == 0 ? 0 :
            quic_saturating_mul(cc->max_datagram_size, acked_bytes) /
                cc->congestion_window;
        cc->congestion_window = quic_saturating_add(
            cc->congestion_window, increase);
    }
}

/* Called when a packet is detected as lost */
void neverc_quic_congestion_on_loss(quic_congestion_t *cc,
                                     uint64_t sent_time,
                                     size_t lost_bytes,
                                     uint64_t now_ms) {
    if (!cc) return;
    cc->bytes_in_flight = lost_bytes >= cc->bytes_in_flight
        ? 0 : cc->bytes_in_flight - lost_bytes;

    /* Enter recovery if not already in recovery for this period */
    if (!cc->in_recovery || sent_time > cc->recovery_start_time) {
        cc->recovery_start_time = now_ms;
        cc->in_recovery = 1;
        cc->ssthresh = cc->congestion_window / 2;
        if (cc->ssthresh < QUIC_MINIMUM_WINDOW)
            cc->ssthresh = QUIC_MINIMUM_WINDOW;
        cc->congestion_window = cc->ssthresh;
    }
}

/* Called when a packet is sent */
void neverc_quic_congestion_on_sent(quic_congestion_t *cc, size_t sent_bytes) {
    if (cc)
        cc->bytes_in_flight = quic_saturating_add(
            cc->bytes_in_flight, sent_bytes);
}

/* Can we send? */
int neverc_quic_congestion_can_send(const quic_congestion_t *cc) {
    return cc->bytes_in_flight < cc->congestion_window;
}

/* Available window */
uint64_t neverc_quic_congestion_available(const quic_congestion_t *cc) {
    if (cc->bytes_in_flight >= cc->congestion_window) return 0;
    return cc->congestion_window - cc->bytes_in_flight;
}

/* ======================================================================
 * Loss Detection Algorithm (RFC 9002 §6)
 * ====================================================================== */

void neverc_quic_loss_init(quic_loss_detector_t *ld) {
    memset(ld, 0, sizeof(*ld));
    neverc_quic_rtt_init(&ld->rtt);
    neverc_quic_congestion_init(&ld->cc);
}

/* Record a sent packet */
void neverc_quic_loss_on_sent(quic_loss_detector_t *ld, int space,
                               uint64_t pkt_number, uint64_t sent_time,
                               size_t sent_bytes, int ack_eliciting) {
    quic_sent_packet_t *pkt = (quic_sent_packet_t *)calloc(1, sizeof(*pkt));
    if (!pkt) return;

    pkt->pkt_number = pkt_number;
    pkt->sent_time_ms = sent_time;
    pkt->sent_bytes = sent_bytes;
    pkt->ack_eliciting = ack_eliciting;
    pkt->in_flight = (sent_bytes > 0);

    /* Insert at tail */
    quic_sent_packet_t **tail = &ld->spaces[space].sent_packets;
    while (*tail) tail = &(*tail)->next;
    *tail = pkt;

    if (pkt->in_flight)
        neverc_quic_congestion_on_sent(&ld->cc, sent_bytes);

    if (ack_eliciting)
        ld->spaces[space].time_of_last_ack_eliciting = sent_time;
}

/* Detect lost packets (RFC 9002 §6.1) */
static void detect_lost_packets(quic_loss_detector_t *ld, int space,
                                 uint64_t now_ms) {
    quic_loss_space_t *ls = &ld->spaces[space];
    if (!ls->has_largest_acked) return;

    uint64_t largest_acked = ls->largest_acked_packet;

    /* Time threshold */
    uint64_t time_threshold = quic_saturating_mul(
        ld->rtt.smoothed_rtt, QUIC_TIME_THRESHOLD_FACTOR_NUM) /
            QUIC_TIME_THRESHOLD_FACTOR_DEN;
    if (time_threshold < QUIC_GRANULARITY_MS)
        time_threshold = QUIC_GRANULARITY_MS;

    ls->loss_time = 0;

    quic_sent_packet_t *pkt = ls->sent_packets;
    while (pkt) {
        if (pkt->acked || pkt->lost) {
            pkt = pkt->next;
            continue;
        }
        if (pkt->pkt_number > largest_acked) {
            pkt = pkt->next;
            continue;
        }

        int lost = 0;

        /* Packet threshold: lost if largest_acked - pkt_number >= threshold */
        if (largest_acked >= pkt->pkt_number &&
            largest_acked - pkt->pkt_number >= QUIC_PACKET_THRESHOLD) {
            lost = 1;
        }

        /* Time threshold: lost if sent long enough ago */
        if (now_ms >= pkt->sent_time_ms &&
            now_ms - pkt->sent_time_ms >= time_threshold) {
            lost = 1;
        }

        if (lost) {
            pkt->lost = 1;
            if (pkt->in_flight) {
                neverc_quic_congestion_on_loss(&ld->cc, pkt->sent_time_ms,
                                               pkt->sent_bytes, now_ms);
            }
        } else {
            /* Calculate when this packet would be considered lost by time */
            uint64_t loss_deadline = quic_saturating_add(
                pkt->sent_time_ms, time_threshold);
            if (ls->loss_time == 0 || loss_deadline < ls->loss_time)
                ls->loss_time = loss_deadline;
        }

        pkt = pkt->next;
    }
}

int neverc_quic_loss_detect(quic_loss_detector_t *ld, uint64_t now_ms) {
    if (!ld) return 0;
    int lost = 0;
    for (int space = 0; space < QUIC_PN_SPACE_COUNT; space++) {
        quic_loss_space_t *loss_space = &ld->spaces[space];
        if (loss_space->loss_time == 0 || loss_space->loss_time > now_ms)
            continue;
        detect_lost_packets(ld, space, now_ms);
        for (quic_sent_packet_t *packet = loss_space->sent_packets;
             packet; packet = packet->next) {
            if (packet->lost) {
                lost = 1;
                break;
            }
        }
    }
    return lost;
}

/* Mark a specific packet as acknowledged */
void neverc_quic_loss_mark_acked(quic_loss_detector_t *ld, int space,
                                   uint64_t pkt_number, uint64_t now_ms) {
    quic_loss_space_t *ls = &ld->spaces[space];
    quic_sent_packet_t *pkt = ls->sent_packets;
    while (pkt) {
        if (pkt->pkt_number == pkt_number && !pkt->acked && !pkt->lost) {
            pkt->acked = 1;
            if (pkt->in_flight) {
                neverc_quic_congestion_on_ack(&ld->cc, pkt->sent_time_ms,
                                               pkt->sent_bytes, now_ms);
            }
            break;
        }
        pkt = pkt->next;
    }
}

/* Process ACK frame: call after marking individual acked packets */
void neverc_quic_loss_on_ack(quic_loss_detector_t *ld, int space,
                              uint64_t largest_acked,
                              uint64_t ack_delay_ms,
                              uint64_t now_ms) {
    quic_loss_space_t *ls = &ld->spaces[space];

    if (!ls->has_largest_acked || largest_acked > ls->largest_acked_packet) {
        ls->largest_acked_packet = largest_acked;
        ls->has_largest_acked = 1;
    }

    /* Find the sent packet matching largest_acked for RTT measurement */
    quic_sent_packet_t *pkt = ls->sent_packets;
    while (pkt) {
        if (pkt->pkt_number == largest_acked && pkt->acked) {
            uint64_t rtt_sample = now_ms >= pkt->sent_time_ms
                ? now_ms - pkt->sent_time_ms : 0;
            int handshake_confirmed = (space == 2);
            neverc_quic_rtt_update(&ld->rtt, rtt_sample, ack_delay_ms,
                                    handshake_confirmed);
            break;
        }
        pkt = pkt->next;
    }

    /* Detect losses among un-acked packets below largest_acked */
    detect_lost_packets(ld, space, now_ms);

    /* Reset PTO count on successful ACK */
    ld->pto_count = 0;
}

/* Get loss detection timeout (0 = no timer needed) */
uint64_t neverc_quic_loss_get_timeout(const quic_loss_detector_t *ld,
                                      int handshake_confirmed) {
    /* Time-threshold loss deadlines take precedence over PTO candidates. */
    uint64_t earliest_loss_time = 0;
    for (int i = 0; i < QUIC_PN_SPACE_COUNT; i++) {
        if (ld->spaces[i].loss_time > 0) {
            if (earliest_loss_time == 0 || ld->spaces[i].loss_time < earliest_loss_time)
                earliest_loss_time = ld->spaces[i].loss_time;
        }
    }
    if (earliest_loss_time > 0)
        return earliest_loss_time;

    /* PTO is selected independently from spaces with packets in flight. */
    uint64_t earliest_pto = 0;
    for (int space = 0; space < QUIC_PN_SPACE_COUNT; space++) {
        if (space == QUIC_PNS_APPLICATION && !handshake_confirmed)
            continue;
        int has_ack_eliciting_in_flight = 0;
        for (const quic_sent_packet_t *packet =
                 ld->spaces[space].sent_packets;
             packet; packet = packet->next) {
            if (packet->ack_eliciting && packet->in_flight &&
                !packet->acked && !packet->lost) {
                has_ack_eliciting_in_flight = 1;
                break;
            }
        }
        if (!has_ack_eliciting_in_flight) continue;

        uint64_t duration = neverc_quic_pto(
            &ld->rtt, space == QUIC_PNS_APPLICATION);
        if (ld->pto_count >= 63 ||
            duration > (UINT64_MAX >> ld->pto_count))
            duration = UINT64_MAX;
        else
            duration <<= ld->pto_count;
        uint64_t candidate = quic_saturating_add(
            ld->spaces[space].time_of_last_ack_eliciting, duration);
        if (earliest_pto == 0 || candidate < earliest_pto)
            earliest_pto = candidate;
    }
    return earliest_pto;
}

/* Clean up acknowledged/lost packets from the tracking list */
void neverc_quic_loss_cleanup(quic_loss_detector_t *ld, int space) {
    quic_sent_packet_t **pp = &ld->spaces[space].sent_packets;
    while (*pp) {
        quic_sent_packet_t *pkt = *pp;
        if (pkt->acked || pkt->lost) {
            *pp = pkt->next;
            free(pkt);
        } else {
            pp = &pkt->next;
        }
    }
}

void neverc_quic_loss_destroy(quic_loss_detector_t *ld) {
    for (int i = 0; i < 3; i++) {
        quic_sent_packet_t *pkt = ld->spaces[i].sent_packets;
        while (pkt) {
            quic_sent_packet_t *next = pkt->next;
            free(pkt);
            pkt = next;
        }
    }
}
