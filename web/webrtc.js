/* ============================================================================
   webrtc.js -- the browser's transport, so two tabs can play without a server.

   A tab cannot open a TCP socket, so the native game's direct-IP multiplayer
   cannot exist here. What a tab CAN do is WebRTC, which connects two peers
   directly and encrypts the link with DTLS -- and that last part is why this
   works at all. A WebSocket would have needed wss://, wss:// needs a
   certificate for the host's name, and a friend's home PC has a dynamic IP and
   no name. WebRTC sidesteps the entire web PKI because the peers authenticate
   each other rather than presenting certificates to a browser.

   --- what is deliberately absent --------------------------------------------
   There is no server here. No relay, no broker, no signalling service. The two
   blobs that WebRTC needs exchanged are handed to the player as text, and they
   send them to their friend however they were already talking. That is clumsy
   by design: the alternative is something to host, secure and keep running,
   and the whole reason this port exists is to not have one of those.

   STUN is used -- Google's public one -- but STUN is not a relay. It answers
   one question ("what does my address look like from outside?") and never sees
   a byte of the game. There is no TURN, so a pair behind strict NAT on both
   ends will fail to connect rather than falling back to a paid relay. That is
   the trade: free forever, works most of the time.

   --- why the blob is one paste and not several ------------------------------
   Normally ICE candidates trickle in over a second or two and each is sent as
   it appears. That needs a live channel between the peers, which is precisely
   what does not exist yet. So this waits for gathering to finish and folds
   everything into a single blob. It costs a few seconds before the code
   appears, and buys the player one paste instead of a conversation.
   ========================================================================== */
(function () {
  'use strict';

  var ICE = [{ urls: 'stun:stun.l.google.com:19302' }];

  /* Bytes that have arrived and not yet been read by the game. A queue of
     chunks rather than one growing buffer: the game drains whatever is there
     each frame, and concatenating on every message would make a large
     transfer quadratic. */
  var inbox = [];
  var inboxBytes = 0;

  var hostBlob = '', joinBlob = '';
  var pc = null;          // RTCPeerConnection
  var chan = null;        // RTCDataChannel
  var state = 'idle';     // idle | gathering | waiting | connecting | open | closed | failed
  var lastError = '';

  function reset() {
    try { if (chan) chan.close(); } catch (e) {}
    try { if (pc) pc.close(); } catch (e) {}
    chan = null; pc = null;
    inbox = []; inboxBytes = 0;
  }

  /* --- the blob ------------------------------------------------------------
     Gzipped where the browser has CompressionStream, because an SDP is
     verbose, repetitive text and compresses about four to one -- the
     difference between a paste that fits in a chat message and one that does
     not. Falls back to plain base64 where it does not, and the prefix says
     which so the other end never has to guess. */
  async function pack(obj) {
    var text = JSON.stringify(obj);
    var bytes = new TextEncoder().encode(text);
    if (typeof CompressionStream === 'function') {
      try {
        var cs = new CompressionStream('gzip');
        var stream = new Blob([bytes]).stream().pipeThrough(cs);
        var packed = new Uint8Array(await new Response(stream).arrayBuffer());
        return 'CLZ' + b64encode(packed);
      } catch (e) { /* fall through to plain */ }
    }
    return 'CLR' + b64encode(bytes);
  }

  async function unpack(blob) {
    var body = blob.replace(/\s+/g, '');
    var tag = body.slice(0, 3);
    var data = b64decode(body.slice(3));
    if (tag === 'CLZ') {
      var ds = new DecompressionStream('gzip');
      var stream = new Blob([data]).stream().pipeThrough(ds);
      data = new Uint8Array(await new Response(stream).arrayBuffer());
    } else if (tag !== 'CLR') {
      throw new Error('not a Cinderlift code');
    }
    return JSON.parse(new TextDecoder().decode(data));
  }

  function b64encode(bytes) {
    var s = '';
    for (var i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
    return btoa(s);
  }

  function b64decode(text) {
    var s = atob(text);
    var out = new Uint8Array(s.length);
    for (var i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
    return out;
  }

  /* Resolves once ICE has finished, so the description we hand over is
     complete. The timeout is not a nicety: a candidate type that cannot be
     gathered (no STUN reachable, say) can otherwise leave gathering pending
     indefinitely, and a player staring at a spinner deserves the partial
     answer -- host candidates alone are enough on a LAN. */
  function gathered(conn) {
    return new Promise(function (resolve) {
      if (conn.iceGatheringState === 'complete') { resolve(); return; }
      var done = false;
      function finish() {
        if (done) return;
        done = true;
        conn.removeEventListener('icegatheringstatechange', check);
        resolve();
      }
      function check() { if (conn.iceGatheringState === 'complete') finish(); }
      conn.addEventListener('icegatheringstatechange', check);
      setTimeout(finish, 4000);
    });
  }

  function wire(dc) {
    chan = dc;
    chan.binaryType = 'arraybuffer';
    chan.onopen = function () { state = 'open'; };
    chan.onclose = function () { state = 'closed'; };
    chan.onerror = function (e) { lastError = 'data channel error'; state = 'failed'; };
    chan.onmessage = function (e) {
      var bytes = new Uint8Array(e.data);
      inbox.push(bytes);
      inboxBytes += bytes.length;
    };
  }

  function makeConnection() {
    var conn = new RTCPeerConnection({ iceServers: ICE });
    conn.onconnectionstatechange = function () {
      if (conn.connectionState === 'failed') {
        /* The honest message. Without TURN this is where a strict NAT on both
           ends lands, and saying so beats a silent hang. */
        lastError = 'Could not reach the other player directly';
        state = 'failed';
      } else if (conn.connectionState === 'disconnected' ||
                 conn.connectionState === 'closed') {
        if (state === 'open') state = 'closed';
      }
    };
    return conn;
  }

  var api = {
    /* Host: build an offer and return the code to hand over. */
    host: async function () {
      reset();
      state = 'gathering';
      lastError = '';
      pc = makeConnection();
      /* Ordered and reliable, which is what the game's packet framing already
         assumes -- it was written for TCP and reads a length-prefixed stream.
         An unreliable channel would be faster for player positions and would
         also mean rewriting every packet handler to tolerate gaps. */
      wire(pc.createDataChannel('cinderlift', { ordered: true }));
      var offer = await pc.createOffer();
      await pc.setLocalDescription(offer);
      await gathered(pc);
      state = 'waiting';
      return await pack({ t: 'offer', d: pc.localDescription.sdp });
    },

    /* Guest: take the host's code, return the one to send back. */
    join: async function (code) {
      reset();
      state = 'gathering';
      lastError = '';
      var msg = await unpack(code);
      if (msg.t !== 'offer') throw new Error('that is not a host code');
      pc = makeConnection();
      pc.ondatachannel = function (e) { wire(e.channel); };
      await pc.setRemoteDescription({ type: 'offer', sdp: msg.d });
      var answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      await gathered(pc);
      state = 'connecting';
      return await pack({ t: 'answer', d: pc.localDescription.sdp });
    },

    /* Host: take the guest's reply and finish the connection. */
    accept: async function (code) {
      var msg = await unpack(code);
      if (msg.t !== 'answer') throw new Error('that is not a join code');
      if (!pc) throw new Error('not hosting');
      await pc.setRemoteDescription({ type: 'answer', sdp: msg.d });
      state = 'connecting';
    },

    state: function () { return state; },
    error: function () { return lastError; },
    isOpen: function () { return state === 'open' && chan && chan.readyState === 'open'; },

    /* Chunked because a data channel message has a practical ceiling well
       below the size of a world snapshot. The receiver does not care where the
       boundaries fall: the game's own 4-byte length prefix reassembles the
       packets, so this is free to split wherever it likes. */
    send: function (bytes) {
      if (!api.isOpen()) return false;
      var CHUNK = 16 * 1024;
      try {
        for (var at = 0; at < bytes.length; at += CHUNK)
          chan.send(bytes.subarray(at, Math.min(at + CHUNK, bytes.length)));
        return true;
      } catch (e) {
        lastError = 'send failed';
        return false;
      }
    },

    /* How many bytes are waiting, so the caller can size a read. */
    pending: function () { return inboxBytes; },

    /* Copy up to `max` bytes into the given Uint8Array view. Returns how many.
       Partial chunks are put back at the head, so a caller with a small buffer
       still makes progress rather than losing the remainder. */
    read: function (view, max) {
      var wrote = 0;
      while (inbox.length && wrote < max) {
        var head = inbox[0];
        var room = max - wrote;
        if (head.length <= room) {
          view.set(head, wrote);
          wrote += head.length;
          inbox.shift();
        } else {
          view.set(head.subarray(0, room), wrote);
          inbox[0] = head.subarray(room);
          wrote += room;
        }
      }
      inboxBytes -= wrote;
      return wrote;
    },

    close: function () { reset(); state = 'closed'; },

    /* --- the surface the game calls ---------------------------------------
       C cannot await, so the async handshake is started and then polled. Each
       of these begins work and returns immediately; the code appears in
       hostCode()/joinCode() when it is ready, and until then they are empty.
       That maps onto a game loop without threading anything through it. */
    lastFault: '',
    beginHost: function () {
      api.lastFault = '';
      hostBlob = '';
      api.host().then(function (c) { hostBlob = c; })
                .catch(function (e) { api.lastFault = e.message || 'host failed'; state = 'failed'; });
    },
    beginJoin: function (code) {
      api.lastFault = '';
      joinBlob = '';
      api.join(code).then(function (c) { joinBlob = c; })
                    .catch(function (e) { api.lastFault = e.message || 'join failed'; state = 'failed'; });
    },
    beginAccept: function (code) {
      api.lastFault = '';
      api.accept(code).catch(function (e) { api.lastFault = e.message || 'accept failed'; state = 'failed'; });
    },
    hostCode: function () { return hostBlob; },
    joinCode: function () { return joinBlob; },
    fault: function () { return api.lastFault || lastError; }
  };

  window.CinderNet = api;
})();
