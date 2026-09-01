/* ============================================================================
   multiplayer.js -- the signalling panel.

   Why this is HTML and not part of the game's own menu: a player has to COPY a
   code out and PASTE one back in, and a canvas has no clipboard, no text
   selection and no context menu. Building it inside the game would mean
   reimplementing a textarea out of pixels.

   It also keeps every browser-only wart out of main.cpp, which is the same
   bargain the rest of this port makes.

   The flow is three steps and it is worth being blunt about which ones the
   player has to do by hand, because there is no server to do them:

     host   presses Host, waits for a code, sends it to their friend
     guest  pastes it, gets a code back, sends that to the host
     host   pastes that, and they are connected

   Two pastes. That is the price of having nothing to run.
   ========================================================================== */
(function () {
  'use strict';

  var el = null, poll = 0;
  /* Lives beyond the overlay. Closing the panel must not close the room after
     player one joins; the other two guests still use the same code. */
  var hostSession = null;

  function call(name, sig, args) {
    try { return Module.ccall(name, sig[0], sig.slice(1), args || []); }
    catch (e) { return null; }
  }

  function css() {
    var s = document.createElement('style');
    s.textContent = [
      '#mp{position:fixed;inset:0;background:rgba(8,9,13,.82);z-index:60;',
      '   display:flex;align-items:center;justify-content:center;font:13px/1.55 ui-monospace,Consolas,monospace}',
      '#mp .box{background:#161922;border:1px solid #2f3646;max-width:560px;width:calc(100% - 32px);padding:18px 20px;color:#c8cedb}',
      '#mp h2{margin:0 0 2px;font-size:14px;letter-spacing:.09em;color:#e8a24a}',
      '#mp p{margin:0 0 14px;color:#7e8696}',
      '#mp .step{border-top:1px solid #262c39;padding-top:12px;margin-top:12px}',
      '#mp .step b{color:#8fb3d8;font-weight:normal}',
      '#mp textarea{width:100%;height:60px;background:#0d0f15;color:#c8cedb;border:1px solid #2f3646;',
      '   font:11px/1.35 ui-monospace,monospace;padding:6px;box-sizing:border-box;margin-top:6px;resize:vertical}',
      '#mp button{background:#2a3140;color:#dfe6f2;border:1px solid #3c4557;padding:6px 13px;cursor:pointer;margin:8px 6px 0 0}',
      '#mp button:hover{background:#39435a}',
      '#mp button.go{border-color:#7a5a2a;color:#f0c98a}',
      '#mp .stat{margin-top:12px;color:#e8a24a;min-height:1.4em}',
      '#mp .warn{color:#c98b6b;margin-top:10px}',
      '#mp .x{float:right;color:#5d6575;cursor:pointer;padding:0 4px}',
      '#mp .code{font-size:34px;letter-spacing:.22em;color:#f0c98a;text-align:center;',
      '   padding:14px 0 6px;font-weight:700}',
      '#mp .alt{color:#5d6575;font-size:12px;margin-top:14px;cursor:pointer;text-decoration:underline}'
    ].join('');
    document.head.appendChild(s);
  }

  function shell(title, lead, body) {
    el.innerHTML = '<div class="box"><span class="x" id="mpx">close</span>' +
                   '<h2>' + title + '</h2><p>' + lead + '</p>' + body + '</div>';
    document.getElementById('mpx').onclick = close;
  }

  function close() {
    if (poll) { clearInterval(poll); poll = 0; }
    if (el) { el.remove(); el = null; }
    var c = document.getElementById('canvas');
    if (c) c.focus();
  }

  function open() {
    if (el) return;
    el = document.createElement('div');
    el.id = 'mp';
    /* SDL listens for keyboard events on <body> so the canvas keeps working
       after any click around the game.  That also means events from these real
       HTML inputs bubble into SDL, whose Emscripten handler prevents their
       default action; the field is focused but the browser never inserts a
       character.  Stop only the overlay's keyboard events before they reach
       body.  Do not preventDefault: text editing, paste, shortcuts and Enter
       still belong to the input itself.  Keyup is included so SDL can never
       see one half of a press made in the panel. */
    ['keydown', 'keyup', 'keypress'].forEach(function (type) {
      el.addEventListener(type, function (e) { e.stopPropagation(); });
    });
    document.body.appendChild(el);
    menu();
  }

  /* Whether the broker answered, checked once per panel opening. Not cached
     across openings: a worker that was down when the game loaded may be up
     by the time somebody actually wants to play. */
  var broker = null;
  var guestSession = null;
  /* Which seat this client last held, so a reconnection can ask for it
     back rather than being handed the next free one. */
  var lastSeat = null;

  function menu() {
    shell('MULTIPLAYER',
      'Up to four players, connected directly to the host. There is no server ' +
      'in the middle of your game.',
      '<button class="go" id="mph">Host a game</button>' +
      '<button id="mpj">Join a game</button>' +
      '<div class="warn">Every player needs the same version of the game. ' +
      'Some strict networks will not allow a direct connection at all, and ' +
      'there is no relay to fall back on &mdash; if it will not connect, the ' +
      'Windows build can host by IP instead.</div>');
    document.getElementById('mph').onclick = function () { start(host, hostPaste); };
    document.getElementById('mpj').onclick = function () { start(guest, guestPaste); };
  }

  /* Ask the broker once, then take the short path or the one that cannot
     break. The manual route is not a degraded mode to apologise for -- it is
     the floor everything else is built on, and it is always reachable from
     the link at the bottom of either screen. */
  function start(viaCode, viaPaste) {
    shell('MULTIPLAYER', 'Checking for a room service...', '');
    var go = function () { (broker ? viaCode : viaPaste)(); };
    if (broker !== null) { go(); return; }
    if (!window.CinderSignal) { broker = false; go(); return; }
    CinderSignal.available().then(function (ok) { broker = ok; go(); });
  }

  function altLink(label, fn) {
    var d = document.createElement('div');
    d.className = 'alt';
    d.textContent = label;
    d.onclick = fn;
    el.querySelector('.box').appendChild(d);
  }

  function copyBtnFor(id) {
    return function () {
      var t = document.getElementById(id);
      t.select();
      try { navigator.clipboard.writeText(t.value); } catch (e) { document.execCommand('copy'); }
      this.textContent = 'copied';
      var b = this;
      setTimeout(function () { b.textContent = 'copy'; }, 1200);
    };
  }

  function hostPaste() {
    call('webMpHost', ['null']);
    var slot = 0;
    shell('HOSTING',
      'Connect up to three friends, one reply at a time.',
      '<div class="step"><b id="mpslot">Guest 1 of 3 &mdash; your code</b>' +
      '<textarea id="mpo" readonly placeholder="building the code, a moment..."></textarea>' +
      '<button id="mpc">copy</button></div>' +
      '<div class="step"><b>2 &mdash; their reply</b>' +
      '<textarea id="mpa" placeholder="paste your friend\'s code here"></textarea>' +
      '<button class="go" id="mpk">Connect</button></div>' +
      '<div class="stat" id="mps"></div>');
    document.getElementById('mpc').onclick = copyBtnFor('mpo');
    document.getElementById('mpk').onclick = function () {
      var v = document.getElementById('mpa').value.trim();
      if (!v || slot >= 3) return;
      CinderNet.beginAccept(slot, v);
      slot++;
      document.getElementById('mpa').value = '';
      document.getElementById('mpo').value = slot < 3 ? CinderNet.hostCode(slot) : '';
      document.getElementById('mpslot').textContent = slot < 3 ?
        ('Guest ' + (slot + 1) + ' of 3 — your code') : 'All three guest replies accepted';
    };
    watch(function () {
      var t = document.getElementById('mpo');
      if (t && !t.value && slot < 3) t.value = CinderNet.hostCode(slot);
    }, false);
  }

  function guestPaste() {
    shell('JOINING',
      'Paste the code your friend sent you, then send them the one you get back.',
      '<div class="step"><b>1 &mdash; their code</b>' +
      '<textarea id="mpi" placeholder="paste the host\'s code here"></textarea>' +
      '<button class="go" id="mpg">Go</button></div>' +
      '<div class="step"><b>2 &mdash; your reply</b>' +
      '<textarea id="mpo" readonly placeholder="appears once you press Go"></textarea>' +
      '<button id="mpc">copy</button></div>' +
      '<div class="stat" id="mps"></div>');
    document.getElementById('mpc').onclick = copyBtnFor('mpo');
    document.getElementById('mpg').onclick = function () {
      var v = document.getElementById('mpi').value.trim();
      if (!v) return;
      /* The game starts the join, which starts the transport: going through
         the game rather than round it is what sets the network role, and a
         connection with no role attached would carry packets nobody applies. */
      call('webMpJoin', ['null', 'string'], [v]);
    };
    watch(function () {
      var t = document.getElementById('mpo');
      if (t && !t.value) t.value = CinderNet.joinCode();
    });
  }

  /* --- the short path ---------------------------------------------------
     Same transport, same two descriptions; the broker just carries them so
     nobody has to. It holds them for minutes and never sees the game. */
  function host() {
    if (hostSession && hostSession.active) { showHostSession(); return; }
    call('webMpHost', ['null']);
    shell('HOSTING',
      'Give this same code to as many as three friends. Nothing else to do.',
      '<div class="code" id="mpcode">&middot; &middot; &middot;</div>' +
      '<div class="stat" id="mps">making a code...</div>');
    altLink('or exchange codes by hand instead', hostPaste);

    (async function () {
      try {
        var offers = [];
        for (var i = 0; i < 150 && offers.length < 3; i++) {
          offers = CinderNet.hostCodes().filter(Boolean);
          if (offers.length < 3) await new Promise(function (r) { setTimeout(r, 100); });
        }
        if (offers.length !== 3) throw new Error('could not build all three host connections');
        var code = await CinderSignal.open(CinderNet.hostCodes());
        hostSession = { code:code, active:true, accepted:0, error:'' };
        var box = document.getElementById('mpcode');
        if (box) box.textContent = code;
        setStatus('0/3 guests joined — room stays open in the background');
        watch(function () { return refreshHostStatus(); }, false);
        superviseHost();
      } catch (e) {
        if (hostSession) { hostSession.error=e.message; hostSession.active=false; refreshHostStatus(); }
        else fallback(e.message, hostPaste);
      }
    })();
  }

  /* Keep the room truthful for as long as the game runs.

     The first version of this stopped once three guests had been accepted,
     which was fine right up until somebody dropped -- and somebody did,
     from New York, within minutes. Their seat stayed marked used, so when
     they re-entered the same code the room handed them the NEXT one and
     they came back as player three. Three drops and a room is full of
     players who are not there.

     So this runs for the whole session and does two things: it accepts
     answers as they arrive, and it notices a link that was open and is not
     any more, builds a fresh offer for that exact seat, and publishes it.
     A dropped WebRTC link cannot be resumed -- the description that made it
     described one moment's network -- so re-offering is the only way back,
     and doing it per seat is what returns a player to their own number.

     It is detached from the panel on purpose. The player closes that as
     soon as they have read the code, and a room that stopped healing at
     that moment would be a room that only works while somebody is
     watching it. */
  function superviseHost() {
    var wasOpen = [false, false, false];

    (async function acceptLoop() {
      while (hostSession && hostSession.active) {
        try {
          var reply = await CinderSignal.waitForAnswer(
            hostSession.code, function () { return !hostSession || !hostSession.active; });
          if (!hostSession || !hostSession.active) return;
          if (reply) {
            CinderNet.beginAccept(reply.slot, reply.answer);
            refreshHostStatus();
          }
          /* A null reply is the poll timing out with nobody there, which is
             the ordinary state of a room waiting for friends -- not a
             reason to stop hosting. */
        } catch (e) {
          /* The room really is gone (expired, or the service is down). Stop
             claiming to host something that no longer exists. */
          hostSession.error = e.message;
          hostSession.active = false;
          refreshHostStatus();
          return;
        }
      }
    })();

    (async function seatLoop() {
      while (hostSession && hostSession.active) {
        await new Promise(function (r) { setTimeout(r, 1500); });
        if (!hostSession || !hostSession.active) return;
        for (var slot = 0; slot < 3; slot++) {
          var state = CinderNet.state(slot);
          if (state === 'open') { wasOpen[slot] = true; continue; }
          /* Only a seat that HELD somebody needs re-offering. One that has
             never been used still has its original offer sitting in the
             room, waiting, and replacing that would invalidate a code a
             friend is in the middle of typing. */
          if (!wasOpen[slot]) continue;
          if (linkLive(state)) continue;
          wasOpen[slot] = false;
          reopen(slot);
        }
      }
    })();
  }

  async function reopen(slot) {
    try {
      var offer = await CinderNet.host(slot);
      if (!hostSession || !hostSession.active) return;
      await CinderSignal.reopenSeat(hostSession.code, slot, offer);
      refreshHostStatus();
    } catch (e) {
      /* Left for the next sweep rather than escalated: a seat that failed to
         re-open is a seat nobody can take, which is the same as the state it
         was already in. Nothing else is harmed by trying again in a moment. */
    }
  }

  function showHostSession() {
    shell('HOSTING', 'The room keeps accepting guests after this panel closes.',
      '<div class="code" id="mpcode">' + hostSession.code + '</div>' +
      '<div class="stat" id="mps"></div>');
    refreshHostStatus();
    watch(function () { return refreshHostStatus(); }, false);
  }

  function refreshHostStatus() {
    if (!hostSession) return;
    var peers = call('webMpPeerCount', ['number']);
    var text = peers + '/3 guests connected';
    if (peers < 3 && hostSession.active) text += ' — room stays open in the background';
    else if (peers >= 3) text += ' — game full';
    if (hostSession.error) text = hostSession.error;
    setStatus(text);
    return text;
  }

  function guest() {
    shell('JOINING',
      'Type the code your friend gave you.',
      '<input id="mpin" maxlength="5" placeholder="ABC12" ' +
      'style="width:100%;text-align:center;font:700 30px/1.4 ui-monospace,monospace;' +
      'letter-spacing:.22em;background:#0d0f15;color:#f0c98a;border:1px solid #2f3646;' +
      'padding:8px;box-sizing:border-box;text-transform:uppercase">' +
      '<button class="go" id="mpg">Join</button>' +
      '<div class="stat" id="mps"></div>');
    altLink('or exchange codes by hand instead', guestPaste);
    var input = document.getElementById('mpin');
    input.focus();
    var go = function () {
      var code = input.value.trim().toUpperCase();
      if (code.length < 3) return;
      setStatus('looking for that game...');
      (async function () {
        try {
          await joinOnce(code);
          watch(function () {});
          superviseGuest(code);
        } catch (e) {
          setStatus(e.message);
        }
      })();
    };
    document.getElementById('mpg').onclick = go;
    input.onkeydown = function (e) { if (e.key === 'Enter') go(); };
  }

  /* A link is either making progress or it is gone. Listing the states that
     mean "gone" is the wrong way round -- it was written that way first, and
     a reset link reporting 'idle' slipped straight through the gap. Listing
     the ones that mean "still working" cannot miss a new state; at worst it
     reconnects something that was about to connect anyway. */
  function linkLive(state) {
    return state === 'open' || state === 'gathering' ||
           state === 'connecting' || state === 'waiting';
  }

  /* One attempt at joining, so that the first one and every reconnection
     afterwards are the same code rather than two versions that drift. */
  async function joinOnce(code, preferSeat) {
    var reservation = await CinderSignal.fetchOffer(code, preferSeat);
    lastSeat = reservation.slot;
    call('webMpJoin', ['null', 'string'], [reservation.offer]);
    var answer = '';
    for (var i = 0; i < 150 && !answer; i++) {
      answer = CinderNet.joinCode();
      if (!answer) await new Promise(function (r) { setTimeout(r, 100); });
    }
    if (!answer) throw new Error('could not build a join code');
    await CinderSignal.postAnswer(code, reservation, answer);
    return reservation;
  }

  /* Come back after a drop.

     What happened without this is worse than it sounds: the connection went,
     and the guest carried on walking around a world nobody else could see.
     They still had the terrain, because a client simulates locally and the
     host is only a correction -- so nothing looked wrong. They were playing
     alone and had no way to know.

     So a drop is now something the page acts on. It waits for the host to
     re-open the seat (which its own supervisor does within a couple of
     seconds) and walks back in on the same code. The player is told, because
     a silent reconnection that fails looks identical to one that never
     started.

     Bounded. If the host has closed the game there is nothing to come back
     to, and retrying forever would be a tab quietly talking to a room that
     stopped existing an hour ago. */
  function superviseGuest(code) {
    if (guestSession) guestSession.active = false;
    guestSession = { code: code, active: true };
    var session = guestSession;

    (async function () {
      var everOpen = false;
      while (session.active) {
        await new Promise(function (r) { setTimeout(r, 1000); });
        if (!session.active) return;
        var state = CinderNet.state(0);
        if (state === 'open') { everOpen = true; continue; }
        if (!everOpen) continue;                 /* never connected: not a drop */
        if (linkLive(state)) continue;

        everOpen = false;
        for (var attempt = 1; attempt <= 6 && session.active; attempt++) {
          announce('Connection lost — rejoining (' + attempt + '/6)');
          /* The host needs a moment to notice and publish a fresh offer for
             the seat. Backing off also keeps six attempts from being spent
             in six seconds on a host that is simply gone. */
          await new Promise(function (r) { setTimeout(r, attempt * 2000); });
          if (!session.active) return;
          try {
            await joinOnce(code, lastSeat);
            for (var i = 0; i < 150 && CinderNet.state(0) !== 'open'; i++)
              await new Promise(function (r) { setTimeout(r, 100); });
            if (CinderNet.state(0) === 'open') { everOpen = true; announce(''); break; }
          } catch (e) { /* room gone or full for now; the next attempt says so */ }
          if (attempt === 6) announce('Could not rejoin — the game may have ended');
        }
      }
    })();
  }

  /* Reconnection happens with the panel shut, so it needs somewhere to speak
     that is not the panel. Reuses the strip under the game rather than
     inventing a second place for the game to talk to you. */
  function announce(text) {
    var strip = document.getElementById('controls');
    if (!strip) return;
    var slot = document.getElementById('mpnote');
    if (!slot) {
      slot = document.createElement('span');
      slot.id = 'mpnote';
      slot.style.cssText = 'margin-left:12px;color:#e8a24a';
      strip.appendChild(slot);
    }
    slot.textContent = text;
    setStatus(text);
  }

  function setStatus(text) {
    var s = document.getElementById('mps');
    if (s) s.textContent = text;
  }

  /* The broker let us down mid-flow. Say so plainly and hand over the route
     that needs nothing, rather than leaving somebody on a dead screen. */
  function fallback(why, then) {
    setStatus((why || 'the room service is unavailable') + ' -- swapping codes by hand instead');
    setTimeout(then, 1400);
  }

  /* One timer for the whole panel: fills in codes as they are generated and
     reports what the game and the transport each think is happening. They can
     disagree -- an open channel with a handshake still in flight is normal for
     a moment -- so both are shown rather than one being guessed from the
     other. */
  function watch(step, closeWhenReady) {
    if (closeWhenReady === undefined) closeWhenReady = true;
    if (poll) clearInterval(poll);
    poll = setInterval(function () {
      var custom = step();
      var s = document.getElementById('mps');
      if (!s) return;
      var fault = CinderNet.fault();
      var link = CinderNet.state();
      var game = call('webMpStatus', ['string']) || '';
      s.textContent = fault ? ('Problem: ' + fault) :
        (custom !== undefined ? custom : (link + ' — ' + game));
      if (closeWhenReady && call('webMpReady', ['number'])) {
        s.textContent = 'Connected. Closing this panel.';
        setTimeout(close, 900);
      }
    }, 400);
  }

  window.CinderMultiplayer = { open: open, close: close };

  document.addEventListener('DOMContentLoaded', function () {
    css();
    var strip = document.getElementById('controls');
    if (!strip) return;
    var b = document.createElement('button');
    b.textContent = 'Multiplayer';
    b.style.cssText = 'background:#2a3140;color:#dfe6f2;border:1px solid #3c4557;' +
                      'padding:3px 10px;cursor:pointer;margin-left:10px;font:inherit';
    b.onclick = open;
    strip.appendChild(b);
  });
})();
