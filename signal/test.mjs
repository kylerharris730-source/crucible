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
check(opened.status === 200 && opened.body.code?.length === 5 && opened.body.key?.length === 48,
      'open returns a code and a key (five characters for a host that does not ask)');
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
{
  const six = await call('POST', '/room', JSON.stringify({ offers: ['CLR6a', 'CLR6b', 'CLR6c'], codeLength: 6 }));
  check(six.status === 200 && /^[A-HJKMNP-Z2-9]{6}$/.test(six.body.code), 'a host that asks gets a six-character code');
  check((await call('GET', `/room/${six.body.code}`)).body?.offer === 'CLR6a', 'which a guest can join');
  check((await call('GET', '/room/ZZZZZZZ')).status === 404, 'seven characters is never a room');
}
check((await call('GET', '/room/PROBE')).status === 404, "the page's PROBE is a clean 404");

// --- one address, several guests ---------------------------------------------
// A classroom or a household is one address to the broker. Two friends behind it
// joining together must both keep their seats; only a guest's own retry, naming
// the claim it held, gives a seat back.
const two = (await call('POST', '/room', JSON.stringify({ offers: ['CLR1', 'CLR2', 'CLR3'] }))).body;
const from = ip => ({ 'CF-Connecting-IP': ip });
const school = from('203.0.113.7');
const s1 = await call('GET', `/room/${two.code}`, undefined, school);
const s2 = await call('GET', `/room/${two.code}`, undefined, school);
check(s1.status === 200 && s2.status === 200 && s1.body.slot !== s2.body.slot, 'two guests behind one address get two seats');
check((await call('POST', `/room/${two.code}/answer`, JSON.stringify({ slot: s1.body.slot, claim: s1.body.claim, answer: 'CLRx' }))).status === 204,
      'and the first answer still lands');
const s3 = await call('GET', `/room/${two.code}?release=${s2.body.claim}`, undefined, school);
check(s3.status === 200, 'a retry naming its old claim gets a seat');
check((await call('POST', `/room/${two.code}/answer`, JSON.stringify({ slot: s2.body.slot, claim: s2.body.claim, answer: 'CLRy' }))).status === 409,
      'and the seat it named is given up');
const s4 = await call('GET', `/room/${two.code}?release=nonsense`, undefined, from('198.51.100.1'));
check(s4.status === 200, 'naming a claim it never held frees nothing, but still finds the free seat');
check(new Set([s1.body.slot, s3.body.slot, s4.body.slot]).size === 3, 'and nobody shares one');
check((await call('GET', `/room/${two.code}`, undefined, from('192.0.2.9'))).status === 409, 'a fourth guest finds it full');

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
