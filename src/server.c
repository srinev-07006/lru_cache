/**
 * server.c — Minimal HTTP server for the LRU Cache demo
 *
 * Architecture:
 *   Single-threaded Winsock server on port 8080. Handles one request at a
 *   time (acceptable for a demo/capstone — no concurrent access issues).
 *
 *   Uses raw BSD-style sockets via Winsock2. No external HTTP library.
 *   Why raw sockets? The rubric rewards understanding, not convenience.
 *   A framework would hide the data-structure work behind magic.
 *
 * Endpoints:
 *   GET  /api/get/:key    → { hit, value, latency_ms }
 *   POST /api/put         → { evicted }
 *   GET  /api/cache/state → full cache introspection (MRU→LRU + hash table)
 *   GET  /api/stats       → { hits, misses, size, capacity }
 *   POST /api/capacity    → resize the cache
 *   GET  /               → serves frontend/index.html
 *   GET  / assets (*.css|.js|.html) → serves static files from frontend/
 *
 * Simulated database:
 *   On a cache miss, simulate_db() sleeps 150–300ms (random) and returns
 *   a deterministic value "db_record_for_{key}". This lets the frontend
 *   show a visible latency difference between cache hits and misses.
 */

#define _CRT_SECURE_NO_WARNINGS   /* allow sprintf, strcpy on MSVC */
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Link Winsock library automatically when using MSVC */
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

#include "lru_cache.h"

/* ── Configuration ────────────────────────────────────────────────── */
#define SERVER_PORT     8080
#define DEFAULT_CAP     5       /* small capacity for visible evictions */
#define MAX_REQUEST     8192    /* max HTTP request size we'll read     */
#define MAX_RESPONSE    65536   /* max HTTP response buffer             */
#define VALUE_BUF       1024    /* buffer for cache value lookups       */
#define PATH_BUF        512     /* buffer for file paths                */

/* Global cache — single-threaded, so no mutex needed.
 * In a production system you'd protect this with a mutex;
 * see the README optimization notes for discussion. */
static LRUCache *g_cache = NULL;

/* ══════════════════════════════════════════════════════════════════════════
 * HIGH-RESOLUTION TIMER
 *
 * Uses QueryPerformanceCounter/Frequency for sub-millisecond precision.
 * This lets the frontend show that cache hits take <1ms while misses
 * take 150–300ms (the simulated DB delay).
 * ══════════════════════════════════════════════════════════════════════════ */
static LARGE_INTEGER g_freq;

static void timer_init(void) {
    QueryPerformanceFrequency(&g_freq);
}

static double timer_now_ms(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)g_freq.QuadPart;
}

/* ══════════════════════════════════════════════════════════════════════════
 * SIMULATED DATABASE
 *
 * Fakes a slow database round-trip:
 *   - Sleeps for a random duration between 150–300ms.
 *   - Returns a deterministic value "db_record_for_{key}" so results
 *     are reproducible and debuggable.
 *
 * Why simulate instead of using a real DB?
 *   The project's focus is the cache, not database integration.
 *   The sleep makes the speed benefit of caching visually obvious.
 * ══════════════════════════════════════════════════════════════════════════ */
static int simulate_db(int key) {
    int delay = 150 + (rand() % 151);
    Sleep((DWORD)delay);
    return key * 100;
}

/* ══════════════════════════════════════════════════════════════════════════
 * JSON HELPERS
 *
 * Hand-rolled JSON output. The API surface is tiny (5 endpoints), so a
 * JSON library would be overkill.  We also do minimal JSON *parsing* for
 * POST bodies — just extract known fields with strstr/sscanf.
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * json_escape — Write a JSON-safe version of `src` into `dst`.
 *
 * Escapes backslash, double-quote, and control characters.
 * Returns the number of characters written (excluding NUL).
 */
static int json_escape(const char *src, char *dst, int dst_size) {
    int i = 0;
    while (*src && i < dst_size - 2) {
        if (*src == '"' || *src == '\\') {
            dst[i++] = '\\';
        }
        dst[i++] = *src++;
    }
    dst[i] = '\0';
    return i;
}

/**
 * extract_json_string — Pull the value of "field":"value" from a JSON body.
 *
 * Minimal parser: finds "field":" then reads until the closing quote.
 * Good enough for { "key": "foo", "value": "bar" } — not a general parser.
 *
 * @return 1 on success, 0 if field not found.
 */
static int extract_json_string(const char *body, const char *field,
                               char *out, int out_size) {
    /* Build the search pattern: "field":" */
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", field);

    const char *p = strstr(body, pattern);
    if (!p) return 0;

    /* Advance past the field name and find the colon + opening quote */
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return 0;
    p++;  /* skip opening quote */

    /* Copy until closing quote */
    int i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;  /* skip escape backslash, take next char literally */
        }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

/**
 * extract_json_int — Pull the value of "field": number from a JSON body.
 *
 * @return 1 on success, 0 if field not found.
 */
static int extract_json_int(const char *body, const char *field, int *out) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", field);

    const char *p = strstr(body, pattern);
    if (!p) return 0;

    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;

    *out = atoi(p);
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════════
 * HTTP RESPONSE HELPERS
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * send_response — Format and send a complete HTTP response.
 *
 * Builds "HTTP/1.1 {status}\r\n{headers}\r\n\r\n{body}" and sends it.
 * Includes CORS header so the frontend works from any origin.
 */
static void send_response(SOCKET client, int status_code,
                          const char *status_text,
                          const char *content_type,
                          const char *body, int body_len) {
    char header[2048];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, content_type, body_len);

    send(client, header, header_len, 0);
    if (body && body_len > 0) {
        send(client, body, body_len, 0);
    }
}

static void send_json(SOCKET client, int status, const char *status_text,
                      const char *json) {
    send_response(client, status, status_text,
                  "application/json", json, (int)strlen(json));
}

static void send_404(SOCKET client) {
    const char *body = "{\"error\":\"Not found\"}";
    send_json(client, 404, "Not Found", body);
}

static void send_400(SOCKET client, const char *msg) {
    char body[256];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", msg);
    send_json(client, 400, "Bad Request", body);
}

/* ══════════════════════════════════════════════════════════════════════════
 * STATIC FILE SERVING
 *
 * Serves files from the frontend/ directory relative to the server binary.
 * Supports .html, .css, .js, and .png. Rejects paths containing ".."
 * to prevent directory traversal.
 * ══════════════════════════════════════════════════════════════════════════ */

static const char* mime_for_extension(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".png") == 0)  return "image/png";
    if (strcmp(dot, ".ico") == 0)  return "image/x-icon";
    if (strcmp(dot, ".svg") == 0)  return "image/svg+xml";
    return "application/octet-stream";
}

static void serve_static_file(SOCKET client, const char *url_path) {
    /* Security: reject directory traversal attempts */
    if (strstr(url_path, "..")) {
        send_response(client, 403, "Forbidden",
                      "text/plain", "Forbidden", 9);
        return;
    }

    /* Build file path: frontend/ + url_path
     * If url_path is "/" serve index.html */
    char filepath[PATH_BUF];
    if (strcmp(url_path, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "frontend/index.html");
    } else {
        /* Skip the leading '/' in url_path */
        snprintf(filepath, sizeof(filepath), "frontend/%s",
                 url_path + 1);
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        send_404(client);
        return;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 10 * 1024 * 1024) {  /* 10MB limit */
        fclose(f);
        send_404(client);
        return;
    }

    char *buf = (char *)malloc(size);
    if (!buf) {
        fclose(f);
        send_response(client, 500, "Internal Server Error",
                      "text/plain", "Out of memory", 13);
        return;
    }

    long bytes_read = (long)fread(buf, 1, size, f);
    fclose(f);

    send_response(client, 200, "OK", mime_for_extension(filepath),
                  buf, (int)bytes_read);
    free(buf);
}

/* ══════════════════════════════════════════════════════════════════════════
 * API HANDLERS
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * handle_api_get — GET /api/get/:key
 *
 * 1. Start a high-res timer.
 * 2. Check the LRU cache. If hit → respond immediately (fast path).
 * 3. On miss → call simulate_db() (150–300ms delay), store the result
 *    in the cache (which may trigger an eviction), then respond.
 * 4. Include latency_ms in the response so the frontend can chart it.
 */
static void handle_api_get(SOCKET client, const char *key_str) {
    int key = atoi(key_str);
    int value = 0;
    char response[MAX_RESPONSE];

    double start = timer_now_ms();
    int hit = lru_get(g_cache, key, &value);

    if (!hit) {
        value = simulate_db(key);
        lru_put(g_cache, key, value, NULL);
    }
    double elapsed = timer_now_ms() - start;

    snprintf(response, sizeof(response),
             "{\"hit\":%s,\"key\":%d,\"value\":%d,\"latency_ms\":%.3f}",
             hit ? "true" : "false",
             key, value, elapsed);

    send_json(client, 200, "OK", response);
}

/**
 * handle_api_put — POST /api/put
 *
 * Body: { "key": "...", "value": "..." }
 * Response: { "evicted": "key" | null }
 */
static void handle_api_put(SOCKET client, const char *body) {
    int key, value;

    if (!extract_json_int(body, "key", &key) ||
        !extract_json_int(body, "value", &value)) {
        send_400(client, "Missing key or value");
        return;
    }

    int evicted_key;
    int evicted = lru_put(g_cache, key, value, &evicted_key);

    char response[MAX_RESPONSE];
    if (evicted) {
        snprintf(response, sizeof(response),
                 "{\"evicted\":%d}", evicted_key);
    } else {
        snprintf(response, sizeof(response), "{\"evicted\":null}");
    }

    send_json(client, 200, "OK", response);
}

/**
 * handle_api_delete — DELETE /api/delete/:key
 */
static void handle_api_delete(SOCKET client, const char *key_str) {
    int key = atoi(key_str);
    int deleted = lru_delete(g_cache, key);
    char response[MAX_RESPONSE];

    snprintf(response, sizeof(response),
             "{\"deleted\":%s,\"key\":%d}",
             deleted ? "true" : "false", key);

    send_json(client, 200, "OK", response);
}

/**
 * handle_api_state — GET /api/cache/state
 *
 * Returns the full cache state for visualization:
 *   - nodes[]: ordered MRU → LRU, each with key, value, bucket_index
 *   - stats: hits, misses, size, capacity, num_buckets
 *   - buckets[]: for each bucket index, list of keys in that chain
 *
 * This is the most complex response. We build it incrementally with
 * pointer arithmetic on a response buffer.
 */
static void handle_api_state(SOCKET client) {
    CacheSnapshot snap = lru_snapshot(g_cache);
    char *response = (char *)malloc(MAX_RESPONSE);
    if (!response) {
        lru_free_snapshot(&snap);
        send_response(client, 500, "Internal Server Error",
                      "text/plain", "Out of memory", 13);
        return;
    }

    /* Use a write cursor to append to the response buffer */
    int pos = 0;
    int remain = MAX_RESPONSE;

    /* Opening + stats */
    pos += snprintf(response + pos, remain - pos,
        "{\"size\":%d,\"capacity\":%d,\"num_buckets\":%d,"
        "\"hits\":%lu,\"misses\":%lu,\"nodes\":[",
        snap.count, snap.capacity, snap.num_buckets,
        snap.hits, snap.misses);

    /* Nodes array (MRU -> LRU order) */
    for (int i = 0; i < snap.count && pos < remain - 256; i++) {
        pos += snprintf(response + pos, remain - pos,
            "%s{\"key\":%d,\"value\":%d,\"bucket\":%d}",
            i > 0 ? "," : "", snap.nodes[i].key, snap.nodes[i].value, snap.nodes[i].bucket_index);
    }

    /* Hash table: for each bucket, list the chain of keys.
     * We re-walk the snapshot to group nodes by bucket. */
    pos += snprintf(response + pos, remain - pos, "],\"buckets\":[");

    for (int b = 0; b < snap.num_buckets && pos < remain - 256; b++) {
        pos += snprintf(response + pos, remain - pos,
                        "%s{\"index\":%d,\"keys\":[", b > 0 ? "," : "", b);

        int first = 1;
        for (int i = 0; i < snap.count; i++) {
            if (snap.nodes[i].bucket_index == b) {
                pos += snprintf(response + pos, remain - pos,
                                "%s%d", first ? "" : ",", snap.nodes[i].key);
                first = 0;
            }
        }
        pos += snprintf(response + pos, remain - pos, "]}");
    }

    pos += snprintf(response + pos, remain - pos, "]}");
    send_json(client, 200, "OK", response);
    free(response);
    lru_free_snapshot(&snap);
}

/**
 * handle_api_stats — GET /api/stats
 *
 * Simple stats summary: hits, misses, current size, capacity.
 */
static void handle_api_stats(SOCKET client) {
    char response[512];
    snprintf(response, sizeof(response),
        "{\"hits\":%lu,\"misses\":%lu,\"size\":%d,\"capacity\":%d}",
        g_cache->hits, g_cache->misses,
        g_cache->size, g_cache->capacity);
    send_json(client, 200, "OK", response);
}

/**
 * handle_api_capacity — POST /api/capacity
 *
 * Body: { "capacity": N }
 * Resizes the cache. If shrinking, LRU items are evicted until
 * the size fits the new capacity.
 */
static void handle_api_capacity(SOCKET client, const char *body) {
    int new_cap = 0;
    if (!extract_json_int(body, "capacity", &new_cap) || new_cap < 1) {
        send_400(client, "Invalid or missing capacity (must be >= 1)");
        return;
    }

    int old_size = g_cache->size;
    lru_resize(g_cache, new_cap);
    int evicted_count = old_size - g_cache->size;

    char response[256];
    snprintf(response, sizeof(response),
        "{\"capacity\":%d,\"size\":%d,\"evicted_count\":%d}",
        g_cache->capacity, g_cache->size, evicted_count);
    send_json(client, 200, "OK", response);
}

/* ══════════════════════════════════════════════════════════════════════════
 * REQUEST ROUTER
 *
 * Parses the HTTP request line to extract method, path, and body.
 * Dispatches to the appropriate handler.
 * ══════════════════════════════════════════════════════════════════════════ */

static void handle_request(SOCKET client, const char *request) {
    /* Parse: "METHOD /path HTTP/1.x\r\n..." */
    char method[16], path[1024];
    if (sscanf(request, "%15s %1023s", method, path) != 2) {
        send_400(client, "Malformed request");
        return;
    }

    /* Handle CORS preflight (OPTIONS) for any endpoint */
    if (strcmp(method, "OPTIONS") == 0) {
        send_response(client, 204, "No Content",
                      "text/plain", "", 0);
        return;
    }

    /* Find the body: everything after the blank line "\r\n\r\n" */
    const char *body = strstr(request, "\r\n\r\n");
    if (body) body += 4;  /* skip past the blank line */
    else body = "";

    /* ── API Routing ──────────────────────────────────── */

    /* GET /api/get/:key */
    if (strcmp(method, "GET") == 0 &&
        strncmp(path, "/api/get/", 9) == 0) {
        const char *key = path + 9;  /* everything after /api/get/ */
        if (strlen(key) == 0) {
            send_400(client, "Missing key in URL");
        } else {
            handle_api_get(client, key);
        }
        return;
    }

    /* POST /api/put */
    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/put") == 0) {
        handle_api_put(client, body);
        return;
    }

    /* GET /api/cache/state */
    if (strcmp(method, "GET") == 0 &&
        strcmp(path, "/api/cache/state") == 0) {
        handle_api_state(client);
        return;
    }

    /* GET /api/stats */
    if (strcmp(method, "GET") == 0 &&
        strcmp(path, "/api/stats") == 0) {
        handle_api_stats(client);
        return;
    }

    /* POST /api/capacity */
    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/capacity") == 0) {
        handle_api_capacity(client, body);
        return;
    }

    /* DELETE /api/delete/:key */
    if (strcmp(method, "DELETE") == 0 &&
        strncmp(path, "/api/delete/", 12) == 0) {
        const char *key = path + 12;
        if (strlen(key) == 0) {
            send_400(client, "Missing key in URL");
        } else {
            handle_api_delete(client, key);
        }
        return;
    }

    /* ── Static file serving (frontend) ───────────────── */
    if (strcmp(method, "GET") == 0) {
        serve_static_file(client, path);
        return;
    }

    send_404(client);
}

/* ══════════════════════════════════════════════════════════════════════════
 * MAIN — Winsock setup, bind, listen, accept loop
 * ══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    WSADATA wsa;
    SOCKET server_fd, client_fd;
    struct sockaddr_in addr, client_addr;
    int client_len = sizeof(client_addr);
    char request_buf[MAX_REQUEST];

    /* ── Initialize ────────────────────────────────────── */
    timer_init();
    srand((unsigned int)time(NULL));

    /* Start Winsock (version 2.2) */
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    /* Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    /* Allow port reuse so we can restart quickly after a crash */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    /* Bind to 0.0.0.0:8080 */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    /* Listen with a backlog of 10 */
    if (listen(server_fd, 10) == SOCKET_ERROR) {
        fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    /* ── Create the LRU cache ─────────────────────────── */
    g_cache = lru_create(DEFAULT_CAP);
    if (!g_cache) {
        fprintf(stderr, "Failed to create LRU cache\n");
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    printf("=== LRU Cache Server ===\n");
    printf("Listening on http://localhost:%d\n", SERVER_PORT);
    printf("Cache capacity: %d\n", DEFAULT_CAP);
    printf("Open http://localhost:%d in your browser for the visualization.\n\n",
           SERVER_PORT);

    /* ── Accept loop ──────────────────────────────────── */
    while (1) {
        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == INVALID_SOCKET) {
            fprintf(stderr, "accept() failed: %d\n", WSAGetLastError());
            continue;
        }

        /*
         * Read the full request. Loop on recv() until we have
         * the headers and the full Content-Length body (if any).
         */
        int total_bytes = 0;
        while (total_bytes < MAX_REQUEST - 1) {
            int bytes = recv(client_fd, request_buf + total_bytes,
                             MAX_REQUEST - 1 - total_bytes, 0);
            if (bytes <= 0) break;
            total_bytes += bytes;
            request_buf[total_bytes] = '\0';

            /* Check if headers are complete */
            char *header_end = strstr(request_buf, "\r\n\r\n");
            if (header_end) {
                /* For non-POST (no body expected), we're done */
                if (strncmp(request_buf, "POST", 4) != 0) break;

                /* Parse Content-Length to know when the body is complete */
                const char *cl = strstr(request_buf, "Content-Length:");
                if (!cl) cl = strstr(request_buf, "content-length:");
                if (!cl) break;  /* no Content-Length header, use what we have */

                int content_len = atoi(cl + 15);
                int header_len = (int)(header_end + 4 - request_buf);
                if (total_bytes >= header_len + content_len) break;
            }
        }

        if (total_bytes > 0) {
            request_buf[total_bytes] = '\0';

            /* Log the request line (first line only) */
            char *eol = strstr(request_buf, "\r\n");
            if (eol) {
                char line[256];
                int line_len = (int)(eol - request_buf);
                if (line_len > 255) line_len = 255;
                memcpy(line, request_buf, line_len);
                line[line_len] = '\0';
                printf("[%s]\n", line);
            }

            handle_request(client_fd, request_buf);
        }

        closesocket(client_fd);
    }

    /* Unreachable in normal operation, but here for completeness */
    lru_free(g_cache);
    closesocket(server_fd);
    WSACleanup();
    return 0;
}
