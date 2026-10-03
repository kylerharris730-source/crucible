/* ============================================================================
   worker.js -- the signalling broker. A mailbox, not a relay.

   Two browsers cannot start a WebRTC connection without first exchanging two
   descriptions, and there is no channel between them yet to exchange over.
   That is the whole problem this solves, and it is worth being precise about
   how small it is: this carries about two kilobytes, once, at the moment two
   people connect. Then it is finished. It never sees a byte of the game.

   The distinction from a relay is the entire point. A relay carries every byte
   of every session and its bill grows with playtime. This mostly grows with
   JOINS -- with one exception worth being honest about: a host cannot be
   pushed to, so it asks for answers, and it keeps asking for as long as it
   hosts, because a player who drops has to be able to walk back in. Asked
   once a second, that was a request per second per host, and it did grow
   with playtime. Hosts now pace themselves (hostPace in web/multiplayer.js,
   hostThread in src/rtc/room.cpp): fast only around joins and drops, every
   ten seconds otherwise, once a minute when full. The real fix is a socket
   the room can push down -- a Durable Object -- rather than any interval.

   --- why D1 and not KV -------------------------------------------------------
   KV is the obvious first reach and it is the wrong tool here. KV is
   EVENTUALLY consistent across Cloudflare's edge: a host in one city writes a
   room, a guest in another reads it moments later and can legitimately get
   nothing back. For a cache that is fine. For a handshake it is a coin flip,
   and the failure looks exactly like "the code my friend sent me does not
   work" -- unreproducible, and impossible for a player to diagnose.

   D1 is a single primary, so a write is visible to the next read. That is the
   property this needs and the reason for the extra ten lines of schema.

   --- what it deliberately does not do ----------------------------------------
   No accounts, no persistence beyond minutes, no logging of who talked to whom.
   Rooms expire after ROOM_TTL and are swept on the next write, so the steady
   state of this database is close to empty.

   A description contains IP addresses -- that is what makes a direct
   connection possible -- so this briefly holds something a little personal.
   The short TTL is the mitigation, and it is why nothing here is kept for
   analytics. Peers learn each other's addresses regardless the moment they
   connect; that is inherent to having no relay, and it is no different from
   the direct-IP multiplayer the Windows build has always had.
   ========================================================================== */

const ROOM_TTL_SECONDS = 600;      /* ten minutes to send a code to a friend */
const MAX_BODY_BYTES   = 16 * 1024;

/* No I, L, O, 0 or 1. A code is read aloud, typed from a phone screen, and
   copied by people who did not ask to be careful. Ambiguous glyphs in a short
   code are a support burden with no upside. */
const ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';
/* Six characters, 31^6, about 887 million. Five (28.6 million) was too few
   once the rate limiter turned out to be loose -- see "guessing codes". A
   host asks for six with codeLength in its open request; a v0.6.18 host does
   not ask and still gets five, because that page's join box takes only five,
   and its guests have to be able to type the code. */
const CODE_LEN = 6;
const LEGACY_CODE_LEN = 5;
const ROOM_SLOTS = 3;
/* A guest which reserves an offer but vanishes must not consume a seat for the
   room's full ten-minute life. ICE normally answers within four seconds, and
   the Windows build gives it fifteen; three quarters of a minute is ample for
   a slow tab and short enough that a vacated seat comes back quickly. */
const CLAIM_TTL_MS = 45 * 1000;

/* --- guessing codes ------------------------------------------------------------

   A handful of live rooms among millions of codes are hard to hit by chance,
   and easy to hit by trying them all: every guess that lands hands
   over the host's offer, with the host's address in it, and reserves a seat.
   So each address gets a few guesses a minute (JOIN_LIMIT) and a few new rooms
   a minute (OPEN_LIMIT) -- Cloudflare's rate-limiting bindings, configured in
   wrangler.toml. A player joining a friend spends one or two, and the
   client's six rejoin attempts are spaced seconds apart.

   The limiter is approximate: measured on the live worker, a burst of forty
   guesses saw two refused, and a steady guess a second saw none. Cloudflare
   counts per machine and syncs lazily. It is kept because it costs nothing
   and does bite, but the real defence is the code length -- 887 million
   codes at even two hundred guesses a minute is months per address to find
   one of a few live rooms. An exact counter (D1, a Durable Object) would cost
   a billed request per guess, and a guesser could spend the free tier and
   take the broker down with it.

   Malformed codes are answered before any of that, or any database read.
   'PROBE', which the page asks for to see whether the broker is up, is one
   of them: O is not in the alphabet, so it can never be a room. */
function wellFormed(code) {
  if (!code || (code.length !== CODE_LEN && code.length !== LEGACY_CODE_LEN)) return false;
  for (const c of code) if (!ALPHABET.includes(c)) return false;
  return true;
}

function clientAddress(request) {
  return request.headers.get('CF-Connecting-IP') || 'unknown';
}

/* True when this request is within its limit. A binding that is not
   configured -- a local test -- limits nothing. */
async function withinLimit(binding, key) {
  if (!binding) return true;
  try { return (await binding.limit({ key })).success; }
  catch (e) { console.log('rate limiter unavailable:', String(e)); return true; }
}

const SLOW_DOWN = { error: 'too many tries -- wait a minute and try again' };

function newCode(length) {
  const bytes = new Uint8Array(length);
  crypto.getRandomValues(bytes);
  let out = '';
  for (let i = 0; i < length; i++) out += ALPHABET[bytes[i] % ALPHABET.length];
  return out;
}

function newClaim() {
  const bytes = new Uint8Array(12);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
}

/* --- the host key -------------------------------------------------------------

   A room code is five characters that the host reads out to friends, so it is
   not a secret, and anything it alone unlocks is open to whoever overhears or
   guesses it. Before the key, that was everything: collecting guests' answers
   (and with them their addresses), and replacing a seat's offer -- which is
   where the DTLS fingerprint that authenticates the connection travels, so
   whoever replaced it could sit in the middle of that guest's game.

   Opening a room now also returns a key that only the host holds. The two
   host operations -- collecting answers and re-offering a seat -- need it in
   the X-Room-Key header. Only its SHA-256 is stored, inside the offer JSON
   beside the offers, which guests never see whole. */
const KEY_HEADER = 'X-Room-Key';

async function sha256Hex(text) {
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return Array.from(new Uint8Array(digest), b => b.toString(16).padStart(2, '0')).join('');
}

function sameString(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string' || a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

/* Parses the stored offer column. Null for anything without a key -- a room
   opened by a build from before the key, which is refused rather than left
   manageable by anybody. */
function roomOffers(text) {
  try {
    const stored = JSON.parse(text);
    if (Array.isArray(stored.offers) && stored.offers.length === ROOM_SLOTS &&
        typeof stored.keyHash === 'string' && stored.keyHash) return stored;
  } catch (e) {}
  return null;
}

async function hostAllowed(request, stored) {
  const key = request.headers.get(KEY_HEADER);
  if (!key || key.length > 128) return false;
  return sameString(await sha256Hex(key), stored.keyHash);
}

const OBSOLETE = 'this game needs a newer version -- update or reload and try again';

function roomState(text) {
  if (!text) return { claims:[null,null,null], claimedAt:[0,0,0],
                      answers:[null,null,null], used:[false,false,false] };
  try {
    const state = JSON.parse(text);
    if (state.claims.length === ROOM_SLOTS && state.claimedAt.length === ROOM_SLOTS &&
        state.answers.length === ROOM_SLOTS && state.used.length === ROOM_SLOTS) {
      /* Rooms opened while seats were tied to addresses carry this; it
         means nothing now. */
      delete state.claimers;
      return state;
    }
  } catch (e) {}
  return null;
}

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET,POST,OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type, X-Room-Key',
  /* The key header makes the host's calls non-simple, so the browser asks
     first; let it remember the answer rather than doubling every poll. */
  'Access-Control-Max-Age': '86400',
  'Cache-Control': 'no-store'
};

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json', ...CORS }
  });
}

function empty(status) {
  return new Response(null, { status, headers: CORS });
}

/* Swept on write rather than on a schedule: a cron trigger is another thing to
   configure and be wrong about, and the natural moment to spend a little work
   tidying is when somebody is already paying for a write. */
async function sweep(db) {
  await db.prepare('DELETE FROM rooms WHERE expires < ?').bind(Date.now()).run();
}

async function readBody(request) {
  const text = await request.text();
  if (text.length > MAX_BODY_BYTES) return null;
  return text.trim();
}

export default {
  async fetch(request, env) {
    if (request.method === 'OPTIONS') return empty(204);

    const url = new URL(request.url);
    /* Tolerates being mounted at the root of a workers.dev subdomain or under
       a path like /signal on the main domain, so the route can change without
       touching the client. */
    const parts = url.pathname.split('/').filter(Boolean);
    const at = parts.indexOf('room');
    if (at < 0) return json({ error: 'not found' }, 404);

    const code = parts[at + 1] ? parts[at + 1].toUpperCase() : null;
    const tail = parts[at + 2] || null;
    const db = env.DB;

    try {
      /* --- host opens a room -------------------------------------------- */
      if (!code && request.method === 'POST') {
        if (!(await withinLimit(env.OPEN_LIMIT, 'open:' + clientAddress(request))))
          return json(SLOW_DOWN, 429);
        const offer = await readBody(request);
        let offers, codeLength = LEGACY_CODE_LEN;
        try {
          const opening = JSON.parse(offer);
          offers = opening.offers;
          if (opening.codeLength === CODE_LEN) codeLength = CODE_LEN;
        } catch (e) {}
        if (!Array.isArray(offers) || offers.length !== ROOM_SLOTS ||
            offers.some(o => typeof o !== 'string' || !o))
          return json({ error: 'bad offers' }, 400);
        const key = newClaim() + newClaim();
        const storedOffer = JSON.stringify({ offers, keyHash: await sha256Hex(key) });
        const state = JSON.stringify(roomState(null));
        await sweep(db);
        /* Retried rather than trusted: 28 million codes and a ten minute life
           make a collision vanishingly unlikely, but "vanishingly" is not
           "never" and the cost of handling it is three lines. */
        for (let attempt = 0; attempt < 5; attempt++) {
          const room = newCode(codeLength);
          const result = await db
            .prepare('INSERT OR IGNORE INTO rooms (code, offer, answer, expires) VALUES (?, ?, ?, ?)')
            .bind(room, storedOffer, state, Date.now() + ROOM_TTL_SECONDS * 1000)
            .run();
          if (result.meta.changes > 0) return json({ code: room, key });
        }
        return json({ error: 'could not allocate a code' }, 503);
      }

      if (!code) return json({ error: 'not found' }, 404);
      if (!wellFormed(code)) return json({ error: 'no such code' }, 404);

      /* --- guest collects the offer -------------------------------------- */
      if (!tail && request.method === 'GET') {
        const address = clientAddress(request);
        if (!(await withinLimit(env.JOIN_LIMIT, 'join:' + address)))
          return json(SLOW_DOWN, 429);
        const release = url.searchParams.get('release');
        /* Optimistic compare-and-swap makes reservation atomic without a D1
           transaction. Simultaneous guests may both read slot zero, but only
           one can replace the exact state string; the loser retries and takes
           slot one. */
        for (let attempt = 0; attempt < 6; attempt++) {
          const now = Date.now();
          const row = await db.prepare('SELECT offer, answer FROM rooms WHERE code = ? AND expires > ?')
                              .bind(code, now).first();
          if (!row) return json({ error: 'no such code' }, 404);
          const stored = roomOffers(row.offer);
          const state = roomState(row.answer);
          if (!stored || !state) return json({ error: OBSOLETE }, 409);
          const offers = stored.offers;
          /* A reconnecting guest asks for the seat it had. Honoured when that
             seat is genuinely free, ignored otherwise -- it is a preference,
             not a reservation, because the host may not have re-opened it yet
             and refusing outright would leave somebody unable to rejoin a game
             with room in it. What it buys is that a player who drops comes
             back as the same number instead of the next one. */
          const wanted = url.searchParams.get('seat');
          const prefer = wanted === null ? -1 : parseInt(wanted, 10);
          const free = function (i) {
            if (state.used[i]) return false;
            const expired = state.claims[i] && !state.answers[i] &&
                            state.claimedAt[i] < now - CLAIM_TTL_MS;
            return !state.claims[i] || expired;
          };
          /* A retry names the claim it held, and gives that seat up before
             taking a fresh one -- so retrying never strands a seat.

             This used to be keyed on the caller's address: one pending seat
             per address. A classroom, a dorm or a household is one address
             to the broker, so two friends joining the same room seconds
             apart each cancelled the other's unanswered seat, and the first
             one's answer came back "reservation expired". The claim is
             something only the guest that took the seat holds. Holding
             several seats by asking repeatedly is left to the rate limit
             and to CLAIM_TTL_MS, which hands an unanswered seat back. */
          if (release)
            for (let i = 0; i < ROOM_SLOTS; i++)
              if (state.claims[i] === release && !state.answers[i] && !state.used[i]) {
                state.claims[i] = null; state.claimedAt[i] = 0;
              }
          let slot = -1;
          if (Number.isInteger(prefer) && prefer >= 0 && prefer < ROOM_SLOTS && free(prefer))
            slot = prefer;
          else for (let i = 0; i < ROOM_SLOTS; i++) if (free(i)) { slot = i; break; }
          if (slot < 0) return json({ error: 'room full' }, 409);
          const claim = newClaim();
          state.claims[slot] = claim; state.claimedAt[slot] = now; state.answers[slot] = null;
          const next = JSON.stringify(state);
          const changed = await db.prepare('UPDATE rooms SET answer = ? WHERE code = ? AND answer = ? AND expires > ?')
                                  .bind(next, code, row.answer, now).run();
          if (changed.meta.changes > 0) return json({ offer: offers[slot], slot, claim });
        }
        return json({ error: 'room is busy, try again' }, 409);
      }

      /* --- guest posts the answer ---------------------------------------- */
      if (tail === 'answer' && request.method === 'POST') {
        const answerBody = await readBody(request);
        let incoming;
        try { incoming = JSON.parse(answerBody); } catch (e) {}
        if (!incoming || !Number.isInteger(incoming.slot) || incoming.slot < 0 ||
            incoming.slot >= ROOM_SLOTS || typeof incoming.claim !== 'string' ||
            typeof incoming.answer !== 'string' || !incoming.answer)
          return json({ error: 'bad answer' }, 400);
        for (let attempt = 0; attempt < 5; attempt++) {
          const row = await db.prepare('SELECT answer FROM rooms WHERE code = ? AND expires > ?')
                              .bind(code, Date.now()).first();
          if (!row) return json({ error: 'no such code' }, 404);
          const state = roomState(row.answer);
          if (!state || state.claims[incoming.slot] !== incoming.claim)
            return json({ error: 'reservation expired' }, 409);
          state.answers[incoming.slot] = incoming.answer;
          const next = JSON.stringify(state);
          const changed = await db.prepare('UPDATE rooms SET answer = ? WHERE code = ? AND answer = ?')
                                  .bind(next, code, row.answer).run();
          if (changed.meta.changes > 0) return empty(204);
        }
        return json({ error: 'room is busy, try again' }, 409);
      }

      /* --- host re-opens a seat -------------------------------------------

         A WebRTC connection that drops is gone for good: the description
         that built it described one moment's network conditions, and there
         is nothing to retry. The seat therefore needs a NEW offer before
         anybody can take it again, which is why this exists rather than a
         plain 'mark it free'.

         Without it, a guest who drops cannot come back into the seat they
         had -- the room hands them the next unused one instead, so the
         player who was number two returns as number three, and after three
         drops the room is full of ghosts. */
      if (tail === 'seat' && request.method === 'POST') {
        const body = await readBody(request);
        let seat;
        try { seat = JSON.parse(body); } catch (e) {}
        if (!seat || !Number.isInteger(seat.slot) || seat.slot < 0 ||
            seat.slot >= ROOM_SLOTS || typeof seat.offer !== 'string' || !seat.offer)
          return json({ error: 'bad seat' }, 400);
        for (let attempt = 0; attempt < 5; attempt++) {
          const row = await db.prepare('SELECT offer, answer FROM rooms WHERE code = ? AND expires > ?')
                              .bind(code, Date.now()).first();
          if (!row) return json({ error: 'no such code' }, 404);
          const stored = roomOffers(row.offer);
          const state = roomState(row.answer);
          if (!stored || !state) return json({ error: OBSOLETE }, 409);
          if (!(await hostAllowed(request, stored))) return json({ error: 'not the host' }, 403);
          stored.offers[seat.slot] = seat.offer;
          state.claims[seat.slot] = null; state.claimedAt[seat.slot] = 0;
          state.answers[seat.slot] = null; state.used[seat.slot] = false;
          const next = JSON.stringify(state);
          /* Both columns move together, and the seat state is the one under
             contention -- a guest may be reserving another seat in the same
             instant -- so the compare-and-swap is on it. */
          const changed = await db.prepare(
            'UPDATE rooms SET offer = ?, answer = ?, expires = ? WHERE code = ? AND answer = ?')
            .bind(JSON.stringify(stored), next,
                  Date.now() + ROOM_TTL_SECONDS * 1000, code, row.answer).run();
          if (changed.meta.changes > 0) return empty(204);
        }
        return json({ error: 'room is busy, try again' }, 409);
      }

      /* --- host waits for it ---------------------------------------------
         204 means "not yet", which is a different thing from 404 "that code
         does not exist", and the client shows different words for each. A
         single failure code here would make a mistyped code look like a slow
         friend. */
      if (tail === 'answer' && request.method === 'GET') {
        for (let attempt = 0; attempt < 5; attempt++) {
          const row = await db.prepare('SELECT offer, answer, expires FROM rooms WHERE code = ? AND expires > ?')
                              .bind(code, Date.now()).first();
          if (!row) return json({ error: 'no such code' }, 404);
          const stored = roomOffers(row.offer);
          if (!stored) return json({ error: OBSOLETE }, 409);
          if (!(await hostAllowed(request, stored))) return json({ error: 'not the host' }, 403);
          /* Only the host polls this, and it polls for as long as it is
             hosting -- so it is the honest signal that a room is still in
             use, and the room's life is extended from here rather than from
             a separate heartbeat nobody would remember to send.

             Refreshed only past the halfway mark. Rewriting the expiry on
             every poll would be a database write per second per host, for a
             value that changes nothing until it is nearly due. */
          if (typeof row.expires === 'number' &&
              row.expires - Date.now() < (ROOM_TTL_SECONDS * 1000) / 2) {
            await db.prepare('UPDATE rooms SET expires = ? WHERE code = ?')
                    .bind(Date.now() + ROOM_TTL_SECONDS * 1000, code).run();
          }
          const state = roomState(row.answer);
          if (!state) return json({ error: OBSOLETE }, 409);
          const slot = state.answers.findIndex(Boolean);
          if (slot < 0) return empty(204);
          const answer = state.answers[slot]; state.answers[slot] = null; state.used[slot] = true;
          const next = JSON.stringify(state);
          const changed = await db.prepare('UPDATE rooms SET answer = ? WHERE code = ? AND answer = ?')
                                  .bind(next, code, row.answer).run();
          if (changed.meta.changes > 0) return json({ answer, slot });
        }
        return json({ error: 'room is busy, try again' }, 409);
      }

      return json({ error: 'not found' }, 404);
    } catch (e) {
      return json({ error: 'signalling unavailable' }, 500);
    }
  }
};
