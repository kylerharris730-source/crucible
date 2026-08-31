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

  function menu() {
    shell('MULTIPLAYER',
      'Two players, connected directly to each other. There is no server in ' +
      'the middle of your game.',
      '<button class="go" id="mph">Host a game</button>' +
      '<button id="mpj">Join a game</button>' +
      '<div class="warn">Both of you need the same version of the game. ' +
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
    shell('HOSTING',
      'Send step 1 to your friend. They send you a code back; paste it into step 2.',
      '<div class="step"><b>1 &mdash; your code</b>' +
      '<textarea id="mpo" readonly placeholder="building the code, a moment..."></textarea>' +
      '<button id="mpc">copy</button></div>' +
      '<div class="step"><b>2 &mdash; their reply</b>' +
      '<textarea id="mpa" placeholder="paste your friend\'s code here"></textarea>' +
      '<button class="go" id="mpk">Connect</button></div>' +
      '<div class="stat" id="mps"></div>');
    document.getElementById('mpc').onclick = copyBtnFor('mpo');
    document.getElementById('mpk').onclick = function () {
      var v = document.getElementById('mpa').value.trim();
      if (v) CinderNet.beginAccept(v);
    };
    watch(function () {
      var t = document.getElementById('mpo');
      if (t && !t.value) t.value = CinderNet.hostCode();
    });
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
    call('webMpHost', ['null']);
    shell('HOSTING',
      'Give your friend this code. Nothing else to do.',
      '<div class="code" id="mpcode">&middot; &middot; &middot;</div>' +
      '<div class="stat" id="mps">making a code...</div>');
    altLink('or exchange codes by hand instead', hostPaste);

    var dead = false;
    var stop = function () { dead = true; };
    var previous = close;
    close = function () { stop(); previous(); close = previous; };

    (async function () {
      try {
        var offer = '';
        for (var i = 0; i < 150 && !offer && !dead; i++) {
          offer = CinderNet.hostCode();
          if (!offer) await new Promise(function (r) { setTimeout(r, 100); });
        }
        if (dead) return;
        if (!offer) throw new Error('could not build a host code');
        var code = await CinderSignal.open(offer);
        if (dead) return;
        var box = document.getElementById('mpcode');
        if (box) box.textContent = code;
        setStatus('waiting for your friend to join');
        var answer = await CinderSignal.waitForAnswer(code, function () { return dead; });
        if (dead) return;
        if (!answer) { setStatus('nobody joined -- the code has expired'); return; }
        CinderNet.beginAccept(answer);
        watch(function () {});
      } catch (e) {
        if (!dead) fallback(e.message, hostPaste);
      }
    })();
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
          var offer = await CinderSignal.fetchOffer(code);
          call('webMpJoin', ['null', 'string'], [offer]);
          setStatus('connecting...');
          var answer = '';
          for (var i = 0; i < 150 && !answer; i++) {
            answer = CinderNet.joinCode();
            if (!answer) await new Promise(function (r) { setTimeout(r, 100); });
          }
          if (!answer) throw new Error('could not build a join code');
          await CinderSignal.postAnswer(code, answer);
          watch(function () {});
        } catch (e) {
          setStatus(e.message);
        }
      })();
    };
    document.getElementById('mpg').onclick = go;
    input.onkeydown = function (e) { if (e.key === 'Enter') go(); };
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
  function watch(step) {
    if (poll) clearInterval(poll);
    poll = setInterval(function () {
      step();
      var s = document.getElementById('mps');
      if (!s) return;
      var fault = CinderNet.fault();
      var link = CinderNet.state();
      var game = call('webMpStatus', ['string']) || '';
      s.textContent = fault ? ('Problem: ' + fault) : (link + ' — ' + game);
      if (call('webMpReady', ['number'])) {
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
