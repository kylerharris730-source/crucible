# The signalling broker

A mailbox that lets two players find each other. It carries about two kilobytes,
once, at the moment they connect — then it is finished. **It never sees a byte of
the game.**

The game works without it. If this is not deployed, the Multiplayer panel falls
back to players pasting two codes to each other by hand, which needs nothing to
exist at all. That is the floor; this is a convenience on top of it.

## Why it exists

WebRTC needs two descriptions exchanged before a connection can start, and there
is no channel between the players yet to exchange them over. Without a broker,
the players do it themselves — the host copies a 800-character code to a friend,
who copies one back. It works, and it is tedious.

With this, that becomes: **host gets `JXSZT`, friend types `JXSZT`.**

It also allows candidates to be exchanged as they are discovered rather than all
at once, which makes connections more reliable, not only shorter to set up.

## Cost

Free, and it stays free. The load is proportional to *joins*, not to playtime or
player count — a relay's bill grows with every hour played, this one does not
grow at all in any way you will notice. Cloudflare's free tier covers far more
games per day than this will see.

## Deploying

You need a Cloudflare account with the domain on it, which you have for DNS.

```bash
cd signal
npm install -g wrangler        # once
wrangler login                 # once

wrangler d1 create cinderlift-signal
```

That prints a `database_id` — put it in `wrangler.toml`. Then create the table
and deploy:

```bash
wrangler d1 execute cinderlift-signal --remote --file=schema.sql
wrangler deploy
```

### Where it gets served from

The obvious choice — a path like `cinderlift.com/signal/*` — **does not work
here**, and it fails silently, so it is worth understanding before you try it.

A worker route only fires if Cloudflare is *proxying* the domain (the orange
cloud). `cinderlift.com` is DNS-only, which is the normal way to point a domain
at GitHub Pages: Pages answers directly and Cloudflare never sees the request.
A route on a domain Cloudflare never sees cannot run. The deploy succeeds, the
route is listed, and every request still lands on GitHub Pages.

You could turn the proxy on. That also puts *every* request for the whole site
through a path it has never been served over, in exchange for a nicer URL on one
endpoint — and if SSL/TLS mode is Flexible rather than Full, a proxied Pages site
redirect-loops. Not worth it for this.

So the worker gets **its own hostname**. Register a workers.dev subdomain once:

    https://dash.cloudflare.com/<your-account-id>/workers/onboarding

Then `wrangler deploy` prints a URL like
`https://cinderlift-signal.<subdomain>.workers.dev`. Put that in `BASE` at the
top of `web/roomcode.js`, rebuild the site, and the game will start using it.

Cross-origin is fine: the calls are GETs and `text/plain` POSTs, which are
"simple requests" and do not even trigger a preflight.

### Checking it

```bash
curl https://<your-worker-url>/room/PROBE
```

`{"error":"no such code"}` is correct — the worker is alive and has no room by
that name. HTML means you are still hitting Pages and the game will quietly keep
using the paste path.

## What it stores, and for how long

One row per room: a five-character code, the two descriptions, and an expiry.
Rooms live **ten minutes** and are swept whenever the next one is created, so the
steady state of the database is close to empty. A delivered answer deletes its
room immediately.

A WebRTC description contains IP addresses — that is what makes a direct
connection possible — so this briefly holds something mildly personal. The short
life is the mitigation, and it is why nothing is logged. Note that the two
players learn each other's addresses regardless the moment they connect: that is
inherent to having no relay, and no different from the direct-IP multiplayer the
Windows build has always had.

## Why D1 and not KV

KV is the obvious first reach and it is wrong here. KV is *eventually* consistent
across Cloudflare's edge: a host in one city writes a room, a guest in another
reads it moments later, and can legitimately get nothing back. For a cache that
is fine. For a handshake it is a coin flip — and the failure looks exactly like
"the code my friend sent me does not work", which is unreproducible and
impossible for a player to diagnose.

D1 has a single primary, so a write is visible to the next read. That is worth
ten lines of schema.
