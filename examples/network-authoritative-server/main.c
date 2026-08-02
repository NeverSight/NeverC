#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <neverc/std/crypto/rand.h>
#include <neverc/std/crypto/subtle.h>
#include <neverc/std/net/quic.h>
#include <neverc/std/net/tcp.h>
#include <neverc/std/net/udp.h>
#include <neverc/std/thread.h>
#include <neverc/std/time.h>

#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAME_TICK_HZ 60
#define GAME_TICK_NS (NEVERC_TIME_SECOND / GAME_TICK_HZ)
#define SESSION_CAPACITY 4096U
#define NONCE_CAPACITY 8192U
#define INPUT_QUEUE_CAPACITY 8192U
#define SESSION_TTL_MS (30LL * 60LL * 1000LL)
#define JOIN_CLOCK_SKEW_MS 30000LL
#define TOKEN_SIZE 16U
#define NONCE_SIZE 16U

typedef struct {
    atomic_flag flag;
} game_lock_t;

typedef struct {
    uint8_t token[TOKEN_SIZE];
    char client_id[65];
    int64_t expires_ms;
    uint64_t last_sequence;
    int32_t x;
    int32_t y;
    int occupied;
} game_session_t;

typedef struct {
    uint8_t nonce[NONCE_SIZE];
    int64_t expires_ms;
    int occupied;
} game_nonce_t;

typedef struct {
    size_t session_index;
    uint8_t session_token[TOKEN_SIZE];
    uint64_t sequence;
    int32_t dx;
    int32_t dy;
    int reply_udp;
    neverc_udp_addr_t reply_addr;
} game_input_t;

typedef struct game_app game_app_t;

typedef struct {
    game_app_t *app;
    neverc_quic_conn_t *connection;
} game_quic_client_t;

struct game_app {
    atomic_int running;
    game_lock_t state_lock;
    game_session_t sessions[SESSION_CAPACITY];
    game_nonce_t nonces[NONCE_CAPACITY];
    size_t session_cursor;
    size_t nonce_cursor;
    neverc_thread_channel_t *inputs;
    neverc_thread_executor_t *executor;
    neverc_tcp_listener_t *control;
    neverc_udp_conn_t *udp;
    neverc_quic_endpoint_t *quic;
    atomic_uint_fast64_t tick;
};

static volatile sig_atomic_t game_stop_requested;

static void game_handle_signal(int signal_number) {
    (void)signal_number;
    game_stop_requested = 1;
}

static void game_lock_init(game_lock_t *lock) {
    atomic_flag_clear_explicit(&lock->flag, memory_order_release);
}

static void game_lock_acquire(game_lock_t *lock) {
    while (atomic_flag_test_and_set_explicit(&lock->flag,
                                              memory_order_acquire)) {
    }
}

static void game_lock_release(game_lock_t *lock) {
    atomic_flag_clear_explicit(&lock->flag, memory_order_release);
}

static int64_t game_now_ms(void) {
    return neverc_time_unix_milli(neverc_time_now());
}

static int game_hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int game_hex_decode(const char *text, uint8_t *output,
                           size_t output_length) {
    if (!text || strlen(text) != output_length * 2U) return -1;
    for (size_t i = 0; i < output_length; i++) {
        int high = game_hex_value(text[i * 2U]);
        int low = game_hex_value(text[i * 2U + 1U]);
        if (high < 0 || low < 0) return -1;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return 0;
}

static void game_hex_encode(const uint8_t *input, size_t input_length,
                            char *output) {
    static const char alphabet[] = "0123456789abcdef";
    for (size_t i = 0; i < input_length; i++) {
        output[i * 2U] = alphabet[input[i] >> 4];
        output[i * 2U + 1U] = alphabet[input[i] & 15U];
    }
    output[input_length * 2U] = '\0';
}

static int game_valid_client_id(const char *client_id) {
    size_t length = client_id ? strlen(client_id) : 0;
    if (length == 0 || length > 64U) return 0;
    for (size_t i = 0; i < length; i++) {
        char value = client_id[i];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '-' ||
              value == '_'))
            return 0;
    }
    return 1;
}

static int game_nonce_accept_locked(game_app_t *app,
                                    const uint8_t nonce[NONCE_SIZE],
                                    int64_t now_ms) {
    for (size_t i = 0; i < NONCE_CAPACITY; i++) {
        game_nonce_t *entry = &app->nonces[i];
        if (entry->occupied && entry->expires_ms > now_ms &&
            neverc_subtle_constant_time_compare(entry->nonce, nonce,
                                                 NONCE_SIZE))
            return 0;
    }
    game_nonce_t *entry = &app->nonces[app->nonce_cursor++ % NONCE_CAPACITY];
    memcpy(entry->nonce, nonce, NONCE_SIZE);
    entry->expires_ms = now_ms + JOIN_CLOCK_SKEW_MS;
    entry->occupied = 1;
    return 1;
}

static int game_issue_session(game_app_t *app, const char *client_id,
                              int64_t timestamp_ms,
                              const uint8_t nonce[NONCE_SIZE],
                              uint8_t token[TOKEN_SIZE]) {
    int64_t now_ms = game_now_ms();
    if (timestamp_ms < now_ms - JOIN_CLOCK_SKEW_MS ||
        timestamp_ms > now_ms + JOIN_CLOCK_SKEW_MS ||
        neverc_crypto_rand_read(token, TOKEN_SIZE) != 0)
        return -1;
    game_lock_acquire(&app->state_lock);
    if (!game_nonce_accept_locked(app, nonce, now_ms)) {
        game_lock_release(&app->state_lock);
        return -1;
    }
    size_t selected = SESSION_CAPACITY;
    for (size_t probe = 0; probe < SESSION_CAPACITY; probe++) {
        size_t index = (app->session_cursor + probe) % SESSION_CAPACITY;
        game_session_t *session = &app->sessions[index];
        if (!session->occupied || session->expires_ms <= now_ms) {
            selected = index;
            break;
        }
    }
    if (selected == SESSION_CAPACITY) {
        game_lock_release(&app->state_lock);
        return -1;
    }
    game_session_t *session = &app->sessions[selected];
    memset(session, 0, sizeof(*session));
    memcpy(session->token, token, TOKEN_SIZE);
    memcpy(session->client_id, client_id, strlen(client_id) + 1U);
    session->expires_ms = now_ms + SESSION_TTL_MS;
    session->occupied = 1;
    app->session_cursor = (selected + 1U) % SESSION_CAPACITY;
    game_lock_release(&app->state_lock);
    return 0;
}

static int game_find_session_locked(game_app_t *app,
                                    const uint8_t token[TOKEN_SIZE],
                                    int64_t now_ms) {
    for (size_t i = 0; i < SESSION_CAPACITY; i++) {
        game_session_t *session = &app->sessions[i];
        if (session->occupied && session->expires_ms > now_ms &&
            neverc_subtle_constant_time_compare(session->token, token,
                                                 TOKEN_SIZE))
            return (int)i;
    }
    return -1;
}

static int game_accept_sequence(game_app_t *app,
                                const uint8_t token[TOKEN_SIZE],
                                uint64_t sequence, size_t *session_index) {
    int accepted = -1;
    game_lock_acquire(&app->state_lock);
    int index = game_find_session_locked(app, token, game_now_ms());
    if (index >= 0 && sequence > app->sessions[index].last_sequence) {
        app->sessions[index].last_sequence = sequence;
        app->sessions[index].expires_ms = game_now_ms() + SESSION_TTL_MS;
        *session_index = (size_t)index;
        accepted = 0;
    }
    game_lock_release(&app->state_lock);
    return accepted;
}

static int game_authenticate_session(game_app_t *app,
                                     const uint8_t token[TOKEN_SIZE],
                                     size_t *session_index) {
    game_lock_acquire(&app->state_lock);
    int index = game_find_session_locked(app, token, game_now_ms());
    if (index >= 0) *session_index = (size_t)index;
    game_lock_release(&app->state_lock);
    return index >= 0 ? 0 : -1;
}

static int game_tcp_read_line(neverc_tcp_conn_t *connection,
                              char *line, size_t capacity) {
    size_t length = 0;
    while (length + 1U < capacity) {
        char value;
        int count = neverc_tcp_read(connection, &value, 1U);
        if (count != 1) return -1;
        if (value == '\n') {
            if (length > 0 && line[length - 1U] == '\r') length--;
            line[length] = '\0';
            return 0;
        }
        line[length++] = value;
    }
    return -1;
}

static int game_quic_read_line(neverc_quic_stream_t *stream,
                               char *line, size_t capacity) {
    size_t length = 0;
    while (length + 1U < capacity) {
        char value;
        int count = neverc_quic_stream_read(stream, &value, 1U);
        if (count != 1) return -1;
        if (value == '\n') {
            if (length > 0 && line[length - 1U] == '\r') length--;
            line[length] = '\0';
            return 0;
        }
        line[length++] = value;
    }
    return -1;
}

static void game_control_worker(void *context) {
    game_app_t *app = (game_app_t *)context;
    while (atomic_load_explicit(&app->running, memory_order_acquire)) {
        neverc_tcp_conn_t *connection = NULL;
        neverc_net_result_t accepted =
            neverc_tcp_try_accept(app->control, &connection);
        if (accepted.status == NEVERC_NET_WOULD_BLOCK) {
            neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
            continue;
        }
        if (accepted.status != NEVERC_NET_OK || !connection) break;
        (void)neverc_tcp_set_timeout(connection, 5000);
        char line[512];
        char command[16];
        char client_id[65];
        char nonce_text[NONCE_SIZE * 2U + 1U];
        long long timestamp_ms;
        uint8_t nonce[NONCE_SIZE];
        uint8_t token[TOKEN_SIZE];
        int parsed = game_tcp_read_line(connection, line, sizeof(line)) == 0
            ? sscanf(line, "%15s %64s %lld %32s", command, client_id,
                     &timestamp_ms, nonce_text)
            : 0;
        if (parsed == 4 && strcmp(command, "JOIN") == 0 &&
            game_valid_client_id(client_id) &&
            game_hex_decode(nonce_text, nonce, sizeof(nonce)) == 0 &&
            game_issue_session(app, client_id, (int64_t)timestamp_ms,
                               nonce, token) == 0) {
            char token_text[TOKEN_SIZE * 2U + 1U];
            char response[96];
            game_hex_encode(token, sizeof(token), token_text);
            int length = snprintf(response, sizeof(response),
                                  "SESSION %s %lld\n", token_text,
                                  (long long)(game_now_ms() + SESSION_TTL_MS));
            if (length > 0 && (size_t)length < sizeof(response))
                (void)neverc_tcp_write(connection, response, (size_t)length);
        } else {
            static const char rejected[] = "REJECT\n";
            (void)neverc_tcp_write(connection, rejected,
                                   sizeof(rejected) - 1U);
        }
        neverc_tcp_close(connection);
    }
}

static int game_enqueue_input(game_app_t *app, size_t session_index,
                              const uint8_t token[TOKEN_SIZE],
                              uint64_t sequence, int32_t dx, int32_t dy,
                              const neverc_udp_addr_t *reply_addr) {
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return -1;
    game_input_t *input = (game_input_t *)calloc(1, sizeof(*input));
    if (!input) return -1;
    input->session_index = session_index;
    memcpy(input->session_token, token, TOKEN_SIZE);
    input->sequence = sequence;
    input->dx = dx;
    input->dy = dy;
    if (reply_addr) {
        input->reply_udp = 1;
        input->reply_addr = *reply_addr;
    }
    if (neverc_thread_channel_try_send(app->inputs, input) !=
        NEVERC_THREAD_OK) {
        free(input);
        return -1;
    }
    return 0;
}

static void game_udp_worker(void *context) {
    game_app_t *app = (game_app_t *)context;
    char packet[512];
    while (atomic_load_explicit(&app->running, memory_order_acquire)) {
        neverc_udp_addr_t from;
        int count = neverc_udp_read_from(app->udp, packet,
                                         sizeof(packet) - 1U, &from);
        if (count <= 0) {
            neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
            continue;
        }
        packet[count] = '\0';
        char command[16];
        char token_text[TOKEN_SIZE * 2U + 1U];
        unsigned long long sequence;
        int dx;
        int dy;
        if (sscanf(packet, "%15s %32s %llu %d %d", command, token_text,
                   &sequence, &dx, &dy) != 5 ||
            strcmp(command, "INPUT") != 0)
            continue;
        uint8_t token[TOKEN_SIZE];
        size_t session_index;
        if (game_hex_decode(token_text, token, sizeof(token)) != 0 ||
            game_accept_sequence(app, token, (uint64_t)sequence,
                                 &session_index) != 0)
            continue;
        (void)game_enqueue_input(app, session_index, token,
                                 (uint64_t)sequence,
                                 (int32_t)dx, (int32_t)dy, &from);
    }
}

static void game_quic_client_worker(void *context) {
    game_quic_client_t *client = (game_quic_client_t *)context;
    game_app_t *app = client->app;
    neverc_quic_conn_t *connection = client->connection;
    free(client);
    const char *error = NULL;
    neverc_quic_stream_t *stream = neverc_quic_accept_stream(connection,
                                                               &error);
    char line[128];
    char command[16];
    char token_text[TOKEN_SIZE * 2U + 1U];
    uint8_t token[TOKEN_SIZE];
    size_t session_index;
    if (!stream || game_quic_read_line(stream, line, sizeof(line)) != 0 ||
        sscanf(line, "%15s %32s", command, token_text) != 2 ||
        strcmp(command, "AUTH") != 0 ||
        game_hex_decode(token_text, token, sizeof(token)) != 0 ||
        game_authenticate_session(app, token, &session_index) != 0) {
        if (stream) neverc_quic_stream_free(stream);
        neverc_quic_conn_close(connection, 1U, "authentication failed");
        neverc_quic_conn_free(connection);
        return;
    }
    static const char accepted[] = "OK\n";
    (void)neverc_quic_stream_write(stream, accepted,
                                   sizeof(accepted) - 1U);
    (void)neverc_quic_stream_close_write(stream);
    neverc_quic_stream_free(stream);
    char packet[128];
    while (atomic_load_explicit(&app->running, memory_order_acquire) &&
           neverc_quic_conn_is_alive(connection)) {
        int count = neverc_quic_recv_datagram(connection, packet,
                                               sizeof(packet) - 1U);
        if (count <= 0) break;
        packet[count] = '\0';
        unsigned long long sequence;
        int dx;
        int dy;
        if (sscanf(packet, "%llu %d %d", &sequence, &dx, &dy) != 3 ||
            game_accept_sequence(app, token, (uint64_t)sequence,
                                 &session_index) != 0 ||
            game_enqueue_input(app, session_index, token,
                               (uint64_t)sequence,
                               (int32_t)dx, (int32_t)dy, NULL) != 0)
            continue;
        neverc_time_sleep(GAME_TICK_NS);
        int snapshot_valid = 0;
        int32_t snapshot_x = 0;
        int32_t snapshot_y = 0;
        game_lock_acquire(&app->state_lock);
        int snapshot_index =
            game_find_session_locked(app, token, game_now_ms());
        if (snapshot_index >= 0 && (size_t)snapshot_index == session_index) {
            snapshot_x = app->sessions[session_index].x;
            snapshot_y = app->sessions[session_index].y;
            snapshot_valid = 1;
        }
        game_lock_release(&app->state_lock);
        if (!snapshot_valid) continue;
        char response[128];
        int length = snprintf(response, sizeof(response),
                              "STATE %llu %llu %d %d",
                              (unsigned long long)atomic_load_explicit(
                                  &app->tick, memory_order_relaxed),
                              sequence, snapshot_x, snapshot_y);
        if (length > 0 && (size_t)length < sizeof(response))
            (void)neverc_quic_send_datagram(connection, response,
                                             (size_t)length);
    }
    neverc_quic_conn_close(connection, 0U, "client disconnected");
    neverc_quic_conn_free(connection);
}

static void game_quic_worker(void *context) {
    game_app_t *app = (game_app_t *)context;
    while (atomic_load_explicit(&app->running, memory_order_acquire)) {
        const char *error = NULL;
        neverc_quic_conn_t *connection = neverc_quic_accept(app->quic,
                                                              &error);
        if (!connection) break;
        game_quic_client_t *client =
            (game_quic_client_t *)malloc(sizeof(*client));
        if (!client) {
            neverc_quic_conn_close(connection, 2U, "server overloaded");
            neverc_quic_conn_free(connection);
            continue;
        }
        client->app = app;
        client->connection = connection;
        if (neverc_thread_executor_try_submit(app->executor,
                                               game_quic_client_worker,
                                               client) != NEVERC_THREAD_OK) {
            free(client);
            neverc_quic_conn_close(connection, 2U, "server overloaded");
            neverc_quic_conn_free(connection);
        }
    }
}

static void game_apply_tick(game_app_t *app) {
    uint64_t tick = atomic_fetch_add_explicit(&app->tick, 1U,
                                               memory_order_relaxed) + 1U;
    (void)tick;
    for (size_t processed = 0; processed < INPUT_QUEUE_CAPACITY; processed++) {
        void *value = NULL;
        if (neverc_thread_channel_try_receive(app->inputs, &value) !=
            NEVERC_THREAD_OK)
            break;
        game_input_t *input = (game_input_t *)value;
        int32_t x = 0;
        int32_t y = 0;
        int valid_session = 0;
        game_lock_acquire(&app->state_lock);
        game_session_t *session = &app->sessions[input->session_index];
        if (session->occupied && session->expires_ms > game_now_ms() &&
            neverc_subtle_constant_time_compare(
                session->token, input->session_token, TOKEN_SIZE)) {
            session->x += input->dx;
            session->y += input->dy;
            if (session->x < -10000) session->x = -10000;
            if (session->x > 10000) session->x = 10000;
            if (session->y < -10000) session->y = -10000;
            if (session->y > 10000) session->y = 10000;
            x = session->x;
            y = session->y;
            valid_session = 1;
        }
        game_lock_release(&app->state_lock);
        if (valid_session && input->reply_udp) {
            char response[128];
            int length = snprintf(response, sizeof(response),
                                  "STATE %llu %llu %d %d",
                                  (unsigned long long)tick,
                                  (unsigned long long)input->sequence, x, y);
            if (length > 0 && (size_t)length < sizeof(response))
                (void)neverc_udp_write_to(app->udp, response,
                                           (size_t)length,
                                           &input->reply_addr);
        }
        free(input);
    }
}

static void game_app_stop_and_free(game_app_t *app) {
    if (!app) return;
    atomic_store_explicit(&app->running, 0, memory_order_release);
    if (app->quic) neverc_quic_endpoint_close(app->quic);
    if (app->executor) {
        (void)neverc_thread_executor_shutdown(app->executor);
        neverc_thread_executor_free(app->executor);
    }
    if (app->control) neverc_tcp_listener_close(app->control);
    if (app->udp) neverc_udp_close(app->udp);
    if (app->inputs) {
        (void)neverc_thread_channel_close(app->inputs);
        for (;;) {
            void *value = NULL;
            if (neverc_thread_channel_try_receive(app->inputs, &value) !=
                NEVERC_THREAD_OK)
                break;
            free(value);
        }
        neverc_thread_channel_free(app->inputs);
    }
    free(app);
}

static void game_usage(const char *program) {
    fprintf(stderr,
            "usage: %s <cert.pem> <key.pem> [control-addr] [udp-addr] "
            "[quic-addr]\n",
            program);
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 6) {
        game_usage(argv[0]);
        return 2;
    }
    const char *control_addr = argc > 3 ? argv[3] : "0.0.0.0:7000";
    const char *udp_addr = argc > 4 ? argv[4] : "0.0.0.0:7001";
    const char *quic_addr = argc > 5 ? argv[5] : "0.0.0.0:7002";
    (void)signal(SIGINT, game_handle_signal);
    (void)signal(SIGTERM, game_handle_signal);
    game_app_t *app = (game_app_t *)calloc(1, sizeof(*app));
    if (!app) return 1;
    game_lock_init(&app->state_lock);
    atomic_store_explicit(&app->running, 1, memory_order_release);
    app->inputs = neverc_thread_channel_create(INPUT_QUEUE_CAPACITY);
    app->executor = neverc_thread_executor_create(16U, 128U);
    const char *error = NULL;
    app->control = neverc_tcp_listen(control_addr, &error);
    app->udp = neverc_udp_listen(udp_addr, &error);
    if (app->udp) (void)neverc_udp_set_read_timeout(app->udp, 100);
    neverc_quic_config_t quic_config = neverc_quic_config_default();
    const char *alpn[] = {"neverc-game/1", NULL};
    quic_config.cert_file = argv[1];
    quic_config.key_file = argv[2];
    quic_config.alpn = alpn;
    quic_config.max_streams_bidi = 8U;
    quic_config.max_streams_uni = 0U;
    quic_config.max_data = 4U * 1024U * 1024U;
    quic_config.max_udp_payload_size = 1200U;
    app->quic = neverc_quic_listen(quic_addr, &quic_config, &error);
    if (!app->inputs || !app->executor || !app->control || !app->udp ||
        !app->quic) {
        fprintf(stderr, "startup failed: %s\n",
                error ? error : "resource initialization failed");
        game_app_stop_and_free(app);
        return 1;
    }
    if (neverc_thread_executor_submit(app->executor, game_control_worker,
                                       app) != NEVERC_THREAD_OK ||
        neverc_thread_executor_submit(app->executor, game_udp_worker,
                                       app) != NEVERC_THREAD_OK ||
        neverc_thread_executor_submit(app->executor, game_quic_worker,
                                       app) != NEVERC_THREAD_OK) {
        fprintf(stderr, "failed to start network workers\n");
        game_app_stop_and_free(app);
        return 1;
    }
    printf("authoritative server: tick=%dHz control=%s udp=%s quic=%s\n",
           GAME_TICK_HZ, control_addr, udp_addr, quic_addr);
    while (!game_stop_requested) {
        neverc_time_t started = neverc_time_now();
        game_apply_tick(app);
        neverc_duration_t elapsed = neverc_time_sub(neverc_time_now(),
                                                     started);
        if (elapsed < GAME_TICK_NS)
            neverc_time_sleep(GAME_TICK_NS - elapsed);
    }
    game_app_stop_and_free(app);
    return 0;
}
