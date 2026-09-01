import fs from 'node:fs';
import { webcrypto } from 'node:crypto';
if (!globalThis.crypto) globalThis.crypto = webcrypto;

/* Load the Worker as an ES module without imposing package.json module mode on
   the repository's browser scripts. */
const source = fs.readFileSync(new URL('../signal/worker.js', import.meta.url), 'utf8');
const worker = (await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'))).default;

class FakeStatement {
  constructor(db, sql) { this.db=db; this.sql=sql.replace(/\s+/g,' ').trim(); this.args=[]; }
  bind(...args) { this.args=args; return this; }
  async first() {
    const row=this.db.rooms.get(this.args[0]);
    if (!row || row.expires <= this.args[1]) return null;
    if (this.sql.startsWith('SELECT offer, answer, expires')) return { offer:row.offer, answer:row.answer, expires:row.expires };
    if (this.sql.startsWith('SELECT offer, answer')) return { offer:row.offer, answer:row.answer };
    if (this.sql.startsWith('SELECT answer')) return { answer:row.answer };
    /* The rollout guard reads the offer alone to find out whether a room is
       multi-slot before letting a legacy answer overwrite it. A fake that
       throws on an unmodelled query turns a correct guard into a test
       failure, which is how a fake stops being a stand-in and starts being
       an obstacle. */
    if (this.sql.startsWith('SELECT offer FROM')) return { offer:row.offer };
    throw new Error('unexpected first: '+this.sql);
  }
  async run() {
    if (this.sql.startsWith('DELETE FROM rooms')) {
      if (this.sql.includes('WHERE code =')) { const existed=this.db.rooms.delete(this.args[0]); return {meta:{changes:existed?1:0}}; }
      for (const [k,v] of this.db.rooms) if (v.expires < this.args[0]) this.db.rooms.delete(k);
      return {meta:{changes:0}};
    }
    if (this.sql.startsWith('INSERT OR IGNORE')) {
      const [code,offer,answer,expires]=this.args;
      if(this.db.rooms.has(code))return {meta:{changes:0}};
      this.db.rooms.set(code,{offer,answer,expires});return {meta:{changes:1}};
    }
    if (this.sql.startsWith('UPDATE rooms SET answer')) {
      if (this.args.length===3 && this.sql.includes('expires >')) {
        const [next,code,expiry]=this.args,row=this.db.rooms.get(code);
        if(!row||row.expires<=expiry)return {meta:{changes:0}};row.answer=next;return {meta:{changes:1}};
      }
      const [next,code,old,expiry]=this.args, row=this.db.rooms.get(code);
      if(!row||row.answer!==old||(expiry!==undefined&&row.expires<=expiry))return {meta:{changes:0}};
      row.answer=next;return {meta:{changes:1}};
    }
    /* Re-opening a seat writes the offer, the seat state and a fresh expiry
       together, with the compare-and-swap still on the seat state -- a guest
       may be reserving another seat in the same instant. */
    if (this.sql.startsWith('UPDATE rooms SET offer')) {
      const [offer,next,expires,code,old]=this.args, row=this.db.rooms.get(code);
      if(!row||row.answer!==old)return {meta:{changes:0}};
      row.offer=offer;row.answer=next;row.expires=expires;return {meta:{changes:1}};
    }
    /* Hosting extends the room's life from the answer poll. */
    if (this.sql.startsWith('UPDATE rooms SET expires')) {
      const [expires,code]=this.args, row=this.db.rooms.get(code);
      if(!row)return {meta:{changes:0}};
      row.expires=expires;return {meta:{changes:1}};
    }
    throw new Error('unexpected run: '+this.sql);
  }
}
class FakeDB { constructor(){this.rooms=new Map();} prepare(sql){return new FakeStatement(this,sql);} }

const env={DB:new FakeDB()}, base='https://signal.test';
const call=(path,method='GET',body)=>worker.fetch(new Request(base+path,{method,body}),env);
const offers=['offer-a','offer-b','offer-c'];
let response=await call('/room','POST',JSON.stringify({offers}));
if(response.status!==200)throw new Error('room creation failed');
const code=(await response.json()).code;

/* All four requests begin together. Three must atomically reserve distinct
   seats, and the fourth must receive a full room rather than somebody else's
   offer. */
const joins=await Promise.all([0,1,2,3].map(()=>call('/room/'+code)));
const admitted=joins.filter(r=>r.status===200), refused=joins.filter(r=>r.status===409);
if(admitted.length!==3||refused.length!==1)throw new Error('room did not admit exactly three guests');
const reservations=await Promise.all(admitted.map(r=>r.json()));
if(new Set(reservations.map(r=>r.slot)).size!==3)throw new Error('simultaneous guests shared a slot');
for(const r of reservations)if(r.offer!==offers[r.slot])throw new Error('guest received the wrong offer');

await Promise.all(reservations.map((r,i)=>call('/room/'+code+'/answer','POST',
  JSON.stringify({slot:r.slot,claim:r.claim,answer:'answer-'+i}))));
const delivered=[];
for(let i=0;i<3;i++){const r=await call('/room/'+code+'/answer');if(r.status!==200)throw new Error('answer missing');delivered.push(await r.json());}
if(new Set(delivered.map(r=>r.slot)).size!==3)throw new Error('host received a duplicate answer slot');
if((await call('/room/'+code+'/answer')).status!==204)throw new Error('consumed answer was delivered twice');
if((await call('/room/'+code)).status!==409)throw new Error('a delivered seat was incorrectly reused');

/* A page cached during deployment can still complete its old single-peer room
   while the new Worker is already live. */
response=await call('/room','POST','CLRlegacy-offer');
if(response.status!==200)throw new Error('legacy room creation failed');
const legacyCode=(await response.json()).code;
response=await call('/room/'+legacyCode);if((await response.json()).offer!=='CLRlegacy-offer')throw new Error('legacy offer changed');
if((await call('/room/'+legacyCode+'/answer','POST','CLRlegacy-answer')).status!==204)throw new Error('legacy answer rejected');
response=await call('/room/'+legacyCode+'/answer');if((await response.json()).answer!=='CLRlegacy-answer')throw new Error('legacy answer changed');
/* The other half of the rollout, and the one that actually costs somebody a
   game: a browser holding a cached copy of the OLD page joins a NEW room. It
   reads `offer` out of the reply and ignores the `slot` and `claim` beside it,
   then posts a bare answer. An unguarded legacy branch would write that string
   straight over the room's seat state -- evicting the guests already in it and
   leaving the host polling a room it can no longer parse. One stale tab, three
   people's game.

   So the refusal is checked, and so is the room surviving it. */
response=await call('/room','POST',JSON.stringify({offers:['a','b','c']}));
const mixedCode=(await response.json()).code;
const seat=await (await call('/room/'+mixedCode)).json();
const stale=await call('/room/'+mixedCode+'/answer','POST','CLRstale-tab-answer');
if(stale.status!==409)throw new Error('a stale client was allowed to overwrite a multi-slot room');
const after=await call('/room/'+mixedCode);
if(after.status!==200)throw new Error('the room did not survive a stale answer');
const stillThere=await after.json();
if(stillThere.slot===seat.slot)throw new Error('the surviving room forgot a seat was taken');
if((await call('/room/'+mixedCode+'/answer','POST',
   JSON.stringify({slot:seat.slot,claim:seat.claim,answer:'real'}))).status!==204)
  throw new Error('the original guest lost its reservation to a stale tab');

/* Reconnection, which is the case a real game produced within minutes: a guest
   in New York joined, dropped, re-entered the same code, and came back as
   player THREE rather than player two. Their old seat was still marked used,
   so the room handed out the next one, and after three drops a room would be
   full of ghosts with nobody in it.

   A dropped WebRTC link cannot be resumed -- the description that built it
   described one moment's network -- so the host has to publish a NEW offer for
   that seat. This checks the seat comes back, keeps its index, and hands out
   the replacement offer rather than the stale one. */
response=await call('/room','POST',JSON.stringify({offers:['seat-a','seat-b','seat-c']}));
const dropCode=(await response.json()).code;
const first=await (await call('/room/'+dropCode)).json();
await call('/room/'+dropCode+'/answer','POST',
  JSON.stringify({slot:first.slot,claim:first.claim,answer:'first-answer'}));
if((await (await call('/room/'+dropCode+'/answer')).json()).slot!==first.slot)
  throw new Error('host did not collect the first answer');

/* That seat is now used. Until the host re-opens it, it must stay used. */
const beforeReopen=await (await call('/room/'+dropCode)).json();
if(beforeReopen.slot===first.slot)throw new Error('a used seat was handed out again');

if((await call('/room/'+dropCode+'/seat','POST',
   JSON.stringify({slot:first.slot,offer:'seat-a-second'}))).status!==204)
  throw new Error('host could not re-open the dropped seat');
const back=await (await call('/room/'+dropCode)).json();
if(back.slot!==first.slot)throw new Error('the returning guest did not get its old seat');
if(back.offer!=='seat-a-second')throw new Error('the returning guest got the stale offer');
if(back.claim===first.claim)throw new Error('the re-opened seat reused the old reservation');

if((await call('/room/'+dropCode+'/seat','POST',
   JSON.stringify({slot:9,offer:'x'}))).status!==400)
  throw new Error('an out-of-range seat was accepted');

/* Asking for a specific seat back. A preference, not a reservation: honoured
   when that seat is free, ignored when it is not, because refusing outright
   would leave somebody unable to rejoin a game that has room in it. */
response=await call('/room','POST',JSON.stringify({offers:['p0','p1','p2']}));
const prefCode=(await response.json()).code;
const askTwo=await (await call('/room/'+prefCode+'?seat=2')).json();
if(askTwo.slot!==2)throw new Error('a free preferred seat was not honoured');
if(askTwo.offer!=='p2')throw new Error('preferred seat gave the wrong offer');
const askTwoAgain=await (await call('/room/'+prefCode+'?seat=2')).json();
if(askTwoAgain.slot===2)throw new Error('a taken seat was handed out twice');
const askJunk=await (await call('/room/'+prefCode+'?seat=99')).json();
if(!Number.isInteger(askJunk.slot))throw new Error('a nonsense preference broke the reservation');

console.log('four-player signaling passed (three atomic reservations, overflow refused)');
