#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <neverc/std/crypto/hmac.h>
#include <neverc/std/crypto/sha256.h>
#include <neverc/std/net/rpc.h>
#include <neverc/std/thread.h>
#include <neverc/std/time.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLLECT_METHOD "anticheat.Telemetry/Collect"
#define TELEMETRY_VERSION 1U
#define TELEMETRY_PREFIX_SIZE 32U
#define TELEMETRY_SIGNATURE_SIZE 32U
#define TELEMETRY_HEADER_SIZE 64U
#define TELEMETRY_MAX_BODY (1024U * 1024U)
#define TELEMETRY_QUEUE_CAPACITY 4096U
#define REPLAY_CAPACITY 16384U
#define REPLAY_WINDOW_MS 30000LL
#define AGENT_ID_CAPACITY 64U

typedef struct {
    atomic_flag flag;
} collector_lock_t;

typedef struct {
    uint8_t nonce[16];
    int64_t expires_ms;
    int occupied;
} replay_entry_t;

typedef struct {
    int64_t timestamp_ms;
    char agent_id[AGENT_ID_CAPACITY + 1U];
    uint8_t nonce[16];
    uint8_t peer_fingerprint[32];
    uint8_t body_digest[32];
    size_t body_length;
} audit_event_t;

typedef struct {
    uint8_t signing_key[32];
    collector_lock_t replay_lock;
    replay_entry_t replay[REPLAY_CAPACITY];
    size_t replay_cursor;
    neverc_thread_channel_t *audit_queue;
    FILE *audit_file;
} collector_app_t;

static void collector_secure_zero(void *memory, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    while (length > 0) {
        *bytes++ = 0;
        length--;
    }
}

static void collector_lock_init(collector_lock_t *lock) {
    atomic_flag_clear_explicit(&lock->flag, memory_order_release);
}

static void collector_lock_acquire(collector_lock_t *lock) {
    while (atomic_flag_test_and_set_explicit(&lock->flag,
                                              memory_order_acquire)) {
    }
}

static void collector_lock_release(collector_lock_t *lock) {
    atomic_flag_clear_explicit(&lock->flag, memory_order_release);
}

static int64_t collector_now_ms(void) {
    return neverc_time_unix_milli(neverc_time_now());
}

static uint32_t collector_load_u32(const uint8_t *input) {
    return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) | (uint32_t)input[3];
}

static uint64_t collector_load_u64(const uint8_t *input) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8U; i++) value = (value << 8) | input[i];
    return value;
}

static int collector_hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int collector_hex_decode(const char *text, uint8_t *output,
                                size_t output_length) {
    if (!text || strlen(text) != output_length * 2U) return -1;
    for (size_t i = 0; i < output_length; i++) {
        int high = collector_hex_value(text[i * 2U]);
        int low = collector_hex_value(text[i * 2U + 1U]);
        if (high < 0 || low < 0) return -1;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return 0;
}

static void collector_hex_encode(const uint8_t *input, size_t input_length,
                                 char *output) {
    static const char alphabet[] = "0123456789abcdef";
    for (size_t i = 0; i < input_length; i++) {
        output[i * 2U] = alphabet[input[i] >> 4];
        output[i * 2U + 1U] = alphabet[input[i] & 15U];
    }
    output[input_length * 2U] = '\0';
}

static int collector_valid_agent_id(const uint8_t *value, size_t length) {
    if (!value || length == 0 || length > AGENT_ID_CAPACITY) return 0;
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = value[i];
        if (!((byte >= 'a' && byte <= 'z') ||
              (byte >= 'A' && byte <= 'Z') ||
              (byte >= '0' && byte <= '9') || byte == '-' || byte == '_'))
            return 0;
    }
    return 1;
}

static const neverc_rpc_metadata_t *collector_agent_metadata(
    neverc_rpc_server_stream_t *stream) {
    size_t count = 0;
    const neverc_rpc_metadata_t *metadata =
        neverc_rpc_server_stream_metadata(stream, &count);
    for (size_t i = 0; i < count; i++) {
        if (metadata[i].key_length == 8U &&
            memcmp(metadata[i].key, "agent-id", 8U) == 0 &&
            collector_valid_agent_id(metadata[i].value,
                                     metadata[i].value_length))
            return &metadata[i];
    }
    return NULL;
}

static neverc_rpc_status_code_t collector_authenticate(
    neverc_rpc_server_stream_t *stream, void *context) {
    (void)context;
    size_t certificate_length = 0;
    const uint8_t *certificate =
        neverc_rpc_server_stream_peer_certificate(stream,
                                                   &certificate_length);
    if (!certificate || certificate_length == 0 ||
        !collector_agent_metadata(stream))
        return NEVERC_RPC_STATUS_UNAUTHENTICATED;
    return NEVERC_RPC_STATUS_OK;
}

static size_t collector_tenant_key(neverc_rpc_server_stream_t *stream,
                                   void *context, void *output,
                                   size_t output_capacity) {
    (void)context;
    const neverc_rpc_metadata_t *agent = collector_agent_metadata(stream);
    if (!agent || agent->value_length > output_capacity) return 0;
    memcpy(output, agent->value, agent->value_length);
    return agent->value_length;
}

static int collector_accept_nonce(collector_app_t *app,
                                  const uint8_t nonce[16],
                                  int64_t timestamp_ms) {
    int64_t now_ms = collector_now_ms();
    if (timestamp_ms < now_ms - REPLAY_WINDOW_MS ||
        timestamp_ms > now_ms + REPLAY_WINDOW_MS)
        return 0;
    collector_lock_acquire(&app->replay_lock);
    for (size_t i = 0; i < REPLAY_CAPACITY; i++) {
        replay_entry_t *entry = &app->replay[i];
        if (entry->occupied && entry->expires_ms > now_ms &&
            neverc_hmac_equal(entry->nonce, nonce, 16U)) {
            collector_lock_release(&app->replay_lock);
            return 0;
        }
    }
    replay_entry_t *entry =
        &app->replay[app->replay_cursor++ % REPLAY_CAPACITY];
    memcpy(entry->nonce, nonce, 16U);
    entry->expires_ms = now_ms + REPLAY_WINDOW_MS;
    entry->occupied = 1;
    collector_lock_release(&app->replay_lock);
    return 1;
}

static int collector_verify_message(collector_app_t *app,
                                    const char *agent_id,
                                    const uint8_t *message,
                                    size_t message_length,
                                    audit_event_t *event) {
    if (message_length < TELEMETRY_HEADER_SIZE ||
        message[0] != TELEMETRY_VERSION || message[1] != 0 ||
        message[2] != 0 || message[3] != 0)
        return -1;
    uint32_t body_length = collector_load_u32(message + 28U);
    if (body_length > TELEMETRY_MAX_BODY ||
        (size_t)body_length != message_length - TELEMETRY_HEADER_SIZE)
        return -1;
    int64_t timestamp_ms = (int64_t)collector_load_u64(message + 4U);
    size_t agent_length = strlen(agent_id);
    size_t signed_length = agent_length + TELEMETRY_PREFIX_SIZE + body_length;
    uint8_t *signed_data = (uint8_t *)malloc(signed_length);
    if (!signed_data) return -1;
    memcpy(signed_data, agent_id, agent_length);
    memcpy(signed_data + agent_length, message, TELEMETRY_PREFIX_SIZE);
    memcpy(signed_data + agent_length + TELEMETRY_PREFIX_SIZE,
           message + TELEMETRY_HEADER_SIZE, body_length);
    uint8_t expected[TELEMETRY_SIGNATURE_SIZE];
    neverc_hmac_sha256(app->signing_key, sizeof(app->signing_key),
                       signed_data, signed_length, expected);
    free(signed_data);
    int valid_mac = neverc_hmac_equal(
        expected, message + TELEMETRY_PREFIX_SIZE, sizeof(expected));
    collector_secure_zero(expected, sizeof(expected));
    if (!valid_mac ||
        !collector_accept_nonce(app, message + 12U, timestamp_ms))
        return -1;
    event->timestamp_ms = timestamp_ms;
    memcpy(event->nonce, message + 12U, sizeof(event->nonce));
    event->body_length = body_length;
    neverc_sha256_sum(message + TELEMETRY_HEADER_SIZE, body_length,
                      event->body_digest);
    return 0;
}

static void collector_stream_handler(neverc_rpc_server_stream_t *stream,
                                     void *context) {
    collector_app_t *app = (collector_app_t *)context;
    const neverc_rpc_metadata_t *agent = collector_agent_metadata(stream);
    if (!agent) {
        (void)neverc_rpc_server_stream_end(
            stream, NEVERC_RPC_STATUS_UNAUTHENTICATED, "missing agent id");
        return;
    }
    char agent_id[AGENT_ID_CAPACITY + 1U];
    memcpy(agent_id, agent->value, agent->value_length);
    agent_id[agent->value_length] = '\0';
    size_t certificate_length = 0;
    const uint8_t *certificate =
        neverc_rpc_server_stream_peer_certificate(stream,
                                                   &certificate_length);
    uint8_t fingerprint[32];
    neverc_sha256_sum(certificate, certificate_length, fingerprint);
    uint8_t *message = (uint8_t *)malloc(TELEMETRY_HEADER_SIZE +
                                         TELEMETRY_MAX_BODY);
    if (!message) {
        (void)neverc_rpc_server_stream_end(
            stream, NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED,
            "telemetry buffer unavailable");
        return;
    }
    for (;;) {
        size_t message_length = 0;
        int received = neverc_rpc_server_stream_recv(
            stream, message, TELEMETRY_HEADER_SIZE + TELEMETRY_MAX_BODY,
            &message_length);
        if (received == NEVERC_RPC_IO_END) break;
        if (received != NEVERC_RPC_IO_OK) {
            free(message);
            return;
        }
        audit_event_t *event = (audit_event_t *)calloc(1, sizeof(*event));
        if (!event || collector_verify_message(app, agent_id, message,
                                                message_length, event) != 0) {
            free(event);
            free(message);
            (void)neverc_rpc_server_stream_end(
                stream, NEVERC_RPC_STATUS_PERMISSION_DENIED,
                "invalid or replayed telemetry");
            return;
        }
        memcpy(event->agent_id, agent_id, strlen(agent_id) + 1U);
        memcpy(event->peer_fingerprint, fingerprint, sizeof(fingerprint));
        if (neverc_thread_channel_try_send(app->audit_queue, event) !=
            NEVERC_THREAD_OK) {
            free(event);
            free(message);
            (void)neverc_rpc_server_stream_end(
                stream, NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED,
                "audit queue is full");
            return;
        }
        char nonce_text[33];
        char response[48];
        collector_hex_encode(message + 12U, 16U, nonce_text);
        int response_length = snprintf(response, sizeof(response),
                                       "ACK %s\n", nonce_text);
        if (response_length <= 0 ||
            (size_t)response_length >= sizeof(response) ||
            neverc_rpc_server_stream_send(stream, response,
                                           (size_t)response_length) !=
                NEVERC_RPC_IO_OK) {
            free(message);
            return;
        }
    }
    free(message);
    (void)neverc_rpc_server_stream_end(stream, NEVERC_RPC_STATUS_OK, "");
}

static void collector_audit_worker(void *context) {
    collector_app_t *app = (collector_app_t *)context;
    for (;;) {
        void *value = NULL;
        int received = neverc_thread_channel_receive(app->audit_queue,
                                                       &value);
        if (received == NEVERC_THREAD_CLOSED) break;
        if (received != NEVERC_THREAD_OK) continue;
        audit_event_t *event = (audit_event_t *)value;
        char nonce[33];
        char fingerprint[65];
        char digest[65];
        collector_hex_encode(event->nonce, sizeof(event->nonce), nonce);
        collector_hex_encode(event->peer_fingerprint,
                             sizeof(event->peer_fingerprint), fingerprint);
        collector_hex_encode(event->body_digest, sizeof(event->body_digest),
                             digest);
        (void)fprintf(app->audit_file,
                      "{\"timestamp_ms\":%lld,\"agent_id\":\"%s\","
                      "\"nonce\":\"%s\",\"peer_sha256\":\"%s\","
                      "\"body_sha256\":\"%s\",\"body_bytes\":%llu}\n",
                      (long long)event->timestamp_ms, event->agent_id, nonce,
                      fingerprint, digest,
                      (unsigned long long)event->body_length);
        (void)fflush(app->audit_file);
        free(event);
    }
}

static void collector_usage(const char *program) {
    fprintf(stderr,
            "usage: %s <server-cert.pem> <server-key.pem> <client-ca.pem> "
            "<64-hex-signing-key> <audit.jsonl> [listen-addr]\n",
            program);
}

static void collector_cleanup(collector_app_t *app,
                              neverc_thread_executor_t *audit_executor,
                              neverc_rpc_server_t *server) {
    if (server) {
        neverc_rpc_server_shutdown(server);
        neverc_rpc_server_free(server);
    }
    if (app && app->audit_queue)
        (void)neverc_thread_channel_close(app->audit_queue);
    if (audit_executor) {
        (void)neverc_thread_executor_shutdown(audit_executor);
        neverc_thread_executor_free(audit_executor);
    }
    if (!app) return;
    if (app->audit_queue) {
        for (;;) {
            void *value = NULL;
            if (neverc_thread_channel_try_receive(app->audit_queue, &value) !=
                NEVERC_THREAD_OK)
                break;
            free(value);
        }
        neverc_thread_channel_free(app->audit_queue);
    }
    if (app->audit_file) (void)fclose(app->audit_file);
    collector_secure_zero(app->signing_key, sizeof(app->signing_key));
    free(app);
}

int main(int argc, char **argv) {
    if (argc < 6 || argc > 7) {
        collector_usage(argv[0]);
        return 2;
    }
    collector_app_t *app = (collector_app_t *)calloc(1, sizeof(*app));
    if (!app || collector_hex_decode(argv[4], app->signing_key,
                                      sizeof(app->signing_key)) != 0) {
        fprintf(stderr, "invalid signing key\n");
        if (app)
            collector_secure_zero(app->signing_key,
                                  sizeof(app->signing_key));
        free(app);
        return 2;
    }
    collector_lock_init(&app->replay_lock);
    app->audit_file = fopen(argv[5], "ab");
    app->audit_queue = neverc_thread_channel_create(
        TELEMETRY_QUEUE_CAPACITY);
    neverc_thread_executor_t *audit_executor =
        neverc_thread_executor_create(1U, 1U);
    neverc_rpc_server_config_t config = neverc_rpc_server_config_default();
    config.max_frame_size = TELEMETRY_HEADER_SIZE + TELEMETRY_MAX_BODY;
    config.max_metadata_size = 4096U;
    config.max_streams_per_connection = 32U;
    config.receive_queue_capacity = 16U;
    config.handler_workers = 16U;
    config.handler_queue_capacity = 256U;
    config.max_connections = 256U;
    neverc_rpc_server_t *server = neverc_rpc_server_new(&config);
    if (!app->audit_file || !app->audit_queue || !audit_executor || !server ||
        neverc_thread_executor_submit(audit_executor, collector_audit_worker,
                                       app) != NEVERC_THREAD_OK ||
        neverc_rpc_server_register(server, COLLECT_METHOD,
                                   collector_stream_handler, app) !=
            NEVERC_RPC_IO_OK ||
        neverc_rpc_server_set_authenticator(server, collector_authenticate,
                                             app) != NEVERC_RPC_IO_OK ||
        neverc_rpc_server_set_tenant_rate_limit(
            server, 1000U, 2000U, 4096U, collector_tenant_key, app) !=
            NEVERC_RPC_IO_OK) {
        fprintf(stderr, "collector initialization failed\n");
        collector_cleanup(app, audit_executor, server);
        return 1;
    }
    const char *listen_addr = argc == 7 ? argv[6] : "0.0.0.0:7443";
    printf("anti-cheat collector: mTLS NRPC %s, audit=%s\n", listen_addr,
           argv[5]);
    int result = neverc_rpc_server_listen_and_serve_mtls(
        server, listen_addr, argv[1], argv[2], argv[3]);
    collector_cleanup(app, audit_executor, server);
    return result == 0 ? 0 : 1;
}
