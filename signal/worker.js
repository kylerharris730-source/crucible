/* ============================================================================
   worker.js -- the signalling broker. A mailbox, not a relay.

   Two browsers cannot start a WebRTC connection without first exchanging two
   descriptions, and there is no channel between them yet to exchange over.
   That is the whole problem this solves, and it is worth being precise about
   how small it is: this carries about two kilobytes, once, at the moment two
   people connect. Then it is finished. It never sees a byte of the game.

   The distinction from a relay is the entire point. A relay carries every byte
   of every session and its bill grows with playtime. This grows with JOINS,
   which for a game played by friends is a rounding error, and it stays inside
   a free tier essentially forever.

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
const CODE_LEN = 5;                /* 31^5, about 28.6 million */
const ROOM_SLOTS = 3;
/* A guest which reserves an offer but vanishes must not consume a seat for the
   room's full ten-minute life. ICE normally answers within four seconds; a
   minute and a half leaves ample room for a slow tab without making a typo a
   permanent vacancy. */
const CLAIM_TTL_MS = 90 * 1000;

function newCode() {
  const bytes = new Uint8Array(CODE_LEN);
  crypto.getRandomValues(bytes);
  let out = '';
  for (let i = 0; i < CODE_LEN; i++) out += ALPHABET[bytes[i] % ALPHABET.length];
  return out;
}

function newClaim() {
  const bytes = new Uint8Array(12);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
}

function roomState(text) {
  if (!text) return { claims:[null,null,null], claimedAt:[0,0,0],
                      answers:[null,null,null], used:[false,false,false] };
  try {
    const state = JSON.parse(text);
    if (state.claims.length === ROOM_SLOTS && state.claimedAt.length === ROOM_SLOTS &&
        state.answers.length === ROOM_SLOTS && state.used.length === ROOM_SLOTS) return state;
  } catch (e) {}
  return null;
}

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET,POST,OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
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
        const offer = await readBody(request);
        let offers;
        try { offers = JSON.parse(offer).offers; } catch (e) {}
        const multi = Array.isArray(offers) && offers.length === ROOM_SLOTS &&
                      !offers.some(o => typeof o !== 'string' || !o);
        /* Keep accepting the previous single-offer client during rollout. The
           Worker and GitHub Pages cannot change atomically, and either deploy
           order would otherwise break every room for the few minutes between
           them. Legacy rooms retain their old plain strings and old lifecycle. */
        if (!multi && (!offer || (offer.slice(0,3) !== 'CLZ' && offer.slice(0,3) !== 'CLR')))
          return json({ error: 'bad offers' }, 400);
        const storedOffer = multi ? JSON.stringify({ offers }) : offer;
        const state = multi ? JSON.stringify(roomState(null)) : null;
        await sweep(db);
        /* Retried rather than trusted: 28 million codes and a ten minute life
           make a collision vanishingly unlikely, but "vanishingly" is not
           "never" and the cost of handling it is three lines. */
        for (let attempt = 0; attempt < 5; attempt++) {
          const room = newCode();
          const result = await db
            .prepare('INSERT OR IGNORE INTO rooms (code, offer, answer, expires) VALUES (?, ?, ?, ?)')
            .bind(room, storedOffer, state, Date.now() + ROOM_TTL_SECONDS * 1000)
            .run();
          if (result.meta.changes > 0) return json({ code: room });
        }
        return json({ error: 'could not allocate a code' }, 503);
      }

      if (!code) return json({ error: 'not found' }, 404);

      /* --- guest collects the offer -------------------------------------- */
      if (!tail && request.method === 'GET') {
        /* Optimistic compare-and-swap makes reservation atomic without a D1
           transaction. Simultaneous guests may both read slot zero, but only
           one can replace the exact state string; the loser retries and takes
           slot one. */
        for (let attempt = 0; attempt < 6; attempt++) {
          const now = Date.now();
          const row = await db.prepare('SELECT offer, answer FROM rooms WHERE code = ? AND expires > ?')
                              .bind(code, now).first();
          if (!row) return json({ error: 'no such code' }, 404);
          let offers;
          try { offers = JSON.parse(row.offer).offers; } catch (e) {}
          if (!Array.isArray(offers)) return json({ offer: row.offer });
          const state = roomState(row.answer);
          if (!state || !Array.isArray(offers) || offers.length !== ROOM_SLOTS)
            return json({ error: 'room format is obsolete' }, 409);
          let slot = -1;
          for (let i = 0; i < ROOM_SLOTS; i++) {
            if (state.used[i]) continue;
            const expired = state.claims[i] && !state.answers[i] && state.claimedAt[i] < now - CLAIM_TTL_MS;
            if (!state.claims[i] || expired) { slot = i; break; }
          }
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
        /* Legacy answer: same plain packed SDP as before.

           Guarded on the ROOM being legacy too, which is the case that bites
           during a rollout rather than in steady state. A browser holding a
           cached copy of the old page reads `offer` out of a new room's reply
           and ignores the `slot` and `claim` beside it, then posts a bare
           answer -- and an unguarded UPDATE here would overwrite the room's
           seat state with that string, evicting every other guest and leaving
           the host polling a room it can no longer parse. One player with a
           stale tab could break a game for three. Refuse instead, and say the
           one thing that fixes it. */
        if (!incoming || !Number.isInteger(incoming.slot)) {
          if (!answerBody || (answerBody.slice(0,3) !== 'CLZ' && answerBody.slice(0,3) !== 'CLR'))
            return json({ error: 'bad answer' }, 400);
          const existing = await db.prepare('SELECT offer FROM rooms WHERE code = ? AND expires > ?')
                                   .bind(code, Date.now()).first();
          if (!existing) return json({ error: 'no such code' }, 404);
          let multiOffers;
          try { multiOffers = JSON.parse(existing.offer).offers; } catch (e) {}
          if (Array.isArray(multiOffers))
            return json({ error: 'this game needs a newer page -- reload and try again' }, 409);
          const result = await db.prepare('UPDATE rooms SET answer = ? WHERE code = ? AND expires > ?')
                                 .bind(answerBody, code, Date.now()).run();
          if (result.meta.changes === 0) return json({ error: 'no such code' }, 404);
          return empty(204);
        }
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

      /* --- host waits for it ---------------------------------------------
         204 means "not yet", which is a different thing from 404 "that code
         does not exist", and the client shows different words for each. A
         single failure code here would make a mistyped code look like a slow
         friend. */
      if (tail === 'answer' && request.method === 'GET') {
        for (let attempt = 0; attempt < 5; attempt++) {
          const row = await db.prepare('SELECT offer, answer FROM rooms WHERE code = ? AND expires > ?')
                              .bind(code, Date.now()).first();
          if (!row) return json({ error: 'no such code' }, 404);
          let legacy = true;
          try { legacy = !Array.isArray(JSON.parse(row.offer).offers); } catch (e) {}
          if (legacy) {
            if (!row.answer) return empty(204);
            await db.prepare('DELETE FROM rooms WHERE code = ?').bind(code).run();
            return json({ answer: row.answer });
          }
          const state = roomState(row.answer);
          if (!state) return json({ error: 'room format is obsolete' }, 409);
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
