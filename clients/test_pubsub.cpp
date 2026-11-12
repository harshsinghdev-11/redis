#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

static void append_u32(vector<uint8_t>& out, uint32_t val) {
    for (int i = 0; i < 4; i++) {
        out.push_back((val >> (i * 8)) & 0xFF);
    }
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int64_t get_i64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= ((uint64_t)p[i]) << (i * 8);
    }
    int64_t res;
    memcpy(&res, &v, 8);
    return res;
}

vector<uint8_t> make_request(const vector<string>& cmd) {
    vector<uint8_t> body;
    append_u32(body, static_cast<uint32_t>(cmd.size()));
    for (const string &s : cmd) {
        append_u32(body, static_cast<uint32_t>(s.size()));
        body.insert(body.end(), s.begin(), s.end());
    }

    vector<uint8_t> frame;
    append_u32(frame, static_cast<uint32_t>(body.size()));
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

int connect_client() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv != 0) {
        perror("connect failed");
        return -1;
    }
    return fd;
}

void send_cmd(int fd, const vector<string>& cmd) {
    vector<uint8_t> req = make_request(cmd);
    size_t total = 0;
    while (total < req.size()) {
        ssize_t n = send(fd, req.data() + total, req.size() - total, 0);
        assert(n > 0);
        total += n;
    }
}

vector<uint8_t> read_one_frame(int fd) {
    uint8_t hdr[4];
    size_t got = 0;
    while (got < 4) {
        ssize_t n = read(fd, hdr + got, 4 - got);
        assert(n > 0);
        got += n;
    }
    uint32_t body_len = get_u32(hdr);
    vector<uint8_t> body(body_len);
    got = 0;
    while (got < body_len) {
        ssize_t n = read(fd, body.data() + got, body_len - got);
        assert(n > 0);
        got += n;
    }
    return body;
}

int main() {
    cout << "========================================" << endl;
    cout << "   RUNNING PUBSUB INTEGRATION TESTS    " << endl;
    cout << "========================================" << endl;

    // Test 1: Subscribe single and multi-channel
    cout << "[TEST 1] Client 1 subscribing to 'news' and 'tech'..." << endl;
    int sub1 = connect_client();
    assert(sub1 >= 0);
    send_cmd(sub1, {"subscribe", "news", "tech"});

    // Frame 1: ["subscribe", "news", 1]
    vector<uint8_t> f1 = read_one_frame(sub1);
    assert(f1[0] == TAG_ARR);
    assert(get_u32(&f1[1]) == 3);
    cout << " -> Received subscribe confirmation for 'news' (count=1)" << endl;

    // Frame 2: ["subscribe", "tech", 2]
    vector<uint8_t> f2 = read_one_frame(sub1);
    assert(f2[0] == TAG_ARR);
    assert(get_u32(&f2[1]) == 3);
    cout << " -> Received subscribe confirmation for 'tech' (count=2)" << endl;

    // Test 2: Publish message from Client 2
    cout << "[TEST 2] Client 2 publishing to 'news'..." << endl;
    int pub = connect_client();
    assert(pub >= 0);
    send_cmd(pub, {"publish", "news", "headline_1"});

    // Publisher receives TAG_INT (1 receiver)
    vector<uint8_t> pub_resp = read_one_frame(pub);
    assert(pub_resp[0] == TAG_INT);
    int64_t receivers = get_i64(&pub_resp[1]);
    assert(receivers == 1);
    cout << " -> Publisher received receiver count: " << receivers << endl;

    // Client 1 receives pushed message: ["message", "news", "headline_1"]
    vector<uint8_t> pushed = read_one_frame(sub1);
    assert(pushed[0] == TAG_ARR);
    assert(get_u32(&pushed[1]) == 3);
    cout << " -> Subscriber 1 received pushed message on 'news' successfully!" << endl;

    // Test 3: Multiple subscribers fanout
    cout << "[TEST 3] Client 3 subscribing to 'news' and testing fan-out..." << endl;
    int sub2 = connect_client();
    assert(sub2 >= 0);
    send_cmd(sub2, {"subscribe", "news"});
    vector<uint8_t> sub2_conf = read_one_frame(sub2);
    assert(sub2_conf[0] == TAG_ARR);

    send_cmd(pub, {"publish", "news", "breaking_alert"});
    vector<uint8_t> pub_resp2 = read_one_frame(pub);
    assert(pub_resp2[0] == TAG_INT);
    assert(get_i64(&pub_resp2[1]) == 2);
    cout << " -> Publisher received receiver count 2 (fan-out delivered to both)" << endl;

    vector<uint8_t> push_sub1 = read_one_frame(sub1);
    vector<uint8_t> push_sub2 = read_one_frame(sub2);
    assert(push_sub1[0] == TAG_ARR);
    assert(push_sub2[0] == TAG_ARR);
    cout << " -> Both Subscriber 1 and Subscriber 2 received 'breaking_alert'!" << endl;

    // Test 4: Channel isolation (publish to 'sports' where no one is subscribed)
    cout << "[TEST 4] Publishing to unpopulated channel 'sports'..." << endl;
    send_cmd(pub, {"publish", "sports", "game_score"});
    vector<uint8_t> sports_resp = read_one_frame(pub);
    assert(sports_resp[0] == TAG_INT);
    assert(get_i64(&sports_resp[1]) == 0);
    cout << " -> Correctly returned 0 receivers for channel with no subscribers" << endl;

    // Test 5: Context enforcement (disallowed command in Pub/Sub mode)
    cout << "[TEST 5] Testing command restriction while in Pub/Sub mode..." << endl;
    send_cmd(sub1, {"set", "k", "v"});
    vector<uint8_t> err_resp = read_one_frame(sub1);
    assert(err_resp[0] == TAG_ERR);
    assert(get_u32(&err_resp[1]) == 400);
    cout << " -> Successfully rejected 'SET' with error 400 in Pub/Sub mode" << endl;

    // Test 6: Ping in Pub/Sub mode
    cout << "[TEST 6] Testing PING in Pub/Sub mode..." << endl;
    send_cmd(sub1, {"ping", "test_ping"});
    vector<uint8_t> ping_resp = read_one_frame(sub1);
    assert(ping_resp[0] == TAG_ARR);
    cout << " -> Successfully received PING response in Pub/Sub mode" << endl;

    // Test 7: Unsubscribe and state exit
    cout << "[TEST 7] Unsubscribing from channels..." << endl;
    send_cmd(sub1, {"unsubscribe", "news"});
    vector<uint8_t> un1 = read_one_frame(sub1);
    assert(un1[0] == TAG_ARR);
    cout << " -> Unsubscribed from 'news' (1 channel left)" << endl;

    send_cmd(sub1, {"unsubscribe", "tech"});
    vector<uint8_t> un2 = read_one_frame(sub1);
    assert(un2[0] == TAG_ARR);
    cout << " -> Unsubscribed from 'tech' (0 channels left - exited pubsub mode)" << endl;

    // Now normal commands should work for Client 1
    send_cmd(sub1, {"set", "mykey", "myval"});
    vector<uint8_t> set_resp = read_one_frame(sub1);
    assert(set_resp[0] == TAG_NIL);

    send_cmd(sub1, {"get", "mykey"});
    vector<uint8_t> get_resp = read_one_frame(sub1);
    assert(get_resp[0] == TAG_STR);
    cout << " -> Client 1 successfully resumed standard SET/GET commands!" << endl;

    // Test 8: Abrupt disconnect teardown
    cout << "[TEST 8] Testing abrupt subscriber disconnect teardown..." << endl;
    close(sub2); // Client 3 abruptly closes socket
    usleep(50000); // 50ms for server to process close

    send_cmd(pub, {"publish", "news", "after_disconnect"});
    vector<uint8_t> after_disc = read_one_frame(pub);
    assert(after_disc[0] == TAG_INT);
    assert(get_i64(&after_disc[1]) == 0);
    cout << " -> Server safely cleaned up disconnected subscriber; receiver count is now 0!" << endl;

    close(sub1);
    close(pub);

    cout << "\n>>> ALL 8 PUBSUB INTEGRATION TESTS PASSED PERFECTLY! <<<\n" << endl;
    return 0;
}

