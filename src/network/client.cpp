#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

static constexpr size_t  kMaxMsg = 32u << 20;
static constexpr uint16_t kPort  = 1234;

static void die(const char* msg) {
    std::fprintf(stderr, "[errno=%d] %s\n", errno, msg);
    std::abort();
}

// ─── I/O helpers ─────────────────────────────────────────────────────────────
static bool read_full(int fd, uint8_t* buf, size_t n) {
    while (n > 0) {
        ssize_t rv = ::read(fd, buf, n);
        if (rv <= 0) return false;
        buf += rv;
        n   -= static_cast<size_t>(rv);
    }
    return true;
}

static bool write_all(int fd, const uint8_t* buf, size_t n) {
    while (n > 0) {
        ssize_t rv = ::write(fd, buf, n);
        if (rv <= 0) return false;
        buf += rv;   // BUG FIX: original forgot to advance the pointer
        n   -= static_cast<size_t>(rv);
    }
    return true;
}

// ─── Serialization tags ──────────────────────────────────────────────────────
enum class Tag : uint8_t { Nil=0, Err=1, Str=2, Int=3, Dbl=4, Arr=5 };

// ─── Request sending ─────────────────────────────────────────────────────────
static bool send_req(int fd, const std::vector<std::string>& cmd) {
    // Compute total body length
    uint32_t body_len = 4;   // nargs field
    for (auto& s : cmd) body_len += 4 + static_cast<uint32_t>(s.size());
    if (body_len > kMaxMsg) { std::fputs("request too large\n", stderr); return false; }

    std::vector<uint8_t> buf;
    buf.reserve(4 + body_len);

    auto push_u32 = [&](uint32_t v) {
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v),
                               reinterpret_cast<uint8_t*>(&v) + 4);
    };

    push_u32(body_len);
    push_u32(static_cast<uint32_t>(cmd.size()));
    for (auto& s : cmd) {
        push_u32(static_cast<uint32_t>(s.size()));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(s.data()),
                               reinterpret_cast<const uint8_t*>(s.data()) + s.size());
    }

    return write_all(fd, buf.data(), buf.size());
}

// ─── Response printing ───────────────────────────────────────────────────────
static int32_t print_response(const uint8_t* data, size_t size) {
    if (size < 1) { std::fputs("bad response: empty\n", stderr); return -1; }
    switch (static_cast<Tag>(data[0])) {
    case Tag::Nil:
        std::puts("(nil)");
        return 1;
    case Tag::Err: {
        if (size < 9) { std::fputs("bad response: short err\n", stderr); return -1; }
        int32_t  code = 0;
        uint32_t len  = 0;
        std::memcpy(&code, data + 1, 4);
        std::memcpy(&len,  data + 5, 4);
        if (size < 9 + len) { std::fputs("bad response: truncated err\n", stderr); return -1; }
        std::printf("(err) %d %.*s\n", code, static_cast<int>(len), data + 9);
        return static_cast<int32_t>(9 + len);
    }
    case Tag::Str: {
        if (size < 5) { std::fputs("bad response: short str\n", stderr); return -1; }
        uint32_t len = 0;
        std::memcpy(&len, data + 1, 4);
        if (size < 5 + len) { std::fputs("bad response: truncated str\n", stderr); return -1; }
        std::printf("(str) %.*s\n", static_cast<int>(len), data + 5);
        return static_cast<int32_t>(5 + len);
    }
    case Tag::Int: {
        if (size < 9) { std::fputs("bad response: short int\n", stderr); return -1; }
        int64_t val = 0;
        std::memcpy(&val, data + 1, 8);
        std::printf("(int) %ld\n", val);
        return 9;
    }
    case Tag::Dbl: {
        if (size < 9) { std::fputs("bad response: short dbl\n", stderr); return -1; }
        double val = 0;
        std::memcpy(&val, data + 1, 8);
        std::printf("(dbl) %g\n", val);
        return 9;
    }
    case Tag::Arr: {
        if (size < 5) { std::fputs("bad response: short arr\n", stderr); return -1; }
        uint32_t len = 0;
        std::memcpy(&len, data + 1, 4);
        std::printf("(arr) len=%u\n", len);
        size_t consumed = 5;
        for (uint32_t i = 0; i < len; ++i) {
            int32_t rv = print_response(data + consumed, size - consumed);
            if (rv < 0) return rv;
            consumed += static_cast<size_t>(rv);
        }
        std::puts("(arr) end");
        return static_cast<int32_t>(consumed);
    }
    default:
        std::fputs("bad response: unknown tag\n", stderr);
        return -1;
    }
}

static bool read_res(int fd) {
    uint8_t hdr[4];
    if (!read_full(fd, hdr, 4)) {
        std::fputs(errno ? "read() error" : "EOF", stderr);
        return false;
    }
    uint32_t len = 0;
    std::memcpy(&len, hdr, 4);
    if (len > kMaxMsg) { std::fputs("response too long\n", stderr); return false; }

    std::vector<uint8_t> body(len);
    if (!read_full(fd, body.data(), len)) {
        std::fputs("read() error\n", stderr);
        return false;
    }
    int32_t rv = print_response(body.data(), len);
    if (rv < 0 || static_cast<uint32_t>(rv) != len) {
        std::fputs("bad response\n", stderr);
        return false;
    }
    return true;
}

// ─── main ────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fputs("Usage: client <command> [args...]\n", stderr);
        return 1;
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket()");

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(kPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)))
        die("connect()");

    std::vector<std::string> cmd(argv + 1, argv + argc);
    bool ok = send_req(fd, cmd) && read_res(fd);

    ::close(fd);
    return ok ? 0 : 1;
}
