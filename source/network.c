#include "network.h"
#include "backend.h"
#include "resolver.h"
#include "response_error.h"
#include "sse.h"
#include <cJSON.h>
#include <curl/curl.h>
#include <dswifi9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static CURL *request;
static CURLM *multi;
static struct curl_slist *headers;
static struct curl_slist *resolved_hosts;
static bool resolving;
enum {
    NETWORK_CONNECT_SECONDS = 60,
    NETWORK_PORT_BYTES = 6,
    NETWORK_STATUS_BYTES = 512
};
static char request_host[RESOLVER_HOST_BYTES], request_port[NETWORK_PORT_BYTES];
static char request_address[RESOLVER_ADDRESS_BYTES];
static bool initialized;
static bool connecting;
typedef enum {
    NETWORK_IDLE,
    NETWORK_WIFI,
    NETWORK_DNS,
    NETWORK_CONNECT,
    NETWORK_TLS,
    NETWORK_WAIT,
    NETWORK_STREAM
} NetworkPhase;
static NetworkPhase phase;
static time_t started, wifi_deadline, retry_at, last_data, idle_retry;
static unsigned wifi_attempt;
static unsigned backend_index;
static char session_header[160];
static char key_directory[192] = "/lutin";
static char *body_copy;
static SseParser parser;
static char error_body[4096];
static size_t received;
static bool local_error;
static char status[NETWORK_STATUS_BYTES] = "Network idle";
static char token_path[256];
static char request_url[256];
static char ca_path[256] = "/lutin/ca.pem";
static char curl_error[CURL_ERROR_SIZE];
static bool downloading_catalog, catalog_updated;
static char *catalog_data;
static size_t catalog_size;

bool network_catalog_updated(void) {
    bool updated = catalog_updated;
    catalog_updated = false;
    return updated;
}

static bool add_header(const char *value) {
    struct curl_slist *updated = curl_slist_append(headers, value);
    if (!updated)
        return false;
    headers = updated;
    return true;
}

void network_set_directory(const char *directory) {
    snprintf(key_directory, sizeof(key_directory), "%s", directory);
    snprintf(ca_path, sizeof(ca_path), "%s/ca.pem", directory);
}

void network_set_backend(unsigned index, const char *session_id) {
    backend_index = index < BACKEND_COUNT ? index : 0;
    if (!BACKEND_COUNT)
        return;
    const Provider *provider = backends[backend_index].provider;
    session_header[0] = 0;
    if (provider->session_header)
        snprintf(session_header, sizeof(session_header), "%s: %.120s",
                 provider->session_header, session_id);
    for (char *p = session_header; *p; p++)
        if (*p == '\r' || *p == '\n')
            *p = '_';
}

const char *network_wifi(void) {
    if (!initialized)
        return "OFF";
    int state = Wifi_AssocStatus();
    if (state == ASSOCSTATUS_ASSOCIATED)
        return "ON";
    return connecting || (state != ASSOCSTATUS_CANNOTCONNECT &&
                          state != ASSOCSTATUS_DISCONNECTED)
               ? "JOIN"
               : "DOWN";
}

static size_t receive(char *data, size_t size, size_t count, void *context) {
    (void)context;
    size_t length = size * count;
    if (length > 8 * 1024 * 1024 - received) {
        snprintf(status, sizeof(status), "SSE transport exceeds 8 MB");
        local_error = true;
        return 0;
    }
    received += length;
    last_data = time(NULL);
    long http = 0;
    curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &http);
    if (http >= 400) {
        size_t used = strlen(error_body);
        size_t available = sizeof(error_body) - used - 1;
        size_t copy = length < available ? length : available;
        memcpy(error_body + used, data, copy);
        error_body[used + copy] = 0;
        return length;
    }
    if (downloading_catalog) {
        if (length >= CATALOG_MAX_BYTES - catalog_size) {
            snprintf(status, sizeof(status),
                     "Catalog download exceeds byte limit");
            local_error = true;
            return 0;
        }
        memcpy(catalog_data + catalog_size, data, length);
        catalog_size += length;
        catalog_data[catalog_size] = 0;
        return length;
    }
    if (!sse_feed(&parser, data, length) || protocol_failed()) {
        snprintf(status, sizeof(status), "%s",
                 protocol_output_limited() ? "Model output limit reached"
                                           : "Invalid or oversized SSE event");
        local_error = true;
        return 0;
    }
    return length;
}

void network_stop(void) {
    connecting = false;
    resolving = false;
    phase = NETWORK_IDLE;
    if (multi && request)
        curl_multi_remove_handle(multi, request);
    if (request)
        curl_easy_cleanup(request);
    if (multi)
        curl_multi_cleanup(multi);
    curl_slist_free_all(headers);
    curl_slist_free_all(resolved_hosts);
    resolved_hosts = NULL;
    free(body_copy);
    body_copy = NULL;
    free(catalog_data);
    catalog_data = NULL;
    downloading_catalog = false;
    headers = NULL;
    request = NULL;
    multi = NULL;
    sse_init(&parser, NULL);
}

static bool begin_connection(void) {
    if (!initialized) {
        initialized = Wifi_InitDefault(INIT_ONLY | WIFI_ATTEMPT_DSI_MODE);
        if (initialized)
            curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    if (!initialized) {
        snprintf(status, sizeof(status), "Network initialization failed");
        network_stop();
        return false;
    }
    error_body[0] = 0;
    request_address[0] = 0;
    received = 0;
    local_error = false;
    started = last_data = time(NULL);
    wifi_attempt = 1;
    retry_at = 0;
    wifi_deadline = started + 30;
    connecting = true;
    phase = NETWORK_WIFI;
    if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) {
        Wifi_AutoConnect();
        snprintf(status, sizeof(status), "Connecting to Wi-Fi...");
    } else
        snprintf(status, sizeof(status), "Wi-Fi connected; preparing HTTPS");
    return true;
}

bool network_update_catalog(void) {
    if (network_busy())
        return false;
    network_stop();
    catalog_updated = false;
    catalog_data = malloc(CATALOG_MAX_BYTES);
    if (!catalog_data) {
        snprintf(status, sizeof(status), "Not enough memory for catalog");
        return false;
    }
    catalog_size = 0;
    downloading_catalog = true;
    snprintf(request_url, sizeof(request_url), "%s",
             "https://raw.githubusercontent.com/sachahjkl/lutin/main/catalog/"
             "models.json");
    return begin_connection();
}

bool network_start(const char *body, NetworkEvent event) {
    if (downloading_catalog)
        return false;
    network_stop();
    if (!BACKEND_COUNT || backend_index >= BACKEND_COUNT) {
        snprintf(status, sizeof(status), "Load a valid model catalog first");
        return false;
    }
    const Backend *backend = &backends[backend_index];
    const Provider *provider = backend->provider;
    snprintf(token_path, sizeof(token_path), "%s/keys/%s", key_directory,
             provider->id);
    snprintf(request_url, sizeof(request_url), "%s/%s", provider->base_url,
             backend->protocol == API_RESPONSES ? "responses"
                                                : "chat/completions");
    char token[512];
    FILE *file = fopen(token_path, "rb");
    if (!file) {
        snprintf(status, sizeof(status), "Missing key: %.140s", token_path);
        return false;
    }
    size_t length = fread(token, 1, sizeof(token) - 1, file);
    bool oversized = fgetc(file) != EOF || ferror(file);
    fclose(file);
    token[length] = 0;
    while (length && (token[length - 1] == '\n' || token[length - 1] == '\r'))
        token[--length] = 0;
    if (oversized || !length || strpbrk(token, "\r\n")) {
        memset(token, 0, sizeof(token));
        snprintf(status, sizeof(status), "Invalid token format");
        return false;
    }
    char authorization[560];
    snprintf(authorization, sizeof(authorization), "%s %s",
             provider->auth_header, token);
    memset(token, 0, sizeof(token));
    bool headers_ok = add_header(authorization);
    memset(authorization, 0, sizeof(authorization));
    headers_ok = headers_ok && add_header("Content-Type: application/json") &&
                 add_header("Accept: text/event-stream") &&
                 add_header("Expect:");
    if (session_header[0])
        headers_ok = headers_ok && add_header(session_header);
    body_copy = protocol_request(body, backend_protocol(backend_index));
    if (!body_copy || !headers_ok) {
        snprintf(status, sizeof(status), "Network initialization failed");
        network_stop();
        return false;
    }
    protocol_start(backend_protocol(backend_index), event);
    sse_init(&parser, protocol_event);
    return begin_connection();
}

void network_tick(void) {
    time_t now = time(NULL);
    if (!network_busy()) {
        if (initialized && Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED &&
            now >= idle_retry) {
            Wifi_AutoConnect();
            idle_retry = now + 30;
        }
        return;
    }
    if (connecting) {
        if (retry_at && now < retry_at) {
            snprintf(status, sizeof(status), "Wi-Fi retry %u/3 in %lds",
                     wifi_attempt, (long)(retry_at - now));
            return;
        }
        if (retry_at) {
            retry_at = 0;
            Wifi_AutoConnect();
            wifi_deadline = now + 30;
        }
        int association = Wifi_AssocStatus();
        if (association == ASSOCSTATUS_CANNOTCONNECT || now >= wifi_deadline) {
            if (wifi_attempt < 3) {
                wifi_attempt++;
                retry_at = now + 5;
                return;
            }
            snprintf(status, sizeof(status), "Wi-Fi failed after 3 attempts");
            network_stop();
            return;
        }
        if (association != ASSOCSTATUS_ASSOCIATED) {
            snprintf(status, sizeof(status), "Wi-Fi %u/3 %lds", wifi_attempt,
                     (long)(now - started));
            return;
        }
        connecting = false;
        request = curl_easy_init();
        multi = curl_multi_init();
        if (!request || !multi) {
            snprintf(status, sizeof(status), "Not enough memory for HTTPS");
            network_stop();
            return;
        }
        curl_easy_setopt(request, CURLOPT_URL, request_url);
        /* All hostname lookup runs in the resolver worker, including on DS. */
        curl_easy_setopt(request, CURLOPT_PROXY, "");
        curl_error[0] = 0;
        curl_easy_setopt(request, CURLOPT_ERRORBUFFER, curl_error);
        curl_easy_setopt(request, CURLOPT_HTTPHEADER, headers);
        if (downloading_catalog)
            curl_easy_setopt(request, CURLOPT_HTTPGET, 1L);
        else
            curl_easy_setopt(request, CURLOPT_POSTFIELDS, body_copy);
        curl_easy_setopt(request, CURLOPT_CAINFO, ca_path);
        curl_easy_setopt(request, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(request, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(request, CURLOPT_USERAGENT, "Lutin/0.5");
        curl_easy_setopt(request, CURLOPT_CONNECTTIMEOUT,
                         (long)NETWORK_CONNECT_SECONDS);
        curl_easy_setopt(request, CURLOPT_TIMEOUT, 600L);
        curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L);
        started = last_data = now;
        phase = NETWORK_DNS;
        CURLU *url = curl_url();
        char *host = NULL, *port = NULL;
        bool valid =
            url &&
            curl_url_set(url, CURLUPART_URL, request_url, 0) == CURLUE_OK &&
            curl_url_get(url, CURLUPART_HOST, &host, 0) == CURLUE_OK &&
            curl_url_get(url, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT) ==
                CURLUE_OK &&
            strlen(host) < sizeof(request_host) &&
            strlen(port) < sizeof(request_port);
        if (valid) {
            snprintf(request_host, sizeof(request_host), "%s", host);
            snprintf(request_port, sizeof(request_port), "%s", port);
        }
        curl_free(host);
        curl_free(port);
        curl_url_cleanup(url);
        bool ready = valid && resolver_poll(NULL);
        if (!ready || !resolver_start(request_host)) {
            snprintf(status, sizeof(status), "%s",
                     !valid ? "Invalid request URL"
                     : !ready
                         ? "DNS worker unavailable; retry after lookup finishes"
                         : "Cannot start DNS worker");
            network_stop();
            return;
        }
        resolving = true;
        snprintf(status, sizeof(status), "DNS lookup: %.200s", request_host);
        return;
    }
    if (!multi)
        return;
    if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) {
        snprintf(status, sizeof(status), "Wi-Fi lost; request stopped");
        network_stop();
        idle_retry = now;
        return;
    }
    if (resolving) {
        ResolverResult result;
        if (now - started >= NETWORK_CONNECT_SECONDS) {
            snprintf(status, sizeof(status), "DNS timeout after %ds",
                     NETWORK_CONNECT_SECONDS);
            network_stop();
            return;
        }
        if (!resolver_poll(&result)) {
            snprintf(status, sizeof(status), "DNS %lds: %.200s",
                     (long)(now - started), request_host);
            return;
        }
        if (result.error) {
            snprintf(status, sizeof(status), "DNS lookup failed (%d): %.200s",
                     result.error, request_host);
            network_stop();
            return;
        }
        /* Host, port, address, two separators and optional IPv6 brackets. */
        char entry[RESOLVER_HOST_BYTES + NETWORK_PORT_BYTES +
                   RESOLVER_ADDRESS_BYTES + sizeof("::[]")];
        bool ipv6 = strchr(result.address, ':') != NULL;
        snprintf(request_address, sizeof(request_address), "%s",
                 result.address);
        snprintf(entry, sizeof(entry), "%s:%s:%s%s%s", request_host,
                 request_port, ipv6 ? "[" : "", result.address,
                 ipv6 ? "]" : "");
        resolved_hosts = curl_slist_append(NULL, entry);
        if (!resolved_hosts ||
            curl_easy_setopt(request, CURLOPT_RESOLVE, resolved_hosts) !=
                CURLE_OK ||
            curl_multi_add_handle(multi, request) != CURLM_OK) {
            snprintf(status, sizeof(status),
                     "Cannot prepare resolved connection");
            network_stop();
            return;
        }
        resolving = false;
        phase = NETWORK_CONNECT;
        snprintf(status, sizeof(status), "Connecting to server: %.200s",
                 request_host);
        return;
    }
    if (now - last_data >= 180) {
        snprintf(status, sizeof(status), "No server data for 180s; stopped");
        network_stop();
        return;
    }
    int running;
    CURLMcode result = curl_multi_perform(multi, &running);
    if (result != CURLM_OK) {
        snprintf(status, sizeof(status), "%s", curl_multi_strerror(result));
        network_stop();
        return;
    }
    double connected = 0, secured = 0;
    curl_easy_getinfo(request, CURLINFO_CONNECT_TIME, &connected);
    curl_easy_getinfo(request, CURLINFO_APPCONNECT_TIME, &secured);
    phase = received        ? NETWORK_STREAM
            : secured > 0   ? NETWORK_WAIT
            : connected > 0 ? NETWORK_TLS
                            : NETWORK_CONNECT;
    const char *label = phase == NETWORK_STREAM    ? "Stream"
                        : phase == NETWORK_WAIT    ? "Waiting"
                        : phase == NETWORK_TLS     ? "TLS"
                        : phase == NETWORK_CONNECT ? "TCP"
                                                   : "DNS";
    if (!local_error)
        snprintf(status, sizeof(status), "%s %lds %uB", label,
                 (long)(now - started), (unsigned)received);
    int pending;
    CURLMsg *message;
    while ((message = curl_multi_info_read(multi, &pending))) {
        if (message->msg != CURLMSG_DONE)
            continue;
        long http = 0;
        curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &http);
        if (local_error) {
            network_stop();
            break;
        }
        if (message->data.result == CURLE_OPERATION_TIMEDOUT)
            snprintf(status, sizeof(status), "%s timeout after %lds (%uB)",
                     label, (long)(now - started), (unsigned)received);
        else if (message->data.result != CURLE_OK) {
            long socket_error = 0;
            curl_easy_getinfo(request, CURLINFO_OS_ERRNO, &socket_error);
            snprintf(
                status, sizeof(status), "%s: curl %d, OS %ld (%s)\nIP: %s\n%s",
                label, (int)message->data.result, socket_error,
                socket_error ? strerror((int)socket_error) : "not reported",
                request_address[0] ? request_address : "not resolved",
                curl_error[0] ? curl_error
                              : curl_easy_strerror(message->data.result));
        } else if (http >= 400) {
            response_error(status, sizeof(status), http, error_body);
        } else if (downloading_catalog) {
            catalog_updated =
                http == 200 &&
                catalog_install(key_directory, catalog_data, catalog_size);
            snprintf(status, sizeof(status), "%s",
                     catalog_updated
                         ? "Model catalog updated"
                         : (http != 200 ? "Catalog download requires HTTP 200"
                                        : catalog_error()));
        } else
            snprintf(status, sizeof(status), "HTTP %ld", http);
        network_stop();
        break;
    }
}

bool network_busy(void) { return connecting || request; }
bool network_output_limited(void) { return protocol_output_limited(); }
const char *network_status(void) { return status; }
