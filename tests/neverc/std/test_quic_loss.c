/*
 * QUIC Loss Detection & Congestion Control Tests (RFC 9002)
 * Tests RTT estimation, packet loss detection, and New Reno congestion control.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Pull in source directly for unit testing */
#include "../../../std/src/net/quic/quic_loss.c"

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_EQ(got, expected) do { \
    tests_run++; \
    if ((got) == (expected)) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL %s:%d: %s == %llu, expected %llu\n", \
           __func__, __LINE__, #got, (unsigned long long)(got), (unsigned long long)(expected)); } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #cond); } \
} while(0)

/* ======================================================================
 * RTT Estimator Tests
 * ====================================================================== */

static void test_rtt_init(void) {
    quic_rtt_t rtt;
    neverc_quic_rtt_init(&rtt);
    ASSERT_EQ(rtt.smoothed_rtt, QUIC_INITIAL_RTT_MS);
    ASSERT_EQ(rtt.rttvar, QUIC_INITIAL_RTT_MS / 2);
    ASSERT_EQ(rtt.min_rtt, UINT64_MAX);
    ASSERT_EQ(rtt.has_sample, 0);
}

static void test_rtt_first_sample(void) {
    quic_rtt_t rtt;
    neverc_quic_rtt_init(&rtt);

    neverc_quic_rtt_update(&rtt, 100, 0, 0);
    ASSERT_EQ(rtt.smoothed_rtt, 100);
    ASSERT_EQ(rtt.rttvar, 50);
    ASSERT_EQ(rtt.min_rtt, 100);
    ASSERT_EQ(rtt.has_sample, 1);
}

static void test_rtt_ewma_update(void) {
    quic_rtt_t rtt;
    neverc_quic_rtt_init(&rtt);

    neverc_quic_rtt_update(&rtt, 100, 0, 0);
    /* smoothed=100, rttvar=50 */

    neverc_quic_rtt_update(&rtt, 120, 0, 1);
    /* abs_diff = |120-100| = 20, rttvar = (3*50 + 20)/4 = 42 */
    /* smoothed = (7*100 + 120)/8 = 102 */
    ASSERT_EQ(rtt.smoothed_rtt, 102);
    ASSERT_EQ(rtt.rttvar, 42);
    ASSERT_EQ(rtt.min_rtt, 100);
}

static void test_rtt_ack_delay_subtraction(void) {
    quic_rtt_t rtt;
    neverc_quic_rtt_init(&rtt);

    neverc_quic_rtt_update(&rtt, 100, 0, 0);
    /* Now handshake confirmed: ack_delay is subtracted */
    neverc_quic_rtt_update(&rtt, 150, 20, 1);
    /* adjusted_rtt = 150 - 20 = 130 (since 150 > min_rtt(100) + 20) */
    /* abs_diff = |130-100| = 30, rttvar = (3*50 + 30)/4 = 45 */
    /* smoothed = (7*100 + 130)/8 = 103 */
    ASSERT_EQ(rtt.smoothed_rtt, 103);
}

static void test_pto_computation(void) {
    quic_rtt_t rtt;
    neverc_quic_rtt_init(&rtt);
    neverc_quic_rtt_update(&rtt, 100, 0, 0);
    /* smoothed=100, rttvar=50 */

    uint64_t pto = neverc_quic_pto(&rtt, 0);
    /* 100 + 4*50 = 300 */
    ASSERT_EQ(pto, 300);

    pto = neverc_quic_pto(&rtt, 1);
    /* 300 + 25 (max_ack_delay) = 325 */
    ASSERT_EQ(pto, 325);

    /* RFC 9000 §10.1: used idle timeout is at least 3× PTO. */
    ASSERT_EQ(neverc_quic_idle_period_ms(100, &rtt, 0), 900);
    ASSERT_EQ(neverc_quic_idle_period_ms(5000, &rtt, 0), 5000);
    ASSERT_EQ(neverc_quic_idle_period_ms(0, &rtt, 0), 0);
}

/* ======================================================================
 * Congestion Control Tests
 * ====================================================================== */

static void test_cc_init(void) {
    quic_congestion_t cc;
    neverc_quic_congestion_init(&cc);
    ASSERT_EQ(cc.congestion_window, QUIC_INITIAL_WINDOW);
    ASSERT_EQ(cc.bytes_in_flight, 0);
    ASSERT_EQ(cc.ssthresh, UINT64_MAX);
    ASSERT_EQ(cc.in_recovery, 0);
}

static void test_cc_slow_start(void) {
    quic_congestion_t cc;
    neverc_quic_congestion_init(&cc);

    neverc_quic_congestion_on_sent(&cc, 1200);
    ASSERT_EQ(cc.bytes_in_flight, 1200);

    neverc_quic_congestion_on_ack(&cc, 1000, 1200, 1100);
    ASSERT_EQ(cc.bytes_in_flight, 0);
    /* Slow start: cwnd += acked_bytes */
    ASSERT_EQ(cc.congestion_window, QUIC_INITIAL_WINDOW + 1200);
}

static void test_cc_congestion_avoidance(void) {
    quic_congestion_t cc;
    neverc_quic_congestion_init(&cc);
    cc.ssthresh = QUIC_INITIAL_WINDOW; /* already past slow start */

    neverc_quic_congestion_on_sent(&cc, 1200);
    neverc_quic_congestion_on_ack(&cc, 1000, 1200, 1100);
    /* Congestion avoidance: cwnd += MSS * acked_bytes / cwnd
     * = 1200 * 1200 / 14720 = 97 */
    uint64_t expected = QUIC_INITIAL_WINDOW + (1200 * 1200 / QUIC_INITIAL_WINDOW);
    ASSERT_EQ(cc.congestion_window, expected);
}

static void test_cc_loss_reduces_window(void) {
    quic_congestion_t cc;
    neverc_quic_congestion_init(&cc);

    neverc_quic_congestion_on_sent(&cc, 5000);
    ASSERT_EQ(cc.bytes_in_flight, 5000);

    neverc_quic_congestion_on_loss(&cc, 1000, 5000, 2000);
    ASSERT_EQ(cc.bytes_in_flight, 0);
    ASSERT_EQ(cc.in_recovery, 1);
    /* ssthresh = cwnd/2 = 14720/2 = 7360 */
    ASSERT_EQ(cc.ssthresh, QUIC_INITIAL_WINDOW / 2);
    ASSERT_EQ(cc.congestion_window, QUIC_INITIAL_WINDOW / 2);
}

static void test_cc_minimum_window(void) {
    quic_congestion_t cc;
    neverc_quic_congestion_init(&cc);
    cc.congestion_window = 3000;

    neverc_quic_congestion_on_sent(&cc, 1000);
    neverc_quic_congestion_on_loss(&cc, 1000, 1000, 2000);
    /* ssthresh = 3000/2 = 1500 < MINIMUM_WINDOW(2400) → 2400 */
    ASSERT_EQ(cc.ssthresh, QUIC_MINIMUM_WINDOW);
    ASSERT_EQ(cc.congestion_window, QUIC_MINIMUM_WINDOW);
}

static void test_cc_can_send(void) {
    quic_congestion_t cc;
    neverc_quic_congestion_init(&cc);

    ASSERT_TRUE(neverc_quic_congestion_can_send(&cc));
    ASSERT_EQ(neverc_quic_congestion_available(&cc), QUIC_INITIAL_WINDOW);

    cc.bytes_in_flight = QUIC_INITIAL_WINDOW;
    ASSERT_TRUE(!neverc_quic_congestion_can_send(&cc));
    ASSERT_EQ(neverc_quic_congestion_available(&cc), 0);
}

/* ======================================================================
 * Loss Detector Integration Tests
 * ====================================================================== */

static void test_loss_detector_init(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);
    ASSERT_EQ(ld.rtt.smoothed_rtt, QUIC_INITIAL_RTT_MS);
    ASSERT_EQ(ld.cc.congestion_window, QUIC_INITIAL_WINDOW);
    ASSERT_EQ(ld.pto_count, 0);
    neverc_quic_loss_destroy(&ld);
}

static void test_loss_on_sent_tracking(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);

    neverc_quic_loss_on_sent(&ld, 2, 0, 1000, 1200, 1);
    neverc_quic_loss_on_sent(&ld, 2, 1, 1010, 1200, 1);
    neverc_quic_loss_on_sent(&ld, 2, 2, 1020, 1200, 1);

    ASSERT_EQ(ld.cc.bytes_in_flight, 3600);
    ASSERT_EQ(ld.spaces[2].time_of_last_ack_eliciting, 1020);

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_discard_space(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);

    neverc_quic_loss_on_sent(&ld, QUIC_PNS_INITIAL, 0, 1000, 1200, 1);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_HANDSHAKE, 0, 1010, 800, 1);
    ASSERT_EQ(ld.cc.bytes_in_flight, 2000);
    uint64_t window = ld.cc.congestion_window;

    neverc_quic_loss_discard_space(&ld, QUIC_PNS_INITIAL);
    ASSERT_EQ(ld.cc.bytes_in_flight, 800);
    ASSERT_EQ(ld.cc.congestion_window, window);
    ASSERT_TRUE(ld.spaces[QUIC_PNS_INITIAL].sent_packets == NULL);

    neverc_quic_loss_discard_space(&ld, QUIC_PNS_HANDSHAKE);
    ASSERT_EQ(ld.cc.bytes_in_flight, 0);
    ASSERT_EQ(ld.cc.congestion_window, window);

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_discard_space_resets_pto_count(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);

    neverc_quic_loss_on_sent(&ld, QUIC_PNS_HANDSHAKE, 0, 1010, 800, 1);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_APPLICATION, 0, 1020, 800, 1);
    ld.pto_count = 3;
    uint64_t inflated = neverc_quic_loss_get_timeout(&ld, 1);
    neverc_quic_loss_discard_space(&ld, QUIC_PNS_HANDSHAKE);
    ASSERT_EQ(ld.pto_count, 0);
    uint64_t pto = neverc_quic_pto(&ld.rtt, 1);
    uint64_t timeout = neverc_quic_loss_get_timeout(&ld, 1);
    ASSERT_EQ(timeout, 1020 + pto);
    ASSERT_TRUE(inflated > timeout);

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_packet_threshold_detection(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);

    /* Send packets 0..4 */
    for (int i = 0; i < 5; i++)
        neverc_quic_loss_on_sent(&ld, 2, (uint64_t)i, 1000 + (uint64_t)i * 10, 1200, 1);

    /* ACK packets 1, 2, 3, 4 (skip 0) */
    for (int i = 1; i <= 4; i++)
        neverc_quic_loss_mark_acked(&ld, 2, (uint64_t)i, 1200);

    neverc_quic_loss_on_ack(&ld, 2, 4, 5, 1200);

    /* Packet 0 should be marked lost: largest_acked(4) - pkt(0) = 4 >= THRESHOLD(3) */
    quic_sent_packet_t *pkt = ld.spaces[2].sent_packets;
    ASSERT_TRUE(pkt != NULL);
    ASSERT_EQ(pkt->pkt_number, 0);
    ASSERT_EQ(pkt->lost, 1);

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_ack_of_unsent_does_not_raise_largest(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);

    neverc_quic_loss_on_sent(&ld, 2, 0, 1000, 1200, 1);
    neverc_quic_loss_on_sent(&ld, 2, 1, 1010, 1200, 1);
    neverc_quic_loss_on_sent(&ld, 2, 2, 1020, 1200, 1);
    /* ACK of sealed-but-never-sent PN 5 must not trip the threshold. */
    neverc_quic_loss_on_ack(&ld, 2, 5, 0, 1200);

    ASSERT_EQ(ld.spaces[2].has_largest_acked, 0);
    quic_sent_packet_t *pkt = ld.spaces[2].sent_packets;
    while (pkt) {
        ASSERT_EQ(pkt->lost, 0);
        pkt = pkt->next;
    }

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_rtt_measurement(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);

    neverc_quic_loss_on_sent(&ld, 2, 0, 1000, 1200, 1);
    neverc_quic_loss_mark_acked(&ld, 2, 0, 1100);
    neverc_quic_loss_on_ack(&ld, 2, 0, 5, 1100);

    /* RTT sample = 1100 - 1000 = 100 */
    ASSERT_EQ(ld.rtt.smoothed_rtt, 100);
    ASSERT_EQ(ld.rtt.min_rtt, 100);

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_cleanup(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);

    neverc_quic_loss_on_sent(&ld, 2, 0, 1000, 1200, 1);
    neverc_quic_loss_on_sent(&ld, 2, 1, 1010, 1200, 1);
    neverc_quic_loss_mark_acked(&ld, 2, 0, 1100);

    neverc_quic_loss_cleanup(&ld, 2);

    /* Packet 0 (acked) should be removed, packet 1 remains */
    quic_sent_packet_t *pkt = ld.spaces[2].sent_packets;
    ASSERT_TRUE(pkt != NULL);
    ASSERT_EQ(pkt->pkt_number, 1);
    ASSERT_TRUE(pkt->next == NULL);

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_timeout(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);

    neverc_quic_loss_on_sent(&ld, 2, 0, 1000, 1200, 1);
    uint64_t timeout = neverc_quic_loss_get_timeout(&ld, 1);
    /* PTO = smoothed_rtt(333) + 4*rttvar(166) + max_ack_delay(25)
     * = 333 + 664 + 25 = 1022 from time 1000 → timeout = 2022 */
    ASSERT_TRUE(timeout > 1000);

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_pto_backoff(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);

    ld.pto_count = 0;
    uint64_t pto0 = neverc_quic_pto(&ld.rtt, 1);

    ld.pto_count = 1;
    /* PTO is exponentially backed off */
    neverc_quic_loss_on_sent(&ld, 2, 0, 1000, 1200, 1);
    uint64_t t1 = neverc_quic_loss_get_timeout(&ld, 1);

    ld.pto_count = 2;
    uint64_t t2 = neverc_quic_loss_get_timeout(&ld, 1);
    /* t2 should have larger PTO than t1 */
    ASSERT_TRUE(t2 > t1);
    (void)pto0;

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_timeout_skips_empty_packet_number_space(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_INITIAL, 0, 1000, 1200, 1);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_APPLICATION, 0, 2000,
                             1200, 1);
    neverc_quic_loss_mark_acked(&ld, QUIC_PNS_APPLICATION, 0, 2100);
    neverc_quic_loss_on_ack(&ld, QUIC_PNS_APPLICATION, 0, 0, 2100);
    neverc_quic_loss_cleanup(&ld, QUIC_PNS_APPLICATION);

    uint64_t expected = 1000 + neverc_quic_pto(&ld.rtt, 0);
    ASSERT_EQ(neverc_quic_loss_get_timeout(&ld, 1), expected);

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_timeout_skips_app_before_handshake_confirmation(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_APPLICATION, 0, 1000,
                             1200, 1);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_INITIAL, 0, 2000, 1200, 1);

    uint64_t initial = 2000 + neverc_quic_pto(&ld.rtt, 0);
    uint64_t app = 1000 + neverc_quic_pto(&ld.rtt, 1);
    ASSERT_TRUE(app < initial);
    ASSERT_EQ(neverc_quic_loss_get_timeout(&ld, 0), initial);
    ASSERT_EQ(neverc_quic_loss_get_timeout(&ld, 1), app);

    neverc_quic_loss_destroy(&ld);
}

static void test_loss_time_ignores_packets_newer_than_largest_acked(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_APPLICATION, 0, 1000,
                             1200, 1);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_APPLICATION, 1, 1090,
                             1200, 1);
    neverc_quic_loss_mark_acked(&ld, QUIC_PNS_APPLICATION, 0, 1100);
    neverc_quic_loss_on_ack(&ld, QUIC_PNS_APPLICATION, 0, 0, 1100);

    ASSERT_EQ(ld.spaces[QUIC_PNS_APPLICATION].loss_time, 0);
    ASSERT_TRUE(!ld.spaces[QUIC_PNS_APPLICATION].sent_packets->next->lost);
    ASSERT_EQ(neverc_quic_loss_get_timeout(&ld, 1),
              1090 + neverc_quic_pto(&ld.rtt, 1));

    neverc_quic_loss_destroy(&ld);
}

static void test_time_threshold_loss_expires_at_deadline(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);
    ld.rtt.smoothed_rtt = 8;
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_APPLICATION, 0, 1000,
                             1200, 1);
    ld.spaces[QUIC_PNS_APPLICATION].has_largest_acked = 1;
    ld.spaces[QUIC_PNS_APPLICATION].largest_acked_packet = 1;

    detect_lost_packets(&ld, QUIC_PNS_APPLICATION, 1009);

    ASSERT_TRUE(ld.spaces[QUIC_PNS_APPLICATION].sent_packets->lost);
    ASSERT_EQ(ld.spaces[QUIC_PNS_APPLICATION].loss_time, 0);

    neverc_quic_loss_destroy(&ld);
}

static void test_time_threshold_uses_max_of_srtt_and_latest_rtt(void) {
    /* RFC 9002 §6.1.2: loss_delay = 9/8 * max(smoothed_rtt, latest_rtt). */
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);
    ld.rtt.smoothed_rtt = 8;
    ld.rtt.latest_rtt = 80;
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_APPLICATION, 0, 1000,
                             1200, 1);
    ld.spaces[QUIC_PNS_APPLICATION].has_largest_acked = 1;
    ld.spaces[QUIC_PNS_APPLICATION].largest_acked_packet = 1;

    detect_lost_packets(&ld, QUIC_PNS_APPLICATION, 1009);

    ASSERT_TRUE(!ld.spaces[QUIC_PNS_APPLICATION].sent_packets->lost);
    ASSERT_EQ(ld.spaces[QUIC_PNS_APPLICATION].loss_time, 1090);

    detect_lost_packets(&ld, QUIC_PNS_APPLICATION, 1090);
    ASSERT_TRUE(ld.spaces[QUIC_PNS_APPLICATION].sent_packets->lost);

    neverc_quic_loss_destroy(&ld);
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(void) {
    printf("QUIC Loss Detection & Congestion Control test suite:\n\n");

    test_rtt_init();
    test_rtt_first_sample();
    test_rtt_ewma_update();
    test_rtt_ack_delay_subtraction();
    test_pto_computation();
    test_cc_init();
    test_cc_slow_start();
    test_cc_congestion_avoidance();
    test_cc_loss_reduces_window();
    test_cc_minimum_window();
    test_cc_can_send();
    test_loss_detector_init();
    test_loss_on_sent_tracking();
    test_loss_discard_space();
    test_loss_discard_space_resets_pto_count();
    test_loss_packet_threshold_detection();
    test_loss_ack_of_unsent_does_not_raise_largest();
    test_loss_rtt_measurement();
    test_loss_cleanup();
    test_loss_timeout();
    test_loss_pto_backoff();
    test_loss_timeout_skips_empty_packet_number_space();
    test_loss_timeout_skips_app_before_handshake_confirmation();
    test_loss_time_ignores_packets_newer_than_largest_acked();
    test_time_threshold_loss_expires_at_deadline();
    test_time_threshold_uses_max_of_srtt_and_latest_rtt();

    printf("\n%d passed, %d failed (of %d)\n", tests_passed, tests_failed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
