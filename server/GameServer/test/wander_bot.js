// [테스트 전용] 떠돌이 봇: 입장 → 원을 그리며 이동 → 보이는 첫 상대에게 사격·피격 보고
// 사용: node wander_bot.js host port name durationSec [shoot=1]
'use strict';
const net = require('net');
const [HOST, PORT, NAME, DUR, SHOOT] = [process.argv[2] || '127.0.0.1', +(process.argv[3] || 10301), process.argv[4] || 'Bot', +(process.argv[5] || 30), (process.argv[6] || '1') === '1'];
function pk(type, fill) { const b = Buffer.alloc(128); b.writeUInt16LE(type, 0); const n = fill ? fill(b, 2) : 2; const p = b.subarray(0, n); return Buffer.concat([Buffer.from([0x77, p.length & 255, p.length >> 8, 0, 0]), p]); }
const s = net.connect(PORT, HOST);
let buf = Buffer.alloc(0), me = null, x = 0, z = 0, cx = 0, cz = 0, t0 = Date.now(), offset = 0, others = new Map(), seq = 0, mseq = 0, dead = false;
const now = () => Date.now() - t0;
s.on('connect', () => s.write(pk(3000, (b, o) => { b.writeUInt32LE(4, o); o += 4; for (let i = 0; i < 12; i++) { b.writeUInt16LE(i < NAME.length ? NAME.charCodeAt(i) : 0, o); o += 2; } return o; })));
s.on('data', d => {
  buf = Buffer.concat([buf, d]);
  while (buf.length >= 5) {
    const l = buf.readUInt16LE(1); if (buf.length < 5 + l) break; const p = buf.subarray(5, 5 + l); buf = buf.subarray(5 + l);
    const t = p.readUInt16LE(0);
    if (t === 3100 && p[2] === 0) { me = p.readUInt32LE(3); cx = x = p.readFloatLE(8); cz = z = p.readFloatLE(12); offset = p.readUInt32LE(29) - now(); console.log(`[${NAME}] entered id ${me} at ${x.toFixed(1)},${z.toFixed(1)}`); }
    if (t === 3102) for (let i = 0; i < p[2]; i++) { const o = 3 + i * 54; others.set(p.readUInt32LE(o), { x: p.readFloatLE(o + 28), z: p.readFloatLE(o + 32) }); console.log(`[${NAME}] sees ${p.readUInt32LE(o)}`); }
    if (t === 3103) for (let i = 0; i < p[2]; i++) others.delete(p.readUInt32LE(3 + i * 4));
    if (t === 3104) { const id = p.readUInt32LE(2); if (others.has(id)) others.set(id, { x: p.readFloatLE(6), z: p.readFloatLE(10) }); }
    if (t === 3107) console.log(`[${NAME}] damage ${p.readUInt32LE(2)} -> ${p.readUInt32LE(6)} hp ${p.readUInt16LE(16)}`);
    if (t === 3108) { const v = p.readUInt32LE(2); if (v === me) { dead = true; console.log(`[${NAME}] died`); } others.delete(v); }
    if (t === 3113) { const rtt = now() - p.readUInt32LE(2); offset = p.readUInt32LE(6) + rtt / 2 - now(); }
  }
});
s.on('close', () => { console.log(`[${NAME}] closed`); process.exit(0); });
let ang = 0;
setInterval(() => {
  if (!me || dead) return;
  ang += 0.1 * 1.2; // 반경 10 원, 12u/s 이하
  const nx = cx + Math.cos(ang) * 10, nz = cz + Math.sin(ang) * 10;
  const vx = (nx - x) * 10, vz = (nz - z) * 10; x = nx; z = nz;
  s.write(pk(3001, (b, o) => { b.writeFloatLE(x, o); b.writeFloatLE(z, o + 4); b.writeFloatLE(vx, o + 8); b.writeFloatLE(vz, o + 12); b.writeFloatLE(0, o + 16); b.writeUInt16LE(++mseq, o + 20); return o + 22; }));
}, 100);
setInterval(() => { if (me) s.write(pk(3004, (b, o) => { b.writeUInt32LE(now(), o); return o + 4; })); }, 1000);
if (SHOOT) setInterval(() => {
  if (!me || dead || others.size === 0) return;
  const [tid, tp] = others.entries().next().value;
  const dx = tp.x - x, dz = tp.z - z, dist = Math.hypot(dx, dz); if (dist > 60 || dist < 0.1) return;
  seq++; const vt = Math.floor(now() + offset - 100);
  s.write(pk(3002, (b, o) => { b.writeUInt32LE(seq, o); b[o + 4] = 1; b.writeFloatLE(x, o + 5); b.writeFloatLE(z, o + 9); b.writeFloatLE(dx / dist, o + 13); b.writeFloatLE(dz / dist, o + 17); b.writeUInt32LE(vt >>> 0, o + 21); b.writeUInt16LE(0, o + 25); return o + 27; }));
  const hs = seq, hx = tp.x - dx / dist * 0.4, hz = tp.z - dz / dist * 0.4;
  setTimeout(() => s.write(pk(3003, (b, o) => { b[o] = 1; b.writeUInt32LE(hs, o + 1); b[o + 5] = 0; b.writeUInt32LE(tid, o + 6); b.writeFloatLE(hx, o + 10); b.writeFloatLE(hz, o + 14); return o + 18; })), dist / 60 * 1000);
}, 1500);
setTimeout(() => process.exit(0), DUR * 1000);
