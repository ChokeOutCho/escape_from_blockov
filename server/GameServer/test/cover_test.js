// [테스트 전용] 파괴 가능 엄폐물 · 스폰 규칙 · 에어드랍 예고 테스트 (game-spec 3장, 3.6, 6.4, 11.3)
//  1) node cover_test.js make cover/cover_map.bmp      : 테스트 맵 생성 (600x600, 24bit)
//  2) test/cover 폴더에서 서버 실행(포트 10503) → node cover_test.js run 127.0.0.1 10503
//  3) test/spawn 폴더에서 서버 실행(포트 10504) → node cover_test.js spawn 127.0.0.1 10504
// 맵: 엄폐물 0 = x[535,537) z[518,532) 체력 30 (R 174)  /  엄폐물 1 = (510,540) 1칸 체력 10 (R 150)   스폰 (525,525)
'use strict';
const fs = require('fs');
const net = require('net');
const W = 600, H = 600;
const COVER0 = { x0: 535, x1: 537, z0: 518, z1: 532, r: 174, level: 3 };
const COVER1 = { x0: 510, x1: 511, z0: 540, z1: 541, r: 150, level: 1 };
const inside = (c, x, z) => x >= c.x0 && x < c.x1 && z >= c.z0 && z < c.z1;
const cellValue = (x, z) => inside(COVER0, x, z) ? 3 + COVER0.level * 16 : inside(COVER1, x, z) ? 3 + COVER1.level * 16 : 0;

function makeMap(out) {
  const stride = Math.ceil(W * 3 / 4) * 4;
  const img = Buffer.alloc(54 + stride * H, 0xff);
  img.write('BM', 0); img.writeUInt32LE(img.length, 2); img.writeUInt32LE(54, 10);
  img.writeUInt32LE(40, 14); img.writeInt32LE(W, 18); img.writeInt32LE(H, 22);
  img.writeUInt16LE(1, 26); img.writeUInt16LE(24, 28); img.writeUInt32LE(0, 30); img.writeUInt32LE(stride * H, 34);
  for (const c of [COVER0, COVER1])
    for (let z = c.z0; z < c.z1; z++) for (let x = c.x0; x < c.x1; x++) {
      const o = 54 + z * stride + x * 3;      // bottom-up: row = z
      img[o] = 40; img[o + 1] = 40; img[o + 2] = c.r;   // BGR
    }
  fs.writeFileSync(out, img);
  console.log('map written', out);
}

function expectedHash() {
  let h = 2166136261 >>> 0;
  for (let z = 0; z < 1500; z++) for (let x = 0; x < 1500; x++) { h ^= cellValue(x, z); h = Math.imul(h, 16777619) >>> 0; }
  return h >>> 0;
}

const sleep = ms => new Promise(r => setTimeout(r, ms));
let failures = 0;
const check = (c, m) => { console.log(`${c ? '  ok  ' : '  FAIL'} ${m}`); if (!c) failures++; };
function pk(type, fill) { const b = Buffer.alloc(128); b.writeUInt16LE(type, 0); const n = fill ? fill(b, 2) : 2; const p = b.subarray(0, n); return Buffer.concat([Buffer.from([0x77, p.length & 255, p.length >> 8, 0, 0]), p]); }
const COVER_BIT = 0x80000000;

function coverStates(p) { const n = p[2], out = []; for (let i = 0; i < n; i++) { const o = 3 + i * 9; out.push({ id: p.readUInt16LE(o), destroyed: p[o + 2], at: p.readUInt32LE(o + 3), regen: p.readUInt16LE(o + 7) }); } return out; }

class Bot {
  constructor(host, port, name) { this.host = host; this.port = port; this.name = name; this.msgs = []; this.buf = Buffer.alloc(0); this.t0 = Date.now(); this.seq = 0; this.shot = 0; this.all = []; }
  now() { return Date.now() - this.t0; }
  connect() { return new Promise(r => { this.s = net.connect(this.port, this.host, r); this.s.on('data', d => { this.buf = Buffer.concat([this.buf, d]); while (this.buf.length >= 5) { const l = this.buf.readUInt16LE(1); if (this.buf.length < 5 + l) break; const p = Buffer.from(this.buf.subarray(5, 5 + l)); this.msgs.push(p); this.all.push(p); this.buf = this.buf.subarray(5 + l); } }); }); }
  async wait(type, pred = () => true, ms = 1500) { const end = Date.now() + ms; while (Date.now() < end) { const i = this.msgs.findIndex(p => p.readUInt16LE(0) === type && pred(p)); if (i >= 0) return this.msgs.splice(i, 1)[0]; await sleep(10); } return null; }
  async enter() {
    this.s.write(pk(3000, (b, o) => { b.writeUInt32LE(7, o); o += 4; for (let i = 0; i < 12; i++) { b.writeUInt16LE(i < this.name.length ? this.name.charCodeAt(i) : 0, o); o += 2; } return o; }));
    const e = await this.wait(3100);
    this.id = e.readUInt32LE(3); this.x = e.readFloatLE(8); this.z = e.readFloatLE(12); this.offset = e.readUInt32LE(29) - this.now(); this.mapHash = e.readUInt32LE(57);
    return e;
  }
  serverNow() { return Math.floor(this.now() + this.offset); }
  move(x, z, vx, vz) { this.s.write(pk(3001, (b, o) => { b.writeFloatLE(x, o); b.writeFloatLE(z, o + 4); b.writeFloatLE(vx, o + 8); b.writeFloatLE(vz, o + 12); b.writeFloatLE(0, o + 16); b.writeUInt16LE(++this.seq, o + 20); return o + 22; })); }
  async walkTo(tx, tz) {
    while (Math.hypot(tx - this.x, tz - this.z) > 0.01) {
      const d = Math.hypot(tx - this.x, tz - this.z), st = Math.min(4, d);
      const nx = this.x + (tx - this.x) / d * st, nz = this.z + (tz - this.z) / d * st;
      this.move(nx, nz, (tx - this.x) / d * 50, (tz - this.z) / d * 50);
      const c = await this.wait(3105, () => true, 90);
      if (c) { this.x = c.readFloatLE(2); this.z = c.readFloatLE(6); return false; }
      this.x = nx; this.z = nz;
    }
    this.move(this.x, this.z, 0, 0);
    return true;
  }
  fire(dx, dz) {
    const l = Math.hypot(dx, dz); dx /= l; dz /= l;
    const seq = ++this.shot, vt = this.serverNow() - 100;
    this.s.write(pk(3002, (b, o) => { b.writeUInt32LE(seq, o); b[o + 4] = 1; b.writeFloatLE(this.x, o + 5); b.writeFloatLE(this.z, o + 9); b.writeFloatLE(dx, o + 13); b.writeFloatLE(dz, o + 17); b.writeUInt32LE(vt >>> 0, o + 21); b.writeUInt16LE(0, o + 25); return o + 27; }));
    return seq;
  }
  report(seq, target, hx, hz) {
    this.s.write(pk(3003, (b, o) => { b[o] = 1; b.writeUInt32LE(seq, o + 1); b[o + 5] = 0; b.writeUInt32LE(target >>> 0, o + 6); b.writeFloatLE(hx, o + 10); b.writeFloatLE(hz, o + 14); return o + 18; }));
  }
}

async function run(host, port) {
  const a = new Bot(host, port, 'A'), b = new Bot(host, port, 'B');
  await a.connect(); await a.enter(); await b.connect(); await b.enter();
  const eh = expectedHash();
  check(a.mapHash === eh, `SC_ENTER_GAME MapHash 0x${a.mapHash.toString(16)} == 기대값 0x${eh.toString(16)} (파괴 가능 칸 = 3 + level*16)`);
  await sleep(200);
  check(!a.all.some(p => p.readUInt16LE(0) === 3123), '입장 시 파괴된 엄폐물 없음 → SC_COVER_STATE 없음');

  console.log('== 이동 차단 / 멀쩡한 엄폐물 너머 사격');
  check(!(await b.walkTo(545, 525)) && b.x < 535, `엄폐물 통과 이동 → 보정 (x=${b.x.toFixed(2)})`);
  check(await b.walkTo(525, 534) && await b.walkTo(545, 534) && await b.walkTo(545, 525), 'B가 엄폐물을 돌아 동쪽(545,525)으로');
  await sleep(500);
  let seq = a.fire(1, 0);
  await sleep(250);
  a.report(seq, b.id, 544.4, 525);
  check((await b.wait(3107, p => p.readUInt32LE(10) === seq, 700)) === null, '멀쩡한 파괴 가능 엄폐물 너머 피격 보고 → 거부');

  console.log('== 엄폐물 피격 · 파괴');
  seq = a.fire(1, 0); await sleep(150);
  a.report(seq, COVER_BIT | 0, 535, 525);
  let hp = await b.wait(3122, () => true, 800);
  check(hp && hp.readUInt16LE(2) === 0 && hp[4] === 10, `SC_COVER_HP (id 0, 30-20 → ${hp && hp[4]}) 엄폐물 주변(B)에게`);
  check((await a.wait(3122, () => true, 300)) !== null, '쏜 사람(A)도 SC_COVER_HP 수신');
  a.report(seq, COVER_BIT | 0, 535, 525);
  check((await a.wait(3122, () => true, 400)) === null, '같은 산탄으로 엄폐물 두 번 → 거부 (탄은 엄폐물에서 멈춤)');
  await sleep(260);
  seq = a.fire(1, 0); await sleep(150);
  const tDestroy = a.serverNow();
  a.report(seq, COVER_BIT | 0, 535, 525);
  hp = await a.wait(3122, () => true, 800);
  check(hp && hp[4] === 0, 'SC_COVER_HP 0');
  const st = await b.wait(3123, () => true, 800);
  const s0 = st && coverStates(st)[0];
  check(s0 && s0.id === 0 && s0.destroyed === 1 && s0.regen === 3 && Math.abs((s0.at - tDestroy) | 0) < 300,
    `SC_COVER_STATE 파괴 (id ${s0 && s0.id}, regen ${s0 && s0.regen}초, 파괴 시각 오차 ${s0 && (s0.at - tDestroy)}ms)`);

  console.log('== 파괴된 엄폐물 (잔해: 이동·총알 통과)');
  await sleep(260);
  seq = a.fire(1, 0); await sleep(250);
  a.report(seq, b.id, 544.4, 525);
  check((await b.wait(3107, p => p.readUInt32LE(10) === seq, 800)) !== null, '파괴된 엄폐물 너머 피격 → 인정 (총알 통과)');
  a.report(seq, COVER_BIT | 0, 535, 525);
  check((await a.wait(3122, () => true, 400)) === null, '파괴된 엄폐물 피격 보고 → 무시');
  check(await b.walkTo(536, 525), `파괴된 엄폐물 안으로 이동 → 허용 (x=${b.x.toFixed(2)})`);

  console.log('== 잘못된 엄폐물 보고');
  await sleep(260);
  seq = a.fire(-15, 15); await sleep(150);
  a.report(seq, COVER_BIT | 1, 520, 530);
  check((await a.wait(3122, () => true, 400)) === null, '엄폐물에서 먼 지점 보고 → 거부');
  a.report(seq, COVER_BIT | 999, 511, 540);
  check((await a.wait(3122, () => true, 300)) === null, '없는 엄폐물 id → 거부');
  await sleep(260);
  seq = a.fire(510.5 - a.x, 540.5 - a.z); await sleep(200);
  a.report(seq, COVER_BIT | 1, 511.05, 540.4);
  const st1 = await a.wait(3123, p => coverStates(p).some(s => s.id === 1 && s.destroyed === 1), 800);
  check(st1 !== null, '엄폐물 1 (체력 10) 한 발에 파괴');

  console.log('== 입장 시 상태 · 재생');
  const c = new Bot(host, port, 'C'); await c.connect(); await c.enter();
  const cs = await c.wait(3123, () => true, 800);
  const ids = cs ? coverStates(cs).filter(s => s.destroyed).map(s => s.id).sort() : [];
  check(ids.length === 2 && ids[0] === 0 && ids[1] === 1, `입장 시 SC_COVER_STATE로 파괴된 엄폐물 목록 (${ids})`);
  const early = await c.wait(3123, p => coverStates(p).some(s => s.id === 0 && s.destroyed === 0), Math.max(0, tDestroy + 4200 - c.serverNow()));
  check(early === null, 'B가 엄폐물 0 자리에 서 있는 동안 재생 대기 (재생 시간 3초 지나도)');
  check(await b.walkTo(545, 525), 'B가 비켜섬 (545,525)');
  const regen = await c.wait(3123, p => coverStates(p).some(s => s.id === 0 && s.destroyed === 0), 1500);
  const waited = regen ? c.serverNow() - tDestroy : -1;
  check(regen !== null && waited >= 4000, `비켜서자 재생 SC_COVER_STATE destroyed=0 (${waited}ms)`);
  const r1 = c.all.some(p => p.readUInt16LE(0) === 3123 && coverStates(p).some(s => s.id === 1 && s.destroyed === 0)) ||
    (await c.wait(3123, p => coverStates(p).some(s => s.id === 1 && s.destroyed === 0), 3500)) !== null;
  check(r1, '아무도 없는 엄폐물 1은 3초 뒤 정상 재생');
  check(!(await b.walkTo(536, 525)) && b.x > 537, `재생된 엄폐물 안으로 이동 → 보정 (x=${b.x.toFixed(2)})`);
  check(await b.walkTo(545, 525), 'B 제자리');
  await sleep(300);
  seq = a.fire(1, 0); await sleep(250);
  a.report(seq, b.id, 544.4, 525);
  check((await b.wait(3107, p => p.readUInt32LE(10) === seq, 700)) === null, '재생 후에는 다시 총알을 막음');

  console.log(failures ? `\n${failures} FAILED` : '\nALL PASSED');
  process.exit(failures ? 1 : 0);
}

// 스폰 규칙 + 에어드랍 예고 (test/spawn: 엄폐물 없음, 에어드랍 4초 주기·2초 전 예고, 1명당 1개 최대 4)
async function spawnTest(host, port) {
  console.log('== 스폰: 주변 3x3 인원이 가장 적은 섹터');
  const bots = [];
  for (let i = 0; i < 6; i++) {
    const bt = new Bot(host, port, 'S' + i); await bt.connect(); await bt.enter();
    bt.sx = Math.floor(bt.x / 50); bt.sy = Math.floor(bt.z / 50);
    const clash = bots.filter(o => Math.max(Math.abs(o.sx - bt.sx), Math.abs(o.sy - bt.sy)) <= 1);
    check(clash.length === 0, `S${i} 스폰 섹터 (${bt.sx},${bt.sy}) — 다른 플레이어의 3x3 밖`);
    bots.push(bt);
  }
  const a = bots[0];
  const imm = await a.wait(3121, p => p[16] === 1, 500);
  check(imm !== null && !a.all.some(p => p.readUInt16LE(0) === 3124 && a.all.indexOf(p) < a.all.indexOf(imm)), '첫 입장 → 예고 없이 즉시 에어드랍');

  console.log('== 에어드랍 예고');
  const fc = await a.wait(3124, p => p[6] > 0, 4500);
  check(fc !== null, 'SC_AIRDROP_FORECAST 수신 (방 전체)');
  if (!fc) { process.exit(1); }
  const dropAt = fc.readUInt32LE(2), n = fc[6];
  const pos = []; for (let i = 0; i < n; i++) pos.push([fc.readFloatLE(7 + i * 8), fc.readFloatLE(11 + i * 8)]);
  const lead = dropAt - a.serverNow();
  check(lead > 1500 && lead <= 2100, `투하 ${lead}ms 전 예고 (airdrop_notice_ms 2000), ${n}개`);
  check(fc.length === 7 + 8 * n, `예고 패킷 길이 ${fc.length} = 7 + 8n`);
  check(bots.every(bt => bt === a || bt.all.some(p => p.readUInt16LE(0) === 3124)), '모든 플레이어가 예고 수신');
  const late = new Bot(host, port, 'L'); await late.connect(); await late.enter();
  await sleep(150);
  check(late.all.some(p => p.readUInt16LE(0) === 3124 && p.readUInt32LE(2) === dropAt), '예고 중 입장 → 입장 시퀀스에 예고 포함');
  const drops = [];
  for (let i = 0; i < n; i++) {
    const d = await a.wait(3121, p => p[16] === 1, 3000);
    if (d) drops.push([d.readFloatLE(6), d.readFloatLE(10), a.serverNow()]);
  }
  check(drops.length === n, `예고한 ${n}개 투하`);
  check(drops.every(d => pos.some(p => Math.abs(p[0] - d[0]) < 0.01 && Math.abs(p[1] - d[1]) < 0.01)), '투하 위치 = 예고 위치');
  check(drops.every(d => Math.abs(d[2] - dropAt) < 250), `투하 시각 = 예고 시각 (오차 ${drops.map(d => d[2] - dropAt)})`);

  console.log(failures ? `\n${failures} FAILED` : '\nALL PASSED');
  process.exit(failures ? 1 : 0);
}

const [, , mode, ...rest] = process.argv;
if (mode === 'make') makeMap(rest[0]);
else if (mode === 'run') run(rest[0], +rest[1]).catch(e => { console.error(e); process.exit(2); });
else if (mode === 'spawn') spawnTest(rest[0], +rest[1]).catch(e => { console.error(e); process.exit(2); });
else console.log('usage: make <out.bmp> | run <host> <port> | spawn <host> <port>');
