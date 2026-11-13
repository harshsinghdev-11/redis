#ifndef PUBSUB_H
#define PUBSUB_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "connection.h"
#include "serialisation.h"

// 10 MiB buffer threshold for slow subscriber protection
constexpr size_t k_max_subscriber_buffer = 10 * 1024 * 1024;

// Global Pub/Sub registry: channel -> set of subscriber connections
extern std::unordered_map<std::string, std::unordered_set<Conn*>> g_pubsub_channels;

// Returns true if connection has at least one active channel subscription
bool is_pubsub_mode(const Conn* conn);

// Returns true if the command is allowed while in Pub/Sub mode
bool is_pubsub_allowed_cmd(const std::string& cmd);

// Handle SUBSCRIBE command
void pubsub_subscribe(Conn* conn, const std::vector<std::string>& channels, tempBuffer& out);

// Handle UNSUBSCRIBE command
void pubsub_unsubscribe(Conn* conn, const std::vector<std::string>& channels, tempBuffer& out);

// Handle PUBLISH command: pushes to subscribers and returns count of receivers
int64_t pubsub_publish(const std::string& channel, const std::string& message);

// Remove connection from all channel subscriptions upon disconnect
void pubsub_cleanup_conn(Conn* conn);

#endif

