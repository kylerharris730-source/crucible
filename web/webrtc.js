/* Browser WebRTC transport. The host owns three independent links (one per
   guest); a guest owns link zero. C++ sees these as ordinary peer sockets. */
(function () {
  'use strict';
  var ICE = [{ urls: 'stun:stun.l.google.com:19302' }];
  var MAX_LINKS = 3, role = 'idle';
  var hostBlobs = ['', '', ''], joinBlob = '';
  function fresh() { return { pc:null, chan:null, inbox:[], inboxBytes:0, state:'idle', error:'' }; }
  var links = [fresh(), fresh(), fresh()];
  function valid(slot) { return slot >= 0 && slot < MAX_LINKS; }
  function resetLink(slot) {
    if (!valid(slot)) return;
    var link = links[slot];
    try { if (link.chan) link.chan.close(); } catch (e) {}
    try { if (link.pc) link.pc.close(); } catch (e) {}
    links[slot] = fresh(); hostBlobs[slot] = '';
  }
  function resetAll() { for (var i=0;i<MAX_LINKS;i++) resetLink(i); joinBlob=''; }

  async function pack(obj) {
    var bytes = new TextEncoder().encode(JSON.stringify(obj));
    if (typeof CompressionStream === 'function') try {
      var stream = new Blob([bytes]).stream().pipeThrough(new CompressionStream('gzip'));
      return 'CLZ' + b64encode(new Uint8Array(await new Response(stream).arrayBuffer()));
    } catch (e) {}
    return 'CLR' + b64encode(bytes);
  }
  async function unpack(blob) {
    var body=blob.replace(/\s+/g,''), tag=body.slice(0,3), data=b64decode(body.slice(3));
    if (tag === 'CLZ') {
      if (typeof DecompressionStream !== 'function') throw new Error('this browser cannot unpack the connection code');
      var stream = new Blob([data]).stream().pipeThrough(new DecompressionStream('gzip'));
      data = new Uint8Array(await new Response(stream).arrayBuffer());
    } else if (tag !== 'CLR') throw new Error('not a Cinderlift code');
    return JSON.parse(new TextDecoder().decode(data));
  }
  function b64encode(bytes) { var s=''; for(var i=0;i<bytes.length;i++) s+=String.fromCharCode(bytes[i]); return btoa(s); }
  function b64decode(text) { var s=atob(text), out=new Uint8Array(s.length); for(var i=0;i<s.length;i++) out[i]=s.charCodeAt(i); return out; }
  function gathered(conn) {
    return new Promise(function(resolve) {
      if (conn.iceGatheringState === 'complete') { resolve(); return; }
      var done=false;
      function finish(){ if(done)return; done=true; conn.removeEventListener('icegatheringstatechange',check); resolve(); }
      function check(){ if(conn.iceGatheringState==='complete') finish(); }
      conn.addEventListener('icegatheringstatechange',check); setTimeout(finish,4000);
    });
  }
  function wire(slot, dc) {
    var link=links[slot]; link.chan=dc; dc.binaryType='arraybuffer';
    dc.onopen=function(){link.state='open';};
    dc.onclose=function(){if(link.state!=='failed')link.state='closed';};
    dc.onerror=function(){link.error='data channel error';link.state='failed';};
    dc.onmessage=function(e){var b=new Uint8Array(e.data);link.inbox.push(b);link.inboxBytes+=b.length;};
  }
  function makeConnection(slot) {
    var link=links[slot], conn=new RTCPeerConnection({iceServers:ICE}); link.pc=conn;
    conn.onconnectionstatechange=function(){
      if(conn.connectionState==='failed'){link.error='Could not reach the other player directly';link.state='failed';}
      else if(conn.connectionState==='disconnected'||conn.connectionState==='closed')if(link.state==='open')link.state='closed';
    };
    return conn;
  }
  async function makeHost(slot) {
    if(!valid(slot))throw new Error('invalid host connection slot');
    resetLink(slot); var link=links[slot]; link.state='gathering'; var pc=makeConnection(slot);
    wire(slot,pc.createDataChannel('cinderlift-'+slot,{ordered:true}));
    await pc.setLocalDescription(await pc.createOffer()); await gathered(pc); link.state='waiting';
    return await pack({t:'offer',d:pc.localDescription.sdp});
  }
  async function makeGuest(code) {
    resetAll();role='guest';var slot=0,link=links[0];link.state='gathering';
    var msg=await unpack(code);if(msg.t!=='offer')throw new Error('that is not a host code');
    var pc=makeConnection(slot);pc.ondatachannel=function(e){wire(slot,e.channel);};
    await pc.setRemoteDescription({type:'offer',sdp:msg.d});
    await pc.setLocalDescription(await pc.createAnswer());await gathered(pc);link.state='connecting';
    return await pack({t:'answer',d:pc.localDescription.sdp});
  }
  async function acceptGuest(code,slot) {
    slot=slot===undefined?0:slot;if(!valid(slot))throw new Error('invalid host connection slot');
    var msg=await unpack(code);if(msg.t!=='answer')throw new Error('that is not a join code');
    if(!links[slot].pc)throw new Error('that host slot is not ready');
    await links[slot].pc.setRemoteDescription({type:'answer',sdp:msg.d});links[slot].state='connecting';
  }
  function aggregateState(){
    if(role==='guest')return links[0].state;if(role!=='host')return'idle';
    var open=0,pending=0;for(var i=0;i<MAX_LINKS;i++){if(links[i].state==='open')open++;else if(links[i].state!=='failed'&&links[i].state!=='closed')pending++;}
    return open?('hosting '+open+'/3'):(pending?'hosting':'closed');
  }

  var api={
    maxLinks:MAX_LINKS,
    host:async function(slot){slot=slot===undefined?0:slot;if(role!=='host'){resetAll();role='host';}var c=await makeHost(slot);hostBlobs[slot]=c;return c;},
    join:async function(code){return await makeGuest(code);},
    accept:async function(code,slot){await acceptGuest(code,slot);},
    state:function(slot){return valid(slot)?links[slot].state:aggregateState();},
    error:function(slot){if(valid(slot))return links[slot].error;for(var i=0;i<MAX_LINKS;i++)if(links[i].error)return links[i].error;return'';},
    isOpen:function(slot){slot=slot||0;return valid(slot)&&links[slot].state==='open'&&links[slot].chan&&links[slot].chan.readyState==='open';},
    send:function(bytes,slot){slot=slot||0;if(!api.isOpen(slot))return false;try{for(var at=0;at<bytes.length;at+=16384)links[slot].chan.send(bytes.subarray(at,Math.min(at+16384,bytes.length)));return true;}catch(e){links[slot].error='send failed';return false;}},
    pending:function(slot){slot=slot||0;return valid(slot)?links[slot].inboxBytes:0;},
    read:function(view,max,slot){slot=slot||0;if(!valid(slot))return 0;var link=links[slot],wrote=0;while(link.inbox.length&&wrote<max){var head=link.inbox[0],room=max-wrote;if(head.length<=room){view.set(head,wrote);wrote+=head.length;link.inbox.shift();}else{view.set(head.subarray(0,room),wrote);link.inbox[0]=head.subarray(room);wrote+=room;}}link.inboxBytes-=wrote;return wrote;},
    close:function(slot){if(valid(slot))resetLink(slot);else{resetAll();role='idle';}},
    lastFault:'',
    beginHost:function(){resetAll();role='host';api.lastFault='';for(let slot=0;slot<MAX_LINKS;slot++)makeHost(slot).then(function(c){hostBlobs[slot]=c;}).catch(function(e){links[slot].error=e.message||'host failed';links[slot].state='failed';});},
    beginJoin:function(code){api.lastFault='';joinBlob='';api.join(code).then(function(c){joinBlob=c;}).catch(function(e){api.lastFault=e.message||'join failed';links[0].state='failed';});},
    beginAccept:function(slot,code){if(typeof slot==='string'){code=slot;slot=0;}api.lastFault='';api.accept(code,slot).catch(function(e){links[slot].error=e.message||'accept failed';links[slot].state='failed';});},
    hostCode:function(slot){return hostBlobs[slot||0]||'';},
    hostCodes:function(){return hostBlobs.slice();},
    joinCode:function(){return joinBlob;},
    fault:function(slot){return api.lastFault||api.error(slot);}
  };
  window.CinderNet=api;
})();
