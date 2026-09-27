/* The broker's host-key rules, against a real SQLite standing in for D1.

     node signal/test.mjs          (Node 22.5+, for node:sqlite)

   worker.js is an ES module without a package.json saying so, so it is
   imported from its source text rather than by path. */
import { DatabaseSync } from 'node:sqlite';
import { readFileSync } from 'node:fs';
const here = new URL('.', import.meta.url);
const source = readFileSync(new URL('worker.js', here), 'utf8');
const worker = (await import('data:text/javascript,' + encodeURIComponent(source))).default;
const sql = new DatabaseSync(':memory:');
sql.exec(readFileSync(new URL('schema.sql', here), 'utf8'));
const DB = { prepare(q) { const st = sql.prepare(q); let args = [];
  const o = { bind(...a) { args = a; return o; },
    async first() { return st.get(...args) ?? null; },
    async run() { const r = st.run(...args); return { meta: { changes: Number(r.changes) } }; } };
  return o; } };
const env = { DB };
async function call(method, path, body, headers = {}) {
  const r = await worker.fetch(new Request('https://x' + path, { method, body, headers }), env);
  const t = await r.text(); let j = null; try { j = JSON.parse(t); } catch {}
  return { status: r.status, body: j };
}
let fail = 0; const check = (ok, what) => { if (!ok) { console.log('FAIL', what); fail++; } else console.log('ok  ', what); };

const opened = await call('POST', '/room', JSON.stringify({ offers: ['CLRa', 'CLRb', 'CLRc'] }));
check(opened.status === 200 && opened.body.code && opened.body.key?.length === 48, 'open returns a code and a key');
const { code, key } = opened.body;
const K = { 'X-Room-Key': key };

check((await call('POST', '/room', 'CLZlegacyoffer')).status === 400, 'legacy single-offer rooms are refused');
const g = await call('GET', `/room/${code}`);
check(g.status === 200 && g.body.offer === 'CLRa' && !JSON.stringify(g.body).includes('keyHash'), 'guest gets one offer and no key hash');
check((await call('GET', `/room/${code}/answer`)).status === 403, 'answers need the key');
check((await call('GET', `/room/${code}/answer`, undefined, { 'X-Room-Key': 'nope' })).status === 403, 'a wrong key is refused');
check((await call('GET', `/room/${code}/answer`, undefined, K)).status === 204, 'the host with the key polls');
check((await call('POST', `/room/${code}/answer`, 'CLZbare')).status === 400, 'legacy bare answers are refused');
check((await call('POST', `/room/${code}/answer`, JSON.stringify({ slot: g.body.slot, claim: g.body.claim, answer: 'CLRans' }))).status === 204, 'guest posts an answer');
const a = await call('GET', `/room/${code}/answer`, undefined, K);
check(a.status === 200 && a.body.answer === 'CLRans' && a.body.slot === 0, 'host collects it');
const seatBody = JSON.stringify({ slot: 0, offer: 'CLRevil' });
check((await call('POST', `/room/${code}/seat`, seatBody)).status === 403, 'a seat cannot be re-offered without the key');
check((await call('POST', `/room/${code}/seat`, JSON.stringify({ slot: 0, offer: 'CLRnew' }), K)).status === 204, 'the host re-offers a seat');
const g2 = await call('GET', `/room/${code}?seat=0`);
check(g2.status === 200 && g2.body.offer === 'CLRnew', 'the re-offered seat carries the host\'s offer');
check((await call('GET', `/room/${code}/answer`, undefined, K)).status === 204, 'and the key still works after a re-offer');
// a room from before the key
sql.prepare('INSERT INTO rooms (code, offer, answer, expires) VALUES (?,?,?,?)').run('ABCDE', JSON.stringify({ offers: ['a','b','c'] }), null, Date.now() + 60000);
check((await call('GET', '/room/ABCDE/answer')).status === 409, 'a keyless room cannot be polled');
check((await call('POST', '/room/ABCDE/seat', seatBody)).status === 409, 'or re-offered');
check((await call('GET', '/room/PROBE')).status === 404, "the page's PROBE is a clean 404");

// --- one pending seat per address --------------------------------------------
const two = (await call('POST', '/room', JSON.stringify({ offers: ['CLR1', 'CLR2', 'CLR3'] }))).body;
const from = ip => ({ 'CF-Connecting-IP': ip });
const s1 = await call('GET', `/room/${two.code}`, undefined, from('203.0.113.7'));
const s2 = await call('GET', `/room/${two.code}`, undefined, from('203.0.113.7'));
const s3 = await call('GET', `/room/${two.code}`, undefined, from('198.51.100.1'));
const s4 = await call('GET', `/room/${two.code}`, undefined, from('198.51.100.2'));
check(s1.status === 200 && s2.status === 200, 'one address may ask twice');
check((await call('POST', `/room/${two.code}/answer`, JSON.stringify({ slot: s1.body.slot, claim: s1.body.claim, answer: 'CLRx' }))).status === 409,
      'but asking again gives up the seat it held');
check(s3.status === 200 && s4.status === 200, 'so two other addresses still find seats');
check(new Set([s2.body.slot, s3.body.slot, s4.body.slot]).size === 3, 'and nobody shares one');
check((await call('GET', `/room/${two.code}`, undefined, from('192.0.2.9'))).status === 409, 'a fourth address finds it full');

// --- rate limits ---------------------------------------------------------------
{
  const counts = {};
  const limiter = n => ({ async limit({ key }) { counts[key] = (counts[key] || 0) + 1; return { success: counts[key] <= n }; } });
  const saved = { ...env };
  env.JOIN_LIMIT = limiter(3); env.OPEN_LIMIT = limiter(2);
  const guesses = [];
  for (let i = 0; i < 5; i++) guesses.push((await call('GET', '/room/ZZZZZ', undefined, from('203.0.113.50'))).status);
  check(guesses.join() === '404,404,404,429,429', 'guessing codes is throttled per address');
  check((await call('GET', '/room/ZZZZZ', undefined, from('203.0.113.51'))).status === 404, 'without touching anybody else');
  check((await call('GET', '/room/PROBE', undefined, from('203.0.113.50'))).status === 404, 'a malformed code is not counted');
  const opens = [];
  for (let i = 0; i < 3; i++)
    opens.push((await call('POST', '/room', JSON.stringify({ offers: ['CLRa', 'CLRb', 'CLRc'] }), from('203.0.113.60'))).status);
  check(opens.join() === '200,200,429', 'opening rooms is throttled per address');
  check((await call('GET', `/room/${code}/answer`, undefined, { ...K, ...from('203.0.113.50') })).status !== 429,
        "the host's own polling is never throttled");
  Object.keys(env).forEach(k => delete env[k]); Object.assign(env, saved);
}

const opt = await worker.fetch(new Request('https://x/room/X/answer', { method: 'OPTIONS' }), env);
check(opt.headers.get('Access-Control-Allow-Headers').includes('X-Room-Key'), 'CORS allows the key header');
process.exit(fail ? 1 : 0);
