#include "rtcnet.h"
#include <windows.h>
#include <winhttp.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>

/* ============================================================================
   room.cpp -- five-character room codes, for the Windows build.

   A port of web/roomcode.js and the supervising half of web/multiplayer.js.
   Same broker (signal/worker.js), same endpoints, same seat rules, so a room
   opened by either build can be joined by the other. Read those files for
   the reasoning behind each step; the comments here are about what is
   different natively.

   What is different: the browser does its waiting in async functions on the
   page's one thread. Here the waiting happens on a background thread of its
   own, because WinHTTP blocks, and blocking the game loop for a network round
   trip would freeze the picture. The thread touches only rtcnet (which is
   thread-safe) and this file's own state. It never touches the game: the
   game notices links opening and closing from its own frame, in netPoll.

   --- stopping ----------------------------------------------------------------
   A stop does not wait for the thread. A request in flight can take seconds
   to time out, and a Disconnect button that hangs for that long feels broken.
   So every session has a number, the thread checks it is still current
   before each thing it does, and an old thread that wakes to find itself
   replaced simply leaves.
   ========================================================================== */

bool rtcJsonString(const std::string& json, const char* key, std::string* out);
bool rtcJsonInt(const std::string& json, const char* key, int* out);
std::string rtcJsonQuote(const std::string& s);

namespace {

/* Must agree with BASE in web/roomcode.js and with signal/wrangler.toml. */
const wchar_t* BROKER_HOST = L"cinderlift-signal.kylerharris730.workers.dev";

std::mutex g_roomLock;
std::atomic<unsigned> g_session(0);
std::string g_code, g_status;

bool current(unsigned session) { return g_session.load() == session; }

void setStatus(unsigned session, const std::string& text) {
    std::lock_guard<std::mutex> hold(g_roomLock);
    if (current(session)) g_status = text;
}

/* Sleeps in small steps so a stopped session notices within a tenth of a
   second. Returns false once the session has been replaced. */
bool nap(unsigned session, int ms) {
    for (int t = 0; t < ms; t += 100) {
        if (!current(session)) return false;
        Sleep(100);
    }
    return current(session);
}

/* --- HTTPS -------------------------------------------------------------------- */

/* Returns the status code, or -1 when the broker could not be reached at all.
   Like roomcode.js, "unreachable" is one answer whatever the cause. */
int request(const wchar_t* method, const std::string& path, const std::string& body,
            std::string* response) {
    response->clear();
    static HINTERNET session = 0;
    static std::mutex sessionLock;
    {
        std::lock_guard<std::mutex> hold(sessionLock);
        if (!session) {
            session = WinHttpOpen(L"Cinderlift", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            /* Windows 7 and early 10 lack AUTOMATIC_PROXY; fall back to the
               older default rather than failing. */
            if (!session)
                session = WinHttpOpen(L"Cinderlift", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (session) WinHttpSetTimeouts(session, 8000, 8000, 10000, 10000);
        }
        if (!session) return -1;
    }
    HINTERNET connect = WinHttpConnect(session, BROKER_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) return -1;
    std::wstring wpath(path.begin(), path.end());
    HINTERNET req = WinHttpOpenRequest(connect, method, wpath.c_str(), 0, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    int status = -1;
    if (req) {
        const wchar_t* headers = L"Content-Type: application/json\r\n";
        const BOOL sent = WinHttpSendRequest(req, headers, (DWORD)-1L,
                                             body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                             (DWORD)body.size(), (DWORD)body.size(), 0);
        if (sent && WinHttpReceiveResponse(req, 0)) {
            DWORD code = 0, size = sizeof(code);
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX))
                status = (int)code;
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail) || !avail) break;
                /* The broker's replies are small; its own ceiling on bodies
                   is 16 KB. Anything much larger is not the broker. */
                if (response->size() + avail > 64 * 1024) { status = -1; break; }
                std::string chunk(avail, '\0');
                DWORD got = 0;
                if (!WinHttpReadData(req, &chunk[0], avail, &got) || !got) break;
                response->append(chunk, 0, got);
            }
        }
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(connect);
    return status;
}

std::string brokerError(const std::string& body, const char* fallback) {
    std::string e;
    return rtcJsonString(body, "error", &e) && !e.empty() ? e : std::string(fallback);
}

/* Waits for a link's finished description. rtcnet produces it once ICE has
   finished gathering, which is normally well under the four seconds the
   browser allows and is given fifteen here. */
bool awaitHostCode(unsigned session, int slot, std::string* code) {
    for (int i = 0; i < 150; ++i) {
        *code = rtcNetHostCode(slot);
        if (!code->empty()) return true;
        if (rtcNetState(slot) == RTC_LINK_FAILED) return false;
        if (!nap(session, 100)) return false;
    }
    return false;
}

bool live(RtcLinkState s) {
    /* Listed as the states that mean "still working" rather than the ones
       that mean "gone", for the reason multiplayer.js gives: a new state
       cannot then slip through as alive. */
    return s == RTC_LINK_OPEN || s == RTC_LINK_GATHERING ||
           s == RTC_LINK_CONNECTING || s == RTC_LINK_WAITING;
}

/* --- host ------------------------------------------------------------------- */

void hostThread(unsigned session) {
    rtcNetBeginHost();
    std::string offers[RTC_MAX_LINKS];
    for (int slot = 0; slot < RTC_MAX_LINKS; ++slot)
        if (!awaitHostCode(session, slot, &offers[slot])) {
            if (current(session)) rtcNetSetFault("Could not build the host connections");
            return;
        }
    std::string body = "{\"offers\":[";
    for (int slot = 0; slot < RTC_MAX_LINKS; ++slot)
        body += (slot ? "," : "") + rtcJsonQuote(offers[slot]);
    body += "]}";
    std::string reply, code;
    const int status = request(L"POST", "/room", body, &reply);
    if (!current(session)) return;
    if (status == -1 || status == 404) { rtcNetSetFault("Room service unavailable -- use Host LAN"); return; }
    if (status != 200 || !rtcJsonString(reply, "code", &code) || code.empty()) {
        rtcNetSetFault(brokerError(reply, "Could not open a room").c_str());
        return;
    }
    {
        std::lock_guard<std::mutex> hold(g_roomLock);
        if (!current(session)) return;
        g_code = code;
    }

    /* For the whole session, not just until three people have joined -- see
       superviseHost in multiplayer.js for the drop that taught that. Two
       jobs on one loop: take answers as they arrive, and give a seat whose
       player dropped a fresh offer so they come back as the same number. */
    /* Paced, not once a second for the whole session: fast for two minutes
       after the room opens, after somebody joins and after a seat is
       re-offered; every ten seconds otherwise; once a minute while all three
       seats are full, which is only to keep the room alive. hostPace in
       web/multiplayer.js has the reasoning -- this is the same policy, and
       the two builds should keep agreeing on it. */
    const DWORD BUSY_POLL_MS = 1000, IDLE_POLL_MS = 10000, FULL_POLL_MS = 60000;
    const DWORD BUSY_FOR_MS = 2 * 60 * 1000;
    DWORD busyUntil = GetTickCount() + BUSY_FOR_MS;
    DWORD lastPoll = GetTickCount() - FULL_POLL_MS;   /* ask straight away */

    bool wasOpen[RTC_MAX_LINKS] = { false, false, false };
    int seatTick = 0;
    while (current(session)) {
        /* The loop ticks every second either way: watching seats is local
           and free. Only the question to the broker is paced. */
        int open = 0;
        for (int slot = 0; slot < RTC_MAX_LINKS; ++slot) if (rtcNetState(slot) == RTC_LINK_OPEN) ++open;
        const DWORD now = GetTickCount();
        const DWORD pace = open >= RTC_MAX_LINKS ? FULL_POLL_MS
                         : (int)(busyUntil - now) > 0 ? BUSY_POLL_MS : IDLE_POLL_MS;
        if (now - lastPoll >= pace) {
            lastPoll = now;
            rtcNetTrace("room: poll (open seats, pace ms)", open, (int)pace);
            const int got = request(L"GET", "/room/" + code + "/answer", std::string(), &reply);
            if (!current(session)) return;
            if (got == 200) {
                std::string answer; int slot = -1;
                if (rtcJsonString(reply, "answer", &answer) && rtcJsonInt(reply, "slot", &slot)) {
                    rtcNetBeginAccept(slot, answer.c_str());
                    busyUntil = GetTickCount() + BUSY_FOR_MS;   /* friends arrive together */
                }
            } else if (got == 404) {
                rtcNetSetFault("The room expired -- host again for a new code");
                return;
            }
            /* 204 is "nobody yet"; a 409 is contention and resolves itself;
               an unreachable broker is not a reason to stop a game that is
               already running -- the players connected are connected
               directly. */
        }

        if (++seatTick >= 2) {
            seatTick = 0;
            for (int slot = 0; slot < RTC_MAX_LINKS; ++slot) {
                const RtcLinkState state = rtcNetState(slot);
                if (state == RTC_LINK_OPEN) { wasOpen[slot] = true; continue; }
                /* A seat that was never used still has its first offer in the
                   room; replacing it would break a code somebody is typing. */
                if (!wasOpen[slot] || live(state)) continue;
                wasOpen[slot] = false;
                /* Somebody just dropped, and is about to try coming back. */
                busyUntil = GetTickCount() + BUSY_FOR_MS;
                rtcNetRehost(slot);
                std::string offer;
                if (!awaitHostCode(session, slot, &offer)) continue;
                std::string ignored;
                request(L"POST", "/room/" + code + "/seat",
                        "{\"slot\":" + std::to_string(slot) + ",\"offer\":" + rtcJsonQuote(offer) + "}",
                        &ignored);
            }
        }
        if (!nap(session, 1000)) return;
    }
}

/* --- guest ------------------------------------------------------------------- */

/* One attempt at taking a seat. The same code for the first join and every
   rejoin, so the two cannot drift. `seat` is the one to ask for, or -1. */
bool joinOnce(unsigned session, const std::string& code, int seat, int* gotSeat, std::string* why) {
    std::string path = "/room/" + code;
    if (seat >= 0) path += "?seat=" + std::to_string(seat);
    std::string reply;
    const int status = request(L"GET", path, std::string(), &reply);
    if (!current(session)) return false;
    if (status == -1) { *why = "Room service unavailable -- try Join by IP"; return false; }
    if (status == 404) { *why = "No game with that code"; return false; }
    if (status == 409) { *why = brokerError(reply, "That game is full"); return false; }
    std::string offer, claim; int slot = -1;
    if (status != 200 || !rtcJsonString(reply, "offer", &offer) ||
        !rtcJsonString(reply, "claim", &claim) || !rtcJsonInt(reply, "slot", &slot)) {
        *why = brokerError(reply, "Could not read that code");
        return false;
    }
    rtcNetBeginJoin(offer.c_str());
    std::string answer;
    for (int i = 0; i < 150 && answer.empty(); ++i) {
        if (rtcNetState(0) == RTC_LINK_FAILED) break;
        answer = rtcNetJoinCode();
        if (answer.empty() && !nap(session, 100)) return false;
    }
    if (answer.empty()) {
        const std::string fault = rtcNetFault();
        *why = fault.empty() ? "Could not build a join code" : fault;
        return false;
    }
    const std::string body = "{\"slot\":" + std::to_string(slot) + ",\"claim\":" +
                             rtcJsonQuote(claim) + ",\"answer\":" + rtcJsonQuote(answer) + "}";
    const int posted = request(L"POST", "/room/" + code + "/answer", body, &reply);
    if (!current(session)) return false;
    if (posted == 404) { *why = "That code expired"; return false; }
    if (posted != 204 && posted != 200) { *why = brokerError(reply, "Could not send the reply"); return false; }
    *gotSeat = slot;
    return true;
}

void guestThread(unsigned session, std::string code) {
    setStatus(session, "Looking for that game...");
    int seat = -1;
    std::string why;
    if (!joinOnce(session, code, -1, &seat, &why)) {
        if (current(session)) { rtcNetSetFault(why.c_str()); setStatus(session, why); }
        return;
    }
    setStatus(session, "Connecting...");

    /* A drop is acted on rather than left for the player to find out about:
       the client simulates locally, so without this they would carry on in
       a world nobody else could see. See superviseGuest in multiplayer.js. */
    bool everOpen = false;
    while (nap(session, 1000)) {
        const RtcLinkState state = rtcNetState(0);
        if (state == RTC_LINK_OPEN) {
            if (!everOpen) setStatus(session, std::string());
            everOpen = true; continue;
        }
        if (!everOpen || live(state)) continue;
        everOpen = false;
        for (int attempt = 1; attempt <= 6; ++attempt) {
            char text[80];
            sprintf(text, "Connection lost -- rejoining (%d/6)", attempt);
            setStatus(session, text);
            if (!nap(session, attempt * 2000)) return;
            int again = -1;
            if (joinOnce(session, code, seat, &again, &why)) {
                seat = again;
                for (int i = 0; i < 150 && rtcNetState(0) != RTC_LINK_OPEN; ++i)
                    if (!nap(session, 100)) return;
                if (rtcNetState(0) == RTC_LINK_OPEN) { everOpen = true; setStatus(session, std::string()); break; }
            }
            if (!current(session)) return;
            if (attempt == 6) { setStatus(session, "Could not rejoin -- the game may have ended"); return; }
        }
    }
}

unsigned beginSession() {
    std::lock_guard<std::mutex> hold(g_roomLock);
    g_code.clear(); g_status.clear();
    return ++g_session;
}

} /* namespace */

void rtcRoomHost() {
    const unsigned session = beginSession();
    std::thread(hostThread, session).detach();
}

void rtcRoomJoin(const char* code) {
    std::string upper;
    for (const char* p = code; *p; ++p) {
        const char c = *p;
        if (c == ' ' || c == '-') continue;
        upper += (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    const unsigned session = beginSession();
    std::thread(guestThread, session, upper).detach();
}

void rtcRoomStop() {
    beginSession();
}

std::string rtcRoomCode() {
    std::lock_guard<std::mutex> hold(g_roomLock);
    return g_code;
}

std::string rtcRoomStatus() {
    std::lock_guard<std::mutex> hold(g_roomLock);
    return g_status;
}
