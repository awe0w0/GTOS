// A bounded loopback fixture for Chromium navigation across two GTOS processes.
#include "../apps/native/probe.h"
#include <process/event_abi.h>
#include <process/loopback_abi.h>

namespace {

constexpr uint32_t port = 18761;
constexpr int32_t again = GTOS_LOOPBACK_ERR_AGAIN;
uint32_t timer;

template<class Request>
int32_t Invoke(uint32_t operation, const Request& request) {
    return static_cast<int32_t>(Call(operation, reinterpret_cast<uint32_t>(&request), sizeof(request)));
}

int32_t Control(uint32_t operation, uint32_t handle) {
    return Invoke(operation, GtosLoopbackControlRequest{GTOS_LOOPBACK_ABI_VERSION, handle});
}

int32_t Option(uint32_t operation, uint32_t handle, uint32_t value) {
    return Invoke(operation, GtosLoopbackOptionRequest{GTOS_LOOPBACK_ABI_VERSION, handle, value});
}

int32_t Transfer(uint32_t operation, uint32_t handle, const void* buffer, uint32_t bytes) {
    return Invoke(operation, GtosLoopbackTransferRequest{
        GTOS_LOOPBACK_ABI_VERSION, handle, reinterpret_cast<uint32_t>(buffer), bytes});
}

template<uint32_t Size>
void Message(const char (&text)[Size]) {
    Call(GTOS_SYS_WRITE, reinterpret_cast<uint32_t>(text), Size - 1);
}

bool Wait() {
    return Invoke(GTOS_SYS_EVENT_WAIT, GtosEventWaitRequest{
        GTOS_EVENT_ABI_VERSION, reinterpret_cast<uint32_t>(&timer), 1, 1}) == GTOS_EVENT_ERR_TIMEOUT;
}

bool Send(uint32_t handle, const char* data, uint32_t bytes) {
    const uint32_t start = Call(GTOS_SYS_TICKS);
    while (bytes && Call(GTOS_SYS_TICKS) - start < 1500) {
        const int32_t size = Transfer(GTOS_SYS_LOOPBACK_WRITE, handle, data, bytes);
        if (size == again) {
            if (!Wait()) return false;
            continue;
        }
        if (size <= 0 || static_cast<uint32_t>(size) > bytes) return false;
        data += size;
        bytes -= size;
    }
    return !bytes;
}

enum Path { Invalid, Redirect, Page, Missing };

char request[2048];

bool PathMatches(const char* path, uint32_t end) {
    for (uint32_t i = 4; i < end; ++i) {
        if (!*path || request[i] != *path) return false;
        ++path;
    }
    return !*path;
}

Path ReadRequest(uint32_t handle) {
    const uint32_t start = Call(GTOS_SYS_TICKS);
    uint32_t bytes = 0;
    while (bytes < sizeof(request) && Call(GTOS_SYS_TICKS) - start < 1000) {
        const int32_t size = Transfer(GTOS_SYS_LOOPBACK_READ, handle, request + bytes, sizeof(request) - bytes);
        if (size == again) {
            if (!Wait()) return Invalid;
            continue;
        }
        if (size <= 0 || static_cast<uint32_t>(size) > sizeof(request) - bytes) return Invalid;
        const uint32_t prior = bytes;
        bytes += size;
        for (uint32_t i = prior > 3 ? prior - 3 : 0; i + 3 < bytes; ++i) {
            if (request[i] != '\r' || request[i + 1] != '\n' ||
                request[i + 2] != '\r' || request[i + 3] != '\n') continue;
            uint32_t line = 0;
            while (line < i && request[line] != '\r' && request[line] != '\n') ++line;
            constexpr char version[] = " HTTP/1.1";
            constexpr char method[] = "GET ";
            if (line < sizeof(method) - 1 + sizeof(version) - 1 ||
                line + 1 >= bytes || request[line + 1] != '\n' || request[line] != '\r')
                return Invalid;
            for (uint32_t j = 0; j < sizeof(method) - 1; ++j)
                if (request[j] != method[j]) return Invalid;
            const uint32_t end = line - (sizeof(version) - 1);
            for (uint32_t j = 0; j < sizeof(version) - 1; ++j)
                if (request[end + j] != version[j]) return Invalid;
            if (PathMatches("/start", end)) return Redirect;
            if (PathMatches("/page?from=redirect", end)) return Page;
            return Missing;
        }
    }
    return Invalid;
}

constexpr char redirect[] =
    "HTTP/1.1 302 Found\r\nLocation: /page?from=redirect\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
constexpr char missing[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
constexpr char pageHeader[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n";
constexpr char page[] = R"HTML(<!doctype html><meta charset="utf-8"><title>GTOS loopback HTTP test</title>
<style>
html,body{margin:0;width:100%;height:100%}
#surface,#done{display:block;width:100%;height:100%;border:0;outline:0;background:#202020}
#surface[data-phase=ready]{background:#a52bc7}
#surface[data-phase=clicked]{background:#12a86d}
#done{background:#f4af22}
</style>
<button id="surface" type="button" aria-label="Activate the served page"></button>
<script>
'use strict';
const surface=document.getElementById('surface');
if (location.protocol==='http:' && location.hostname==='localhost' &&
    location.port==='18761' && location.pathname==='/page' && location.search==='?from=redirect') {
  surface.dataset.phase='ready';
  surface.addEventListener('click', event=>{
    if (event.isTrusted) {
      surface.dataset.phase='clicked';
      surface.focus();
    }
  });
  surface.addEventListener('keydown', event=>{
    if (event.isTrusted && event.code==='KeyG' && surface.dataset.phase==='clicked') {
      const marker=document.createElement('div');
      marker.id='done';
      document.body.replaceChildren(marker);
    }
  });
}
</script>)HTML";

}  // namespace

extern "C" int NativeEntry() {
    const int32_t listener = Control(GTOS_SYS_LOOPBACK_OPEN, 0);
    if (listener == GTOS_LOOPBACK_ERR_UNAVAILABLE) return 45;
    if (listener <= 0 || Option(GTOS_SYS_LOOPBACK_BIND, listener, port) ||
        Option(GTOS_SYS_LOOPBACK_LISTEN, listener, 4) ||
        Invoke(GTOS_SYS_EVENT_CREATE, GtosEventCreateRequest{
            GTOS_EVENT_ABI_VERSION, 0, reinterpret_cast<uint32_t>(&timer)}) != 0 ||
        !timer) return 201;
    Message("GTOS BROWSER HTTP SERVER READY\n");

    bool redirected = false;
    const uint32_t start = Call(GTOS_SYS_TICKS);
    while (Call(GTOS_SYS_TICKS) - start < 120000) {
        const int32_t peer = Control(GTOS_SYS_LOOPBACK_ACCEPT, listener);
        if (peer == again) {
            if (!Wait()) return 206;
            continue;
        }
        if (peer <= 0) return 202;
        const Path path = ReadRequest(peer);
        bool sent = false;
        if (path == Redirect) {
            sent = Send(peer, redirect, sizeof(redirect) - 1);
            if (sent) {
                redirected = true;
                Message("GTOS BROWSER HTTP REDIRECT SENT\n");
            }
        } else if (path == Page && redirected) {
            sent = Send(peer, pageHeader, sizeof(pageHeader) - 1) && Send(peer, page, sizeof(page) - 1);
            if (sent) Message("GTOS BROWSER HTTP PAGE SENT\n");
        } else if (path == Missing) {
            sent = Send(peer, missing, sizeof(missing) - 1);
        }
        if (Control(GTOS_SYS_LOOPBACK_CLOSE, peer)) return 203;
        if (path == Page && redirected) {
            if (!sent || Control(GTOS_SYS_LOOPBACK_CLOSE, listener) ||
                Invoke(GTOS_SYS_EVENT_CLOSE, GtosEventControlRequest{GTOS_EVENT_ABI_VERSION, timer}))
                return 204;
            return 0;
        }
    }
    return 205;
}
