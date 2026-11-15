// Exercise the real Ethernet and IPv4 receivers without hardware I/O.
#include <net/ipv4.h>

using namespace gtos::drivers;
using namespace gtos::hardwarecommunication;
using namespace gtos::net;

InterruptHandler::InterruptHandler(InterruptsManager* manager, uint8_t number)
    : interruptNumber(number), interruptManager(manager) {}
InterruptHandler::~InterruptHandler() {}
uint32_t InterruptHandler::HandlerInterrupt(uint32_t esp) { return esp; }

amd_am79c973::amd_am79c973(PeripheralComponentInterconnectDeviceDescriptor*, InterruptsManager*)
    : InterruptHandler(nullptr, 0), MACAddress0Port(0), MACAddress2Port(0), MACAddress4Port(0),
      registerDataPort(0), registerAddressPort(0), resetPort(0), busControlRegisterDataPort(0) {}
amd_am79c973::~amd_am79c973() {}
void amd_am79c973::Activate() {}
int amd_am79c973::Reset() { return 0; }
uint32_t amd_am79c973::HandlerInterrupt(uint32_t esp) { return esp; }
uint32_t amd_am79c973::GetIPAddress() { return 0x020200C0U; }
uint64_t amd_am79c973::GetMACAddress() { return 0x060504030202ULL; }

RawDataHandler::RawDataHandler(amd_am79c973* device) : backend(device) {}
RawDataHandler::~RawDataHandler() {}
bool RawDataHandler::OnRawDataReceived(uint8_t*, uint32_t) { return false; }

namespace {
    constexpr uint32_t ethernetBytes = 14;
    constexpr uint32_t sourceAddress = 0x010200C0U;
    constexpr uint32_t localAddress = 0x020200C0U;
    constexpr uint32_t payloadBytes = 7;
    unsigned failures;

    void Print(const char* text) {
        uint32_t length = 0, result = 4;
        while (text[length]) ++length;
        asm volatile("int $0x80" : "+a"(result) : "b"(1), "c"(text), "d"(length) : "cc", "memory");
    }
    void Check(bool pass, const char* name) {
        if (!pass) {
            ++failures;
            Print("FAIL IPv4: ");
            Print(name);
            Print("\n");
        }
    }

    // Reference sums the bounded fixture's big-endian words and folds once at the end.
    uint16_t WireChecksum(const uint8_t* bytes, uint32_t length) {
        uint32_t sum = 0;
        for (uint32_t i = 0; i < length; ++i) sum += uint32_t(bytes[i]) << ((i % 2) ? 0 : 8);
        return 0xFFFFU - (sum % 0xFFFFU ? sum % 0xFFFFU : (sum ? 0xFFFFU : 0));
    }

    struct Frame {
        uint8_t bytes[ethernetBytes + 48] = {};
        explicit Frame(uint8_t words = 5) {
            const uint8_t header[] = {
                2, 2, 3, 4, 5, 6, 2, 7, 8, 9, 10, 11, 0x08, 0x00,
                uint8_t(0x40 | words), 0, 0, uint8_t(4 * words + payloadBytes),
                0x12, 0x34, 0x40, 0, 1, 17, 0, 0, 192, 0, 2, 1, 192, 0, 2, 2,
            };
            for (uint32_t i = 0; i < sizeof(header); ++i) bytes[i] = header[i];
            if (words == 6) bytes[ethernetBytes + 20] = 1; // NOP, then end-of-options.
            for (uint32_t i = 0; i < payloadBytes; ++i) bytes[ethernetBytes + 4 * words + i] = 0x80 + i;
            Seal(4 * words);
        }
        uint8_t* IP() { return bytes + ethernetBytes; }
        void Seal(uint32_t headerBytes = 20) {
            IP()[10] = IP()[11] = 0;
            const uint16_t sum = WireChecksum(IP(), headerBytes);
            IP()[10] = sum >> 8;
            IP()[11] = sum & 0xFF;
        }
    };

    class Receiver : public InternetProtocolHandler {
    public:
        Receiver(InternetProtocolProvider* provider) : InternetProtocolHandler(provider, 17) {}
        bool OnInternetProtocolReceived(uint32_t src, uint32_t dst, uint8_t* data, uint32_t size) override {
            ++calls;
            if (src != sourceAddress || dst != localAddress || size != payloadBytes) {
                Check(false, "exact payload bounds and endpoints");
                return false;
            }
            for (uint32_t i = 0; i < size; ++i) Check(data[i] == 0x80 + i, "payload bytes after header");
            return reply;
        }
        unsigned calls = 0;
        bool reply = false;
    };

    void Refused(EtherFrameProvider& ethernet, Receiver& receiver, Frame& frame, uint32_t size,
                 const char* description) {
        const Frame before = frame;
        const unsigned calls = receiver.calls;
        Check(!ethernet.OnRawDataReceived(frame.bytes, size) && receiver.calls == calls, description);
        bool unchanged = true;
        for (uint32_t i = 0; i < sizeof(frame.bytes); ++i) unchanged &= frame.bytes[i] == before.bytes[i];
        Check(unchanged, "discard preserves input frame");
    }
}

extern "C" int NetIpv4TestMain() {
    amd_am79c973 device(nullptr, nullptr);
    EtherFrameProvider ethernet(&device);
    InternetProtocolProvider ipv4(&ethernet, nullptr, 0, 0);
    Receiver receiver(&ipv4);
    Frame padded;
    Check(!ethernet.OnRawDataReceived(padded.bytes, sizeof(padded.bytes)) && receiver.calls == 1,
          "normal frame delivers decoded big-endian length without link padding");
    Frame options(6);
    Check(!ethernet.OnRawDataReceived(options.bytes, sizeof(options.bytes)) && receiver.calls == 2,
          "IPv4 options advance the payload start");
    Check(!ethernet.OnRawDataReceived(nullptr, 0), "missing Ethernet bytes");
    Refused(ethernet, receiver, padded, ethernetBytes + 19, "truncated IPv4 header");
    Refused(ethernet, receiver, padded, ethernetBytes + 26, "truncated IPv4 payload");
    Frame bad;
    bad.IP()[0] = 0x65; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "unsupported IP version");
    bad = Frame(); bad.IP()[0] = 0x44; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "undersized IHL");
    bad = Frame(6); bad.IP()[3] = 20; bad.Seal(24);
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "total length smaller than IHL");
    bad = Frame(); bad.IP()[6] = 0x20; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "fragment reassembly is unavailable");
    bad = Frame(); bad.IP()[7] = 1; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "nonzero fragment offset");
    bad = Frame(); bad.IP()[6] = 0x80; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "reserved fragment flag");
    bad = Frame(); bad.IP()[8] = 0; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "expired TTL");
    bad = Frame(); bad.IP()[10] ^= 1;
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "header checksum mismatch");
    bad = Frame(); bad.IP()[19] = 3; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "another host's IP packet");
    bad = Frame(); bad.IP()[9] = 255; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "unbound last protocol number");
    bad = Frame(); bad.bytes[12] = bad.bytes[13] = 255;
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "unbound last EtherType");

    receiver.reply = true;
    Check(ethernet.OnRawDataReceived(options.bytes, sizeof(options.bytes)) && receiver.calls == 3,
          "registered handler can request a reply");
    Check(options.bytes[1] == 7 && options.bytes[7] == 2 && options.IP()[15] == 2 &&
          options.IP()[19] == 1 && options.IP()[8] == 64 && WireChecksum(options.IP(), 24) == 0,
          "reply swaps endpoints and recomputes the full IPv4 header checksum");
    const uint8_t oddUnaligned[] = {0, 0x80, 0x10, 0xFE};
    Check(InternetProtocolProvider::Checksum(oddUnaligned + 1, 3) == 0xEE81U,
          "unsigned odd byte at an unaligned address");
    if (!failures) Print("PASS IPv4 ingress bounds, checksum, fragments, demultiplexing and reply\n");
    return failures ? 1 : 0;
}

asm(".global _start\n_start:\n andl $-16, %esp\n call NetIpv4TestMain\n"
    " movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");
