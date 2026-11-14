// Exercises the real kernel loopback ABI from CPL3.
#include <process/abi.h>
#include <process/loopback_abi.h>

namespace {

template<class Request>
int Call(unsigned operation, const Request& request) {
    asm volatile("int $0x80" : "+a"(operation)
                 : "b"(&request), "c"(sizeof(request)) : "cc", "memory");
    return static_cast<int>(operation);
}

int Control(unsigned op, unsigned handle) {
    return Call(op, GtosLoopbackControlRequest{GTOS_LOOPBACK_ABI_VERSION, handle});
}

int Option(unsigned op, unsigned handle, unsigned value) {
    return Call(op, GtosLoopbackOptionRequest{GTOS_LOOPBACK_ABI_VERSION, handle, value});
}

int Info(unsigned handle, GtosLoopbackInfo& result) {
    return Option(GTOS_SYS_LOOPBACK_INFO, handle, reinterpret_cast<unsigned>(&result));
}

int Transfer(unsigned op, unsigned handle, const void* data, unsigned bytes) {
    return Call(op, GtosLoopbackTransferRequest{
        GTOS_LOOPBACK_ABI_VERSION, handle, reinterpret_cast<unsigned>(data), bytes});
}

template<unsigned Size>
void Say(const char (&text)[Size]) {
    unsigned operation = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(operation)
                 : "b"(text), "c"(Size - 1) : "cc", "memory");
}

constexpr unsigned kPort = 18761;
char send_data[GTOS_LOOPBACK_TRANSFER_LIMIT];
char read_data[GTOS_LOOPBACK_TRANSFER_LIMIT];

int Run() {
    const int listener = Control(GTOS_SYS_LOOPBACK_OPEN, 0);
    if (listener == GTOS_LOOPBACK_ERR_UNAVAILABLE) {
        Say("GTOS LOOPBACK DISABLED\n");
        return 45;
    }
    const int client = Control(GTOS_SYS_LOOPBACK_OPEN, 0);
    const int waiting = Control(GTOS_SYS_LOOPBACK_OPEN, 0);
    constexpr unsigned unknown = 0x7fffffffU;
    if (listener <= 0 || client <= 0 || waiting <= 0 ||
        Control(GTOS_SYS_LOOPBACK_OPEN, 1) != GTOS_LOOPBACK_ERR_INVALID ||
        Control(GTOS_SYS_LOOPBACK_CLOSE, unknown) != GTOS_LOOPBACK_ERR_BAD_HANDLE ||
        Call(GTOS_SYS_LOOPBACK_OPEN, GtosLoopbackControlRequest{2, 0}) !=
            GTOS_LOOPBACK_ERR_UNAVAILABLE ||
        Option(GTOS_SYS_LOOPBACK_CONNECT, client, kPort) != GTOS_LOOPBACK_ERR_REFUSED ||
        Option(GTOS_SYS_LOOPBACK_BIND, listener, kPort) ||
        Option(GTOS_SYS_LOOPBACK_BIND, waiting, kPort) != GTOS_LOOPBACK_ERR_ADDRESS_IN_USE ||
        Option(GTOS_SYS_LOOPBACK_LISTEN, listener, 1) ||
        Control(GTOS_SYS_LOOPBACK_ACCEPT, listener) != GTOS_LOOPBACK_ERR_AGAIN ||
        Option(GTOS_SYS_LOOPBACK_CONNECT, client, kPort) ||
        Option(GTOS_SYS_LOOPBACK_CONNECT, waiting, kPort) != GTOS_LOOPBACK_ERR_REFUSED) {
        return 201;
    }
    GtosLoopbackInfo info{};
    if (Info(listener, info) || info.local_port != kPort ||
        info.peer_port || info.ready != GTOS_LOOPBACK_ACCEPTABLE ||
        Option(GTOS_SYS_LOOPBACK_INFO, listener, 0) != GTOS_LOOPBACK_ERR_BAD_ADDRESS) {
        return 202;
    }
    for (unsigned i = 0; i < sizeof(send_data); ++i)
        send_data[i] = static_cast<char>(i + i / 19);
    if (Transfer(GTOS_SYS_LOOPBACK_WRITE, client, send_data, sizeof(send_data)) !=
            static_cast<int>(sizeof(send_data)) ||
        Transfer(GTOS_SYS_LOOPBACK_WRITE, client, send_data, 1) != GTOS_LOOPBACK_ERR_AGAIN ||
        Info(client, info) || info.peer_port != kPort ||
        info.local_port < 49152 || info.ready) {
        return 203;
    }
    const unsigned client_port = info.local_port;
    const int server = Control(GTOS_SYS_LOOPBACK_ACCEPT, listener);
    if (server <= 0 || Info(server, info) || info.peer_port != client_port ||
        info.local_port != kPort ||
        info.ready != (GTOS_LOOPBACK_READABLE | GTOS_LOOPBACK_WRITABLE) ||
        Transfer(GTOS_SYS_LOOPBACK_READ, server, nullptr, 1) !=
            GTOS_LOOPBACK_ERR_BAD_ADDRESS ||
        Transfer(GTOS_SYS_LOOPBACK_READ, server, read_data, 23) != 23 ||
        Transfer(GTOS_SYS_LOOPBACK_WRITE, client, send_data, 24) != 23 ||
        Transfer(GTOS_SYS_LOOPBACK_READ, server, read_data, sizeof(read_data)) !=
            static_cast<int>(sizeof(read_data))) {
        return 204;
    }
    for (unsigned i = 0; i < sizeof(read_data); ++i) {
        if (read_data[i] != send_data[(23 + i) % sizeof(send_data)]) return 205;
    }
    if (Transfer(GTOS_SYS_LOOPBACK_READ, server, read_data, 1) != GTOS_LOOPBACK_ERR_AGAIN ||
        Transfer(GTOS_SYS_LOOPBACK_WRITE, server, "reply", 5) != 5 ||
        Control(GTOS_SYS_LOOPBACK_SHUTDOWN_WRITE, server) ||
        Info(client, info) ||
        info.ready != (GTOS_LOOPBACK_READABLE | GTOS_LOOPBACK_WRITABLE |
                       GTOS_LOOPBACK_END_OF_STREAM) ||
        Transfer(GTOS_SYS_LOOPBACK_READ, client, read_data, sizeof(read_data)) != 5 ||
        read_data[0] != 'r' || read_data[4] != 'y' ||
        Transfer(GTOS_SYS_LOOPBACK_READ, client, read_data, 1) ||
        Transfer(GTOS_SYS_LOOPBACK_WRITE, server, "x", 1) != GTOS_LOOPBACK_ERR_BROKEN_PIPE ||
        Control(GTOS_SYS_LOOPBACK_CLOSE, server) ||
        Info(client, info) ||
        info.ready != (GTOS_LOOPBACK_END_OF_STREAM | GTOS_LOOPBACK_PEER_CLOSED) ||
        Transfer(GTOS_SYS_LOOPBACK_WRITE, client, "x", 1) != GTOS_LOOPBACK_ERR_BROKEN_PIPE) {
        return 206;
    }
    if (Option(GTOS_SYS_LOOPBACK_CONNECT, waiting, kPort) ||
        Control(GTOS_SYS_LOOPBACK_CLOSE, listener) || Info(waiting, info) ||
        info.ready != (GTOS_LOOPBACK_END_OF_STREAM | GTOS_LOOPBACK_PEER_CLOSED)) {
        return 207;
    }
    // Leave three owned handles for NativeRuntime::Reap to reclaim.
    Say("GTOS LOOPBACK STREAM PASS\n");
    return 37;
}

}  // namespace

extern "C" int GtosContentShellMain(int, const char**) {
    return Run();
}
