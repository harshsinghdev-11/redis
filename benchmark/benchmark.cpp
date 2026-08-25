#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
using namespace std;

static constexpr const char* SERVER_IP = "127.0.0.1";
static constexpr uint16_t SERVER_PORT = 1234;

void append_u32(vector<uint8_t>& buffer, uint32_t value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);

    buffer.insert(buffer.end(),bytes,bytes + sizeof(value));
}

void append_string(vector<uint8_t>& buffer,const string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());

    append_u32(buffer, len);

    buffer.insert(
        buffer.end(),
        str.begin(),
        str.end()
    );
}

vector<uint8_t> make_request(
    const vector<string>& cmd
) {
    vector<uint8_t> body;

    uint32_t nstr =
        static_cast<uint32_t>(cmd.size());

    append_u32(body, nstr);

    for (const string& str : cmd) {
        append_string(body, str);
    }

    vector<uint8_t> request;

    uint32_t body_size =
        static_cast<uint32_t>(body.size());

    append_u32(request, body_size);

    request.insert(
        request.end(),
        body.begin(),
        body.end()
    );

    return request;
}


// ------------------------------------------------------------
// Socket helpers
// ------------------------------------------------------------

int create_socket() {

    int fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return fd;
}


void connect_to_server(int fd) {

    sockaddr_in addr{};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);

    addr.sin_addr.s_addr =
        inet_addr(SERVER_IP);

    if (connect(
            fd,
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr)
        ) < 0) {

        perror("connect");
        close(fd);
        exit(EXIT_FAILURE);
    }
}

bool send_all(int fd,const uint8_t* data, size_t size) {

    size_t total_sent = 0;

    while (total_sent < size) {

        ssize_t rv = send(
            fd,
            data + total_sent,
            size - total_sent,
            0
        );

        if (rv <= 0) {
            perror("send");
            return false;
        }

        total_sent +=
            static_cast<size_t>(rv);
    }

    return true;
}

bool recv_all(
    int fd,
    uint8_t* buffer,
    size_t size
) {

    size_t total_received = 0;

    while (total_received < size) {

        ssize_t rv = recv(
            fd,
            buffer + total_received,
            size - total_received,
            0
        );

        if (rv <= 0) {
            return false;
        }

        total_received +=
            static_cast<size_t>(rv);
    }

    return true;
}

bool receive_response(int fd,int32_t& status) {

    uint32_t response_length = 0;

    // First receive response length
    if (!recv_all(
            fd,
            reinterpret_cast<uint8_t*>(&response_length),
            sizeof(response_length)
        )) {

        return false;
    }
    if (response_length < 4) {
        cerr << "Invalid response length\n";
        return false;
    }

    // Receive status
    if (!recv_all(
            fd,
            reinterpret_cast<uint8_t*>(&status),
            sizeof(status)
        )) {

        return false;
    }

    // Remaining response data
    size_t data_size =
        response_length - 4;

    if (data_size > 0) {

        vector<uint8_t> data(
            data_size
        );

        if (!recv_all(
                fd,
                data.data(),
                data.size()
            )) {

            return false;
        }
    }

    return true;
}

bool execute_request(
    int fd,
    const vector<string>& command
) {

    vector<uint8_t> request =
        make_request(command);

    if (!send_all(
            fd,
            request.data(),
            request.size()
        )) {

        return false;
    }

    int32_t status = 0;

    if (!receive_response(fd, status)) {
        return false;
    }

    return true;
}

struct BenchmarkResult {

    double total_ms = 0;

    double avg_latency_us = 0;

    double p50_us = 0;
    double p95_us = 0;
    double p99_us = 0;

    double requests_per_second = 0;
};


double percentile(
    vector<double>& values,
    double percentile_value
) {

    if (values.empty()) {
        return 0;
    }

    sort(values.begin(),values.end());

    size_t index =
        static_cast<size_t>(
            percentile_value *
            (values.size() - 1)
        );

    return values[index];
}


// ------------------------------------------------------------
// Benchmark SET
// ------------------------------------------------------------

BenchmarkResult benchmark_set(
    int fd,
    size_t operations
) {

    vector<double> latencies;

    latencies.reserve(operations);

    auto total_start =
        chrono::steady_clock::now();

    for (size_t i = 0; i < operations; i++) {

        string key =
            "key_" + to_string(i);

        string value =
            "value_" + to_string(i);

        vector<string> command = {
            "set",
            key,
            value
        };

        auto start =
            chrono::steady_clock::now();

        if (!execute_request(
                fd,
                command
            )) {

            cerr << "Request failed at "
                 << i << "\n";

            break;
        }

        auto end =
            chrono::steady_clock::now();

        double latency_us =
            chrono::duration<double, micro>(
                end - start
            ).count();

        latencies.push_back(
            latency_us
        );
    }

    auto total_end =
        chrono::steady_clock::now();

    double total_seconds =
        chrono::duration<double>(
            total_end - total_start
        ).count();

    BenchmarkResult result;

    result.total_ms =
        total_seconds * 1000.0;

    if (!latencies.empty()) {

        double sum = 0;

        for (double latency : latencies) {
            sum += latency;
        }

        result.avg_latency_us =
            sum / latencies.size();

        result.p50_us =
            percentile(latencies, 0.50);

        result.p95_us =
            percentile(latencies, 0.95);

        result.p99_us =
            percentile(latencies, 0.99);

        result.requests_per_second =
            latencies.size() /
            total_seconds;
    }

    return result;
}


// ------------------------------------------------------------
// Benchmark GET
//
// We use keys inserted by SET benchmark.
// ------------------------------------------------------------

BenchmarkResult benchmark_get(
    int fd,
    size_t operations
) {

    vector<double> latencies;

    latencies.reserve(operations);

    auto total_start =
        chrono::steady_clock::now();

    for (size_t i = 0; i < operations; i++) {

        string key =
            "key_" + to_string(i);

        vector<string> command = {
            "get",
            key
        };

        auto start =
            chrono::steady_clock::now();

        if (!execute_request(
                fd,
                command
            )) {

            cerr << "Request failed at "
                 << i << "\n";

            break;
        }

        auto end =
            chrono::steady_clock::now();

        double latency_us =
            chrono::duration<double, micro>(
                end - start
            ).count();

        latencies.push_back(
            latency_us
        );
    }

    auto total_end =
        chrono::steady_clock::now();

    double total_seconds =
        chrono::duration<double>(
            total_end - total_start
        ).count();

    BenchmarkResult result;

    result.total_ms =
        total_seconds * 1000.0;

    if (!latencies.empty()) {

        double sum = 0;

        for (double latency : latencies) {
            sum += latency;
        }

        result.avg_latency_us =
            sum / latencies.size();

        result.p50_us =
            percentile(latencies, 0.50);

        result.p95_us =
            percentile(latencies, 0.95);

        result.p99_us =
            percentile(latencies, 0.99);

        result.requests_per_second =
            latencies.size() /
            total_seconds;
    }

    return result;
}


// ------------------------------------------------------------
// Benchmark ERASE
// ------------------------------------------------------------

BenchmarkResult benchmark_erase(
    int fd,
    size_t operations
) {

    vector<double> latencies;

    latencies.reserve(operations);

    auto total_start =
        chrono::steady_clock::now();

    for (size_t i = 0; i < operations; i++) {

        string key =
            "key_" + to_string(i);

        vector<string> command = {
            "erase",
            key
        };

        auto start =
            chrono::steady_clock::now();

        if (!execute_request(
                fd,
                command
            )) {

            cerr << "Request failed at "
                 << i << "\n";

            break;
        }

        auto end =
            chrono::steady_clock::now();

        double latency_us =
            chrono::duration<double, micro>(
                end - start
            ).count();

        latencies.push_back(
            latency_us
        );
    }

    auto total_end =
        chrono::steady_clock::now();

    double total_seconds =
        chrono::duration<double>(
            total_end - total_start
        ).count();

    BenchmarkResult result;

    result.total_ms =
        total_seconds * 1000.0;

    if (!latencies.empty()) {

        double sum = 0;

        for (double latency : latencies) {
            sum += latency;
        }

        result.avg_latency_us =
            sum / latencies.size();

        result.p50_us =
            percentile(latencies, 0.50);

        result.p95_us =
            percentile(latencies, 0.95);

        result.p99_us =
            percentile(latencies, 0.99);

        result.requests_per_second =
            latencies.size() /
            total_seconds;
    }

    return result;
}

void print_result(
    const string& operation,
    size_t operations,
    const BenchmarkResult& result
) {

    cout << "\n";
    cout << "========================================\n";
    cout << "Benchmark: " << operation << "\n";
    cout << "========================================\n";

    cout << "Operations       : "
         << operations << "\n";

    cout << "Total time       : "
         << result.total_ms
         << " ms\n";

    cout << "Throughput       : "
         << result.requests_per_second
         << " req/sec\n";

    cout << "Average latency  : "
         << result.avg_latency_us
         << " us\n";

    cout << "P50 latency      : "
         << result.p50_us
         << " us\n";

    cout << "P95 latency      : "
         << result.p95_us
         << " us\n";

    cout << "P99 latency      : "
         << result.p99_us
         << " us\n";

    cout << "========================================\n";
}

int main(int argc, char* argv[]) {

    if (argc != 3) {

        cout << "Usage:\n";

        cout << "  ./benchmark SET 10000\n";
        cout << "  ./benchmark GET 10000\n";
        cout << "  ./benchmark ERASE 10000\n";

        return 1;
    }

    string operation = argv[1];

    size_t operations =
        stoull(argv[2]);

    int fd = create_socket();

    connect_to_server(fd);

    BenchmarkResult result;

    if (operation == "SET") {

        result =
            benchmark_set(
                fd,
                operations
            );
    }
    else if (operation == "GET") {

        result =
            benchmark_get(
                fd,
                operations
            );
    }
    else if (operation == "ERASE") {

        result =
            benchmark_erase(
                fd,
                operations
            );
    }
    else {

        cerr << "Unknown operation: "
             << operation << "\n";

        close(fd);

        return 1;
    }

    print_result(
        operation,
        operations,
        result
    );

    close(fd);

    return 0;
}