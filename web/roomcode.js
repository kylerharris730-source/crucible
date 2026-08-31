/* ============================================================================
   roomcode.js -- room codes, when the broker is up.

   Named for what it does rather than for the service, deliberately: the
   worker is mounted at /signal/*, and a script called signal.js sitting one
   character away from that route is a collision waiting for somebody to
   mount it at /signal* instead. It cost an afternoon once already.

   This is a CONVENIENCE OVER A PATH THAT CANNOT BREAK, and that framing is the
   whole design. Pasting two long codes by hand needs nothing to exist and will
   work in ten years. A five-character room code needs a worker, a database and
   a domain to all be alive at the same moment.

   So the paste path is never removed, and everything here fails soft: if the
   broker is unreachable, misconfigured, or has not been deployed at all, the
   panel says so and offers the manual route. Nothing in the game depends on
   this file existing.

   What the broker buys, beyond typing less: the host does not have to wait for
   the guest before doing anything. With pasting, the two people have to be
   present and cooperating in a specific order. With a code, the host can send
   five characters and walk away.
   ========================================================================== */
(function () {
  'use strict';

  /* Same origin as the page, so no preflight and no second name to remember.
     Change this and signal/wrangler.toml's route together -- they are the only
     two places that know where the broker lives. */
  var BASE = '/signal';

  /* How long a host waits for their friend before giving up. Ten minutes
     matches the room's life on the server; a host who has walked away is
     better served by the code simply expiring than by a page that polls
     forever. */
  var WAIT_MS = 10 * 60 * 1000;
  /* Once a second. The room is a few hundred bytes and the free tier counts
     reads in the millions, so this is not where the budget goes -- but a tight
     poll would turn one idle host into thousands of requests an hour for
     nothing. */
  var POLL_MS = 1000;

  function url(path) { return BASE + path; }

  /* Every call goes through here so that "the broker is not there" is one
     answer with one shape, rather than a different exception per endpoint.
     A 404 from a route that was never deployed and a network error from a
     worker that is down should look identical to the caller: unavailable. */
  async function ask(path, options) {
    var res;
    try {
      res = await fetch(url(path), options);
    } catch (e) {
      throw new Error('unavailable');
    }
    if (res.status === 404 && path === '/room') throw new Error('unavailable');
    return res;
  }

  var api = {
    /* Has a broker been deployed at all? Answered by trying, because a config
       flag would be one more thing to get out of step with reality. */
    available: async function () {
      try {
        var res = await fetch(url('/room/PROBE'), { method: 'GET' });
        /* 404 is the RIGHT answer here: it means a worker replied and has no
           such room. A missing deployment gives a Pages 404 page, which is
           HTML rather than JSON -- so the body is what distinguishes them. */
        if (!res.ok && res.status !== 404) return false;
        var text = await res.text();
        return text.charAt(0) === '{';
      } catch (e) { return false; }
    },

    /* Host: hand over a description, get a code to read out. */
    open: async function (offer) {
      var res = await ask('/room', { method: 'POST', body: offer });
      if (!res.ok) throw new Error('could not open a room');
      var body = await res.json();
      if (!body.code) throw new Error('could not open a room');
      return body.code;
    },

    /* Guest: exchange a code for the host's description. */
    fetchOffer: async function (code) {
      var res = await ask('/room/' + encodeURIComponent(code), { method: 'GET' });
      if (res.status === 404) throw new Error('no game with that code');
      if (!res.ok) throw new Error('could not read that code');
      return (await res.json()).offer;
    },

    /* Guest: send the reply back. */
    postAnswer: async function (code, answer) {
      var res = await ask('/room/' + encodeURIComponent(code) + '/answer',
                          { method: 'POST', body: answer });
      if (res.status === 404) throw new Error('that code expired');
      if (!res.ok) throw new Error('could not send the reply');
    },

    /* Host: wait for it. Resolves with the answer, or null if nobody came.
       `cancelled` lets the panel stop the poll when it closes, so a shut
       dialog does not keep talking to the network. */
    waitForAnswer: async function (code, cancelled) {
      var until = Date.now() + WAIT_MS;
      while (Date.now() < until) {
        if (cancelled && cancelled()) return null;
        var res = await ask('/room/' + encodeURIComponent(code) + '/answer', { method: 'GET' });
        if (res.status === 200) return (await res.json()).answer;
        if (res.status === 404) throw new Error('that code expired');
        /* 204: nobody has answered yet. */
        await new Promise(function (r) { setTimeout(r, POLL_MS); });
      }
      return null;
    }
  };

  window.CinderSignal = api;
})();
