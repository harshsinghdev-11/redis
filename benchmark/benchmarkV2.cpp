#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <cerrno>

using namespace std;

static constexpr const char *SERVER_IP = "127.0.0.1";
static constexpr uint16_t SERVER_PORT = 1234;

// Number of requests allowed to be outstanding
// per client connection.
static constexpr size_t PIPELINE_DEPTH = 32;

// ============================================================
// Timing
// ============================================================

using Clock = chrono::steady_clock;

// ============================================================
// Request / Response structures
// ============================================================

struct RequestState
{
    vector<uint8_t> data;

    size_t sent = 0;

    Clock::time_point start_time;
};

struct Client
{
    int fd = -1;

    size_t requests_sent = 0;
    size_t requests_completed = 0;

    size_t next_request = 0;

    vector<RequestState> outstanding;

    // Response parsing
    vector<uint8_t> response_buffer;

    uint32_t response_length = 0;

    bool waiting_for_length = true;

    bool waiting_for_body = false;

    size_t response_expected = 0;

    size_t response_received = 0;
};

// ============================================================
// Serialization
// ============================================================

void append_u32(
    vector<uint8_t> &buffer,
    uint32_t value)
{
    const uint8_t *bytes =
        reinterpret_cast<const uint8_t *>(&value);

    buffer.insert(
        buffer.end(),
        bytes,
        bytes + sizeof(value));
}

void append_string(
    vector<uint8_t> &buffer,
    const string &str)
{
    uint32_t len =
        static_cast<uint32_t>(str.size());

    append_u32(buffer, len);

    buffer.insert(
        buffer.end(),
        str.begin(),
        str.end());
}

// Your protocol:
//
// [body size]
// [number of strings]
// [string length][string]
// [string length][string]
// ...

vector<uint8_t> make_request(
    const vector<string> &command)
{
    vector<uint8_t> body;

    uint32_t number_of_strings =
        static_cast<uint32_t>(command.size());

    append_u32(
        body,
        number_of_strings);

    for (const string &str : command)
    {
        append_string(body, str);
    }

    vector<uint8_t> request;

    uint32_t body_size =
        static_cast<uint32_t>(body.size());

    append_u32(
        request,
        body_size);

    request.insert(
        request.end(),
        body.begin(),
        body.end());

    return request;
}

// ============================================================
// Socket
// ============================================================

int create_socket()
{

    int fd = socket(
        AF_INET,
        SOCK_STREAM,
        0);

    if (fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return fd;
}

void set_nonblocking(int fd)
{

    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
    {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }

    if (fcntl(
            fd,
            F_SETFL,
            flags | O_NONBLOCK) < 0)
    {

        perror("fcntl");
        exit(EXIT_FAILURE);
    }
}

void connect_to_server(int fd)
{

    sockaddr_in addr{};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);

    inet_pton(
        AF_INET,
        SERVER_IP,
        &addr.sin_addr);

    // We initially connect using blocking connect.
    if (connect(
            fd,
            reinterpret_cast<sockaddr *>(&addr),
            sizeof(addr)) < 0)
    {

        perror("connect");
        close(fd);
        exit(EXIT_FAILURE);
    }

    set_nonblocking(fd);
}

// ============================================================
// Create command
// ============================================================

vector<string> make_command(
    const string &operation,
    size_t request_id)
{
    string key =
        "bench_key_" +
        to_string(request_id);

    if (operation == "SET")
    {

        string value =
            "bench_value_" +
            to_string(request_id);

        return {
            "set",
            key,
            value};
    }

    if (operation == "GET")
    {

        return {
            "get",
            key};
    }

    if (operation == "ERASE")
    {

        return {
            "erase",
            key};
    }

    cerr << "Unknown operation\n";
    exit(EXIT_FAILURE);
}

// ============================================================
// Response parser
// ============================================================
//
// Server:
//
// [4 bytes response length]
// [4 bytes status]
// [data]
// ============================================================

bool process_response(
    Client &client,
    vector<double> &latencies)
{
    while (true)
    {

        // Need response length first.
        if (client.waiting_for_length)
        {

            if (client.response_buffer.size() < 4)
            {
                return true;
            }

            memcpy(
                &client.response_length,
                client.response_buffer.data(),
                4);

            if (client.response_length < 4)
            {
                cerr << "Invalid response length\n";
                return false;
            }

            client.response_expected =
                4 + client.response_length;

            client.waiting_for_length = false;
            client.waiting_for_body = true;
        }

        // Need complete response.
        if (client.response_buffer.size() <
            client.response_expected)
        {

            return true;
        }

        // We received one complete response.

        if (client.outstanding.empty())
        {
            cerr << "No matching outstanding request\n";
            return false;
        }

        auto request_start =
            client.outstanding.front().start_time;

        auto now = Clock::now();

        double latency_us =
            chrono::duration<double, micro>(
                now - request_start)
                .count();

        latencies.push_back(
            latency_us);

        client.outstanding.erase(
            client.outstanding.begin());

        client.requests_completed++;

        // Remove consumed response.
        client.response_buffer.erase(
            client.response_buffer.begin(),
            client.response_buffer.begin() + client.response_expected);

        // Reset parser.
        client.response_length = 0;
        client.response_expected = 0;

        client.waiting_for_length = true;
        client.waiting_for_body = false;

        // Continue in case multiple responses
        // arrived in a single recv().
    }
}

// ============================================================
// Send pending request
// ============================================================

bool send_request(
    Client &client,
    RequestState &request)
{
    while (request.sent < request.data.size())
    {

        ssize_t rv = send(
            client.fd,
            request.data.data() + request.sent,
            request.data.size() - request.sent,
            0);

        if (rv > 0)
        {

            request.sent +=
                static_cast<size_t>(rv);

            continue;
        }

        if (rv < 0 &&
            (errno == EAGAIN ||
             errno == EWOULDBLOCK))
        {

            return true;
        }

        if (rv < 0)
        {
            perror("send");
            return false;
        }

        return false;
    }

    return true;
}

// ============================================================
// Fill pipeline
// ============================================================

bool fill_pipeline(
    Client &client,
    const string &operation,
    size_t requests_per_client,
    size_t global_client_id,
    vector<double> &latencies)
{

    while (
        client.outstanding.size() < PIPELINE_DEPTH &&
        client.requests_sent < requests_per_client)
    {

        size_t request_id =
            global_client_id *
                requests_per_client +
            client.requests_sent;

        vector<string> command =
            make_command(
                operation,
                request_id);

        RequestState request;

        request.data =
            make_request(command);

        request.sent = 0;

        request.start_time =
            Clock::now();

        client.outstanding.push_back(
            std::move(request));

        RequestState &current =
            client.outstanding.back();

        if (!send_request(
                client,
                current))
        {

            return false;
        }

        // If the complete request was sent,
        // count it.
        if (current.sent ==
            current.data.size())
        {

            client.requests_sent++;
        }
        else
        {
            // Partial send.
            //
            // For this benchmark implementation
            // we stop filling the pipeline here.
            return true;
        }
    }

    return true;
}

// ============================================================
// Receive data
// ============================================================

bool receive_data(
    Client &client,
    vector<double> &latencies)
{

    uint8_t buffer[64 * 1024];

    while (true)
    {

        ssize_t rv = recv(
            client.fd,
            buffer,
            sizeof(buffer),
            0);

        if (rv > 0)
        {

            client.response_buffer.insert(
                client.response_buffer.end(),
                buffer,
                buffer + rv);

            continue;
        }

        if (rv == 0)
        {
            return false;
        }

        if (errno == EAGAIN ||
            errno == EWOULDBLOCK)
        {

            break;
        }

        perror("recv");
        return false;
    }

    return process_response(
        client,
        latencies);
}

// ============================================================
// Result
// ============================================================

struct Result
{

    double total_ms = 0;

    double throughput = 0;

    double average_us = 0;

    double p50_us = 0;

    double p95_us = 0;

    double p99_us = 0;

    size_t completed = 0;
};

// ============================================================
// Percentile
// ============================================================

double calculate_percentile(
    vector<double> values,
    double percentile)
{
    if (values.empty())
    {
        return 0;
    }

    sort(
        values.begin(),
        values.end());

    size_t index =
        static_cast<size_t>(
            percentile *
            (values.size() - 1));

    return values[index];
}

// ============================================================
// Benchmark
// ============================================================

Result benchmark(
    size_t client_count,
    size_t requests_per_client,
    const string &operation)
{

    vector<Client> clients(
        client_count);

    // --------------------------------------------------------
    // Create connections
    // --------------------------------------------------------

    for (size_t i = 0;
         i < client_count;
         i++)
    {

        clients[i].fd =
            create_socket();

        connect_to_server(
            clients[i].fd);
    }

    // --------------------------------------------------------
    // Initial pipeline
    // --------------------------------------------------------

    vector<double> latencies;

    latencies.reserve(
        client_count *
        requests_per_client);

    for (size_t i = 0;
         i < client_count;
         i++)
    {

        if (!fill_pipeline(
                clients[i],
                operation,
                requests_per_client,
                i,
                latencies))
        {

            cerr << "Failed to fill pipeline\n";

            exit(EXIT_FAILURE);
        }
    }

    // --------------------------------------------------------
    // Event loop
    // --------------------------------------------------------

    auto total_start =
        Clock::now();

    size_t total_completed = 0;

    while (
        total_completed <
        client_count *
            requests_per_client)
    {

        vector<pollfd> poll_fds(
            client_count);

        for (size_t i = 0;
             i < client_count;
             i++)
        {

            poll_fds[i].fd =
                clients[i].fd;

            poll_fds[i].events =
                POLLIN;

            // If there are partially-sent requests,
            // monitor POLLOUT.
            for (
                const auto &request :
                clients[i].outstanding)
            {

                if (
                    request.sent <
                    request.data.size())
                {

                    poll_fds[i].events |=
                        POLLOUT;

                    break;
                }
            }
        }

        int rv = poll(
            poll_fds.data(),
            poll_fds.size(),
            -1);

        if (rv < 0)
        {

            if (errno == EINTR)
            {
                continue;
            }

            perror("poll");
            exit(EXIT_FAILURE);
        }

        // ----------------------------------------------------
        // Handle clients
        // ----------------------------------------------------

        for (size_t i = 0;
             i < client_count;
             i++)
        {

            Client &client =
                clients[i];

            short events =
                poll_fds[i].revents;

            if (events & POLLOUT)
            {

                for (
                    auto &request :
                    client.outstanding)
                {

                    if (
                        request.sent <
                        request.data.size())
                    {

                        if (!send_request(
                                client,
                                request))
                        {

                            cerr << "send failed\n";
                            exit(EXIT_FAILURE);
                        }

                        break;
                    }
                }
            }

            if (events & POLLIN)
            {

                size_t before =
                    client.requests_completed;

                if (!receive_data(
                        client,
                        latencies))
                {

                    cerr << "receive failed\n";
                    exit(EXIT_FAILURE);
                }

                size_t after =
                    client.requests_completed;

                total_completed +=
                    after - before;
            }

            if (
                events &
                (POLLERR |
                 POLLHUP |
                 POLLNVAL))
            {

                cerr << "Socket error\n";
                exit(EXIT_FAILURE);
            }

            // ------------------------------------------------
            // Fill pipeline again
            // ------------------------------------------------

            if (
                client.requests_sent <
                requests_per_client)
            {

                if (!fill_pipeline(
                        client,
                        operation,
                        requests_per_client,
                        i,
                        latencies))
                {

                    cerr << "Failed to fill pipeline\n";
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    auto total_end =
        Clock::now();

    // --------------------------------------------------------
    // Close clients
    // --------------------------------------------------------

    for (Client &client : clients)
    {

        close(client.fd);
    }

    // --------------------------------------------------------
    // Calculate results
    // --------------------------------------------------------

    double total_seconds =
        chrono::duration<double>(
            total_end -
            total_start)
            .count();

    Result result;

    result.completed =
        total_completed;

    result.total_ms =
        total_seconds * 1000.0;

    result.throughput =
        total_completed /
        total_seconds;

    if (!latencies.empty())
    {

        double sum = 0;

        for (double latency :
             latencies)
        {

            sum += latency;
        }

        result.average_us =
            sum /
            latencies.size();

        result.p50_us =
            calculate_percentile(
                latencies,
                0.50);

        result.p95_us =
            calculate_percentile(
                latencies,
                0.95);

        result.p99_us =
            calculate_percentile(
                latencies,
                0.99);
    }

    return result;
}

// ============================================================
// Print
// ============================================================

void print_result(
    size_t clients,
    size_t requests_per_client,
    const string &operation,
    const Result &result)
{

    cout << "\n";
    cout << "============================================\n";
    cout << "         Redis C++ Benchmark V2\n";
    cout << "============================================\n";

    cout << "Operation            : "
         << operation << "\n";

    cout << "Clients              : "
         << clients << "\n";

    cout << "Requests/client      : "
         << requests_per_client << "\n";

    cout << "Total requests       : "
         << clients *
                requests_per_client
         << "\n";

    cout << "Completed            : "
         << result.completed
         << "\n";

    cout << "Total time           : "
         << result.total_ms
         << " ms\n";

    cout << "Throughput           : "
         << result.throughput
         << " req/sec\n";

    cout << "Average latency      : "
         << result.average_us
         << " us\n";

    cout << "P50 latency          : "
         << result.p50_us
         << " us\n";

    cout << "P95 latency          : "
         << result.p95_us
         << " us\n";

    cout << "P99 latency          : "
         << result.p99_us
         << " us\n";

    cout << "============================================\n";
}

// ============================================================
// Main
// ============================================================

int main(int argc, char *argv[])
{

    if (argc != 4)
    {

        cout << "Usage:\n";

        cout
            << "  ./benchmark_client "
            << "<clients> "
            << "<requests_per_client> "
            << "<operation>\n\n";

        cout
            << "Examples:\n";

        cout
            << "  ./benchmark_client 1 10000 SET\n";

        cout
            << "  ./benchmark_client 10 10000 SET\n";

        cout
            << "  ./benchmark_client 50 10000 GET\n";

        cout
            << "  ./benchmark_client 100 10000 ERASE\n";

        return 1;
    }

    size_t clients =
        stoull(argv[1]);

    size_t requests_per_client =
        stoull(argv[2]);

    string operation =
        argv[3];

    if (
        operation != "SET" &&
        operation != "GET" &&
        operation != "ERASE")
    {

        cerr
            << "Operation must be "
            << "SET, GET, or ERASE\n";

        return 1;
    }

    cout << "\nStarting benchmark...\n";

    cout << "Clients: "
         << clients << "\n";

    cout << "Requests/client: "
         << requests_per_client
         << "\n";

    cout << "Operation: "
         << operation
         << "\n";

    Result result =
        benchmark(
            clients,
            requests_per_client,
            operation);

    print_result(
        clients,
        requests_per_client,
        operation,
        result);

    return 0;
}