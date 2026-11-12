#include "pubsub.h"
#include "buffer.h"
#include "network.h"
#include <algorithm>
#include <iostream>

std::unordered_map<std::string, std::unordered_set<Conn*>> g_pubsub_channels;

bool is_pubsub_mode(const Conn* conn) {
    return conn != nullptr && !conn->channels.empty();
}

bool is_pubsub_allowed_cmd(const std::string& cmd) {
    return cmd == "subscribe" || cmd == "unsubscribe" ||
           cmd == "psubscribe" || cmd == "punsubscribe" ||
           cmd == "ping" || cmd == "quit";
}

void pubsub_subscribe(Conn* conn, const std::vector<std::string>& channels, tempBuffer& /*out*/) {
    if (!conn) return;

    for (const std::string& ch : channels) {
        conn->channels.insert(ch);
        g_pubsub_channels[ch].insert(conn);

        tempBuffer body;
        out_subscribe_confirm(body, ch, static_cast<int64_t>(conn->channels.size()));
        make_response(body, conn->outgoing);
    }

    conn->want_write = true;
    conn->want_read = true;
}

void pubsub_unsubscribe(Conn* conn, const std::vector<std::string>& channels, tempBuffer& /*out*/) {
    if (!conn) return;

    if (channels.empty()) {
        // Unsubscribe from all channels
        std::vector<std::string> to_unsub(conn->channels.begin(), conn->channels.end());
        if (to_unsub.empty()) {
            tempBuffer body;
            out_arr(body, 3);
            out_str(body, "unsubscribe");
            out_nil(body);
            out_int(body, 0);
            make_response(body, conn->outgoing);
        } else {
            for (const std::string& ch : to_unsub) {
                auto it = g_pubsub_channels.find(ch);
                if (it != g_pubsub_channels.end()) {
                    it->second.erase(conn);
                    if (it->second.empty()) {
                        g_pubsub_channels.erase(it);
                    }
                }
                conn->channels.erase(ch);

                tempBuffer body;
                out_unsubscribe_confirm(body, ch, static_cast<int64_t>(conn->channels.size()));
                make_response(body, conn->outgoing);
            }
        }
    } else {
        for (const std::string& ch : channels) {
            auto it = g_pubsub_channels.find(ch);
            if (it != g_pubsub_channels.end()) {
                it->second.erase(conn);
                if (it->second.empty()) {
                    g_pubsub_channels.erase(it);
                }
            }
            conn->channels.erase(ch);

            tempBuffer body;
            out_unsubscribe_confirm(body, ch, static_cast<int64_t>(conn->channels.size()));
            make_response(body, conn->outgoing);
        }
    }

    if (!conn->outgoing.empty()) {
        conn->want_write = true;
    }
    conn->want_read = true;
}

int64_t pubsub_publish(const std::string& channel, const std::string& message) {
    auto it = g_pubsub_channels.find(channel);
    if (it == g_pubsub_channels.end() || it->second.empty()) {
        return 0;
    }

    // Pre-encode framed push message once
    tempBuffer push_body;
    out_pubsub_msg(push_body, channel, message);
    tempBuffer framed_push;
    make_response(push_body, framed_push);

    int64_t receivers = 0;
    for (Conn* sub_conn : it->second) {
        if (!sub_conn) continue;

        // Slow subscriber buffer check
        if (sub_conn->outgoing.size() + framed_push.size() > k_max_subscriber_buffer) {
            std::cerr << "Slow subscriber on fd " << sub_conn->fd << " exceeded buffer limit. Closing.\n";
            sub_conn->want_close = true;
            continue;
        }

        buf_append(sub_conn->outgoing, framed_push.data(), framed_push.size());
        sub_conn->want_write = true;
        sub_conn->want_read = true;
        receivers++;
    }

    return receivers;
}

void pubsub_cleanup_conn(Conn* conn) {
    if (!conn) return;

    for (const std::string& ch : conn->channels) {
        auto it = g_pubsub_channels.find(ch);
        if (it != g_pubsub_channels.end()) {
            it->second.erase(conn);
            if (it->second.empty()) {
                g_pubsub_channels.erase(it);
            }
        }
    }
    conn->channels.clear();
}

