#include "commands.h"
#include "common.h"
#include "buffer.h"
#include "dlist.h"
#include <cassert>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <vector>

// ─── Constants ───────────────────────────────────────────────────────────────
static constexpr size_t   kMaxMsg          = 32u << 20;   // 32 MiB
static constexpr size_t   kMaxArgs         = 200'000;
static constexpr uint64_t kIdleTimeoutMs   = 5'000;
static constexpr uint16_t kPort            = 1234;
static constexpr int      kThreadPoolSize  = 4;

// ─── Serialization tags (must match client) ──────────────────────────────────
enum class ErrCode : uint32_t { Unknown = 1, TooBig = 2, BadType = 3, BadArg = 4 };
enum class Tag     : uint8_t  { Nil=0, Err=1, Str=2, Int=3, Dbl=4, Arr=5 };

// ─── Connection ──────────────────────────────────────────────────────────────
struct Conn {
    int     fd           = -1;
    bool    want_read    = false;
    bool    want_write   = false;
    bool    want_close   = false;
    Buffer  incoming;
    Buffer  outgoing;
    uint64_t last_active_ms = 0;
    DList   idle_node;
};

// fd → Conn* map (indexed by fd number)
static std::vector<Conn*> fd2conn;

// ─── fd helpers ──────────────────────────────────────────────────────────────
static void fd_set_nb(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) die("fcntl F_GETFL");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) die("fcntl F_SETFL");
}

// ─── Connection management ───────────────────────────────────────────────────
static void conn_put(Conn* conn) {
    if (fd2conn.size() <= static_cast<size_t>(conn->fd))
        fd2conn.resize(conn->fd + 1, nullptr);
    assert(!fd2conn[conn->fd]);
    fd2conn[conn->fd] = conn;
}

static void conn_destroy(Conn* conn) {
    ::close(conn->fd);
    fd2conn[conn->fd] = nullptr;
    dlist_detach(&conn->idle_node);
    delete conn;
}

// ─── Protocol parsing ────────────────────────────────────────────────────────
static int32_t parse_req(const uint8_t* data, size_t size,
                          std::vector<std::string>& out) {
    const uint8_t* end = data + size;
    auto read_u32 = [&](uint32_t& v) -> bool {
        if (data + 4 > end) return false;
        std::memcpy(&v, data, 4); data += 4; return true;
    };

    uint32_t nstr = 0;
    if (!read_u32(nstr) || nstr > kMaxArgs) return -1;

    out.reserve(nstr);
    for (uint32_t i = 0; i < nstr; ++i) {
        uint32_t len = 0;
        if (!read_u32(len)) return -1;
        if (data + len > end) return -1;
        out.emplace_back(reinterpret_cast<const char*>(data), len);
        data += len;
    }
    return (data == end) ? 0 : -1;
}

// ─── Response framing ────────────────────────────────────────────────────────
static void response_begin(Buffer& out, size_t& header_pos) {
    header_pos = out.abs_write_pos();
    out.append_u32(0);   // placeholder for length
}

static void response_end(Buffer& out, size_t header_pos) {
    size_t msg_size = out.abs_write_pos() - header_pos - 4;
    if (msg_size > kMaxMsg) {
        // Trim back to header, emit an error response
        out.resize_to(header_pos + 4);
        constexpr std::string_view kTooBig = "response is too big";
        out.append_u8(static_cast<uint8_t>(Tag::Err));
        out.append_u32(static_cast<uint32_t>(ErrCode::TooBig));
        out.append_u32(static_cast<uint32_t>(kTooBig.size()));
        out.append(kTooBig);
        msg_size = out.abs_write_pos() - header_pos - 4;
    }
    uint32_t len = static_cast<uint32_t>(msg_size);
    std::memcpy(out.data_at(header_pos), &len, 4);
}

// ─── Request processing ──────────────────────────────────────────────────────
static bool try_one_request(Conn* conn) {
    if (conn->incoming.readable() < 4) return false;

    uint32_t len = 0;
    std::memcpy(&len, conn->incoming.read_ptr(), 4);
    if (len > kMaxMsg) {
        log_msg("request too long — closing connection");
        conn->want_close = true;
        return false;
    }
    if (conn->incoming.readable() < 4 + len) return false;

    const uint8_t* body = conn->incoming.read_ptr() + 4;
    std::vector<std::string> cmd;
    if (parse_req(body, len, cmd) < 0) {
        log_msg("bad request — closing connection");
        conn->want_close = true;
        return false;
    }

    size_t header_pos = 0;
    response_begin(conn->outgoing, header_pos);
    do_request(cmd, conn->outgoing);
    response_end(conn->outgoing, header_pos);

    conn->incoming.consume(4 + len);
    return true;
}

// ─── I/O handlers ────────────────────────────────────────────────────────────
static void handle_write(Conn* conn) {
    assert(!conn->outgoing.empty());
    ssize_t rv = ::write(conn->fd, conn->outgoing.read_ptr(), conn->outgoing.readable());
    if (rv < 0) {
        if (errno == EAGAIN) return;
        log_errno("write() error");
        conn->want_close = true;
        return;
    }
    conn->outgoing.consume(static_cast<size_t>(rv));
    if (conn->outgoing.empty()) {
        conn->want_read  = true;
        conn->want_write = false;
    }
}

static void handle_read(Conn* conn) {
    uint8_t buf[64 * 1024];
    ssize_t rv = ::read(conn->fd, buf, sizeof(buf));
    if (rv < 0) {
        if (errno == EAGAIN) return;
        log_errno("read() error");
        conn->want_close = true;
        return;
    }
    if (rv == 0) {
        log_msg(conn->incoming.empty() ? "client closed" : "unexpected EOF");
        conn->want_close = true;
        return;
    }
    conn->incoming.append(buf, static_cast<size_t>(rv));

    // Process all complete requests (pipelining support)
    while (try_one_request(conn)) {}

    if (!conn->outgoing.empty()) {
        conn->want_read  = false;
        conn->want_write = true;
        handle_write(conn);
    }
}

// ─── Accept ──────────────────────────────────────────────────────────────────
static void handle_accept(int listen_fd) {
    sockaddr_in client_addr = {};
    socklen_t   addrlen     = sizeof(client_addr);
    int connfd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
    if (connfd < 0) {
        log_errno("accept() error");
        return;
    }
    uint32_t ip = client_addr.sin_addr.s_addr;
    std::fprintf(stderr, "[INFO] new client %u.%u.%u.%u:%u fd=%d\n",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
        ntohs(client_addr.sin_port), connfd);

    fd_set_nb(connfd);
    auto* conn            = new Conn();
    conn->fd              = connfd;
    conn->want_read       = true;
    conn->last_active_ms  = get_monotonic_msec();
    dlist_init(&conn->idle_node);
    dlist_insert_before(&g_data.idle_list, &conn->idle_node);
    conn_put(conn);
}

// ─── Timer management ────────────────────────────────────────────────────────
static bool hnode_same(HNode* a, HNode* b) { return a == b; }

static void process_timers() {
    uint64_t now_ms = get_monotonic_msec();

    // Idle connection timeouts (linked list ordered by last_active)
    while (!dlist_empty(&g_data.idle_list)) {
        Conn* conn = container_of(g_data.idle_list.next, Conn, idle_node);
        if (conn->last_active_ms + kIdleTimeoutMs > now_ms) break;
        std::fprintf(stderr, "[INFO] idle timeout fd=%d\n", conn->fd);
        conn_destroy(conn);
    }

    // Key TTL expiry (min-heap)
    constexpr size_t kMaxExpired = 2000;
    size_t nexpired = 0;
    while (!g_data.heap.empty() && g_data.heap[0].val < now_ms) {
        Entry* ent  = container_of(g_data.heap[0].ref, Entry, heap_idx);
        HNode* node = g_data.db.remove(&ent->node, hnode_same);
        assert(node == &ent->node);
        entry_del(ent);
        if (++nexpired >= kMaxExpired) break;
    }
}

static int32_t next_timer_ms() {
    uint64_t now_ms  = get_monotonic_msec();
    uint64_t next_ms = static_cast<uint64_t>(-1);

    if (!dlist_empty(&g_data.idle_list)) {
        Conn* conn = container_of(g_data.idle_list.next, Conn, idle_node);
        next_ms    = conn->last_active_ms + kIdleTimeoutMs;
    }
    if (!g_data.heap.empty() && g_data.heap[0].val < next_ms)
        next_ms = g_data.heap[0].val;

    if (next_ms == static_cast<uint64_t>(-1)) return -1;
    if (next_ms <= now_ms)                    return 0;
    return static_cast<int32_t>(next_ms - now_ms);
}

// ─── main ────────────────────────────────────────────────────────────────────
int main() {
    // Init idle list sentinel
    dlist_init(&g_data.idle_list);

    // Create listening socket
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) die("socket()");

    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fd_set_nb(listen_fd);

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(kPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(listen_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)))
        die("bind()");
    if (::listen(listen_fd, SOMAXCONN))
        die("listen()");

    std::fprintf(stderr, "[INFO] listening on port %u\n", kPort);

    std::vector<pollfd> poll_args;
    while (true) {
        poll_args.clear();
        poll_args.push_back({listen_fd, POLLIN, 0});

        for (Conn* conn : fd2conn) {
            if (!conn) continue;
            short events = POLLERR;
            if (conn->want_read)  events |= POLLIN;
            if (conn->want_write) events |= POLLOUT;
            poll_args.push_back({conn->fd, events, 0});
        }

        int rv = ::poll(poll_args.data(), static_cast<nfds_t>(poll_args.size()),
                        next_timer_ms());
        if (rv < 0) {
            if (errno == EINTR) continue;
            die("poll()");
        }

        // Accept new connections
        if (poll_args[0].revents & POLLIN)
            handle_accept(listen_fd);

        // Handle existing connections
        for (size_t i = 1; i < poll_args.size(); ++i) {
            short ready = poll_args[i].revents;
            if (!ready) continue;

            Conn* conn = fd2conn[poll_args[i].fd];
            if (!conn) continue;

            // Refresh idle timer
            conn->last_active_ms = get_monotonic_msec();
            dlist_detach(&conn->idle_node);
            dlist_insert_before(&g_data.idle_list, &conn->idle_node);

            if (ready & POLLIN)  handle_read(conn);
            if (ready & POLLOUT) handle_write(conn);
            if ((ready & POLLERR) || conn->want_close) conn_destroy(conn);
        }

        process_timers();
    }

    ::close(listen_fd);
    return 0;
}
