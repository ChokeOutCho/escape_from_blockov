// [테스트 전용] v6 아이템·구르기·붕대·가방·에어드랍 시나리오 테스트 (Node.js, 의존성 없음)
// 사용: node item_test.js [host] [port]
//  서버는 test/item 설정(스폰 1개, 에어드랍 3초 주기, 가방 8초)으로 띄워야 한다.
'use strict';
const net = require('net');
const HOST = process.argv[2] || '127.0.0.1';
const PORT = parseInt(process.argv[3] || '10502', 10);

const T = {
  CS_ENTER_GAME: 3000, CS_MOVE: 3001, CS_FIRE: 3002, CS_HIT_REPORT: 3003, CS_PING: 3004,
  CS_ROLL: 3006, CS_SWITCH: 3007, CS_BANDAGE: 3008, CS_OPEN: 3009, CS_TAKE: 3010,
  SC_ENTER_GAME: 3100, SC_WEAPON_DEFS: 3101, SC_CREATE: 3102, SC_DELETE: 3103, SC_MOVE: 3104, SC_CORRECT: 3105,
  SC_FIRE: 3106, SC_DAMAGE: 3107, SC_DIE: 3108, SC_DEATH_RESULT: 3109, SC_PONG: 3113,
  SC_INVENTORY: 3115, SC_ROLL: 3116, SC_HP: 3117, SC_C_CREATE: 3118, SC_C_DELETE: 3119, SC_CONTENTS: 3120, SC_AIRDROP: 3121,
};
const NAMES = Object.fromEntries(Object.entries(T).map(([k, v]) => [v, k]));

let failures = 0;
function check(cond, msg) { console.log(`${cond ? '  ok  ' : '  FAIL'} ${msg}`); if (!cond) failures++; }
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

class W {
  constructor(type) { this.b = Buffer.alloc(600); this.o = 0; this.u16(type); }
  u8(v) { this.b.writeUInt8(v, this.o); this.o += 1; return this; }
  u16(v) { this.b.writeUInt16LE(v, this.o); this.o += 2; return this; }
  u32(v) { this.b.writeUInt32LE(v >>> 0, this.o); this.o += 4; return this; }
  f(v) { this.b.writeFloatLE(v, this.o); this.o += 4; return this; }
  name(s) { for (let i = 0; i < 12; i++) this.u16(i < s.length ? s.charCodeAt(i) : 0); return this; }
  packet() { const p = this.b.subarray(0, this.o); const h = Buffer.from([0x77, p.length & 0xff, p.length >> 8, 0, 0]); return Buffer.concat([h, p]); }
}

function parse(p) {
  const t = p.readUInt16LE(0); const m = { type: t, name: NAMES[t] || t, len: p.length };
  switch (t) {
    case T.SC_ENTER_GAME: Object.assign(m, { result: p[2], id: p.readUInt32LE(3), x: p.readFloatLE(8), z: p.readFloatLE(12), hp: p.readUInt16LE(16), weapon: p[28] }); break;
    case T.SC_WEAPON_DEFS: m.list = []; for (let i = 0; i < p[2]; i++) { const o = 3 + i * 36;
      m.list.push({ id: p[o], damage: p.readUInt16LE(o + 1), range: p.readFloatLE(o + 3), speed: p.readFloatLE(o + 7), interval: p.readUInt16LE(o + 11),
        spread: p.readFloatLE(o + 21), pellets: p[o + 25], pierce: p[o + 26], jitter: p.readFloatLE(o + 27), durability: p.readUInt16LE(o + 31), slot: p[o + 33] }); } break;
    case T.SC_CREATE: m.list = []; for (let i = 0; i < p[2]; i++) { const o = 3 + i * 54; m.list.push({ id: p.readUInt32LE(o), x: p.readFloatLE(o + 28), z: p.readFloatLE(o + 32) }); } break;
    case T.SC_CORRECT: Object.assign(m, { x: p.readFloatLE(2), z: p.readFloatLE(6), seq: p.readUInt16LE(10) }); break;
    case T.SC_FIRE: Object.assign(m, { shooter: p.readUInt32LE(2), seq: p.readUInt32LE(6), weapon: p[10], seed: p[27] }); break;
    case T.SC_DAMAGE: Object.assign(m, { attacker: p.readUInt32LE(2), victim: p.readUInt32LE(6), seq: p.readUInt32LE(10), dmg: p.readUInt16LE(14), hp: p.readUInt16LE(16) }); break;
    case T.SC_DIE: Object.assign(m, { victim: p.readUInt32LE(2), killer: p.readUInt32LE(6) }); break;
    case T.SC_PONG: Object.assign(m, { client: p.readUInt32LE(2), server: p.readUInt32LE(6) }); break;
    case T.SC_INVENTORY: Object.assign(m, { equipped: p[2], special: p[3], durability: p.readUInt16LE(4), bandages: p[6] }); break;
    case T.SC_ROLL: Object.assign(m, { id: p.readUInt32LE(2), sx: p.readFloatLE(6), sz: p.readFloatLE(10), ex: p.readFloatLE(14), ez: p.readFloatLE(18) }); break;
    case T.SC_HP: Object.assign(m, { id: p.readUInt32LE(2), hp: p.readUInt16LE(6) }); break;
    case T.SC_C_CREATE: m.list = []; for (let i = 0; i < p[2]; i++) { const o = 3 + i * 13; m.list.push({ id: p.readUInt32LE(o), ctype: p[o + 4], x: p.readFloatLE(o + 5), z: p.readFloatLE(o + 9) }); } break;
    case T.SC_C_DELETE: m.ids = []; for (let i = 0; i < p[2]; i++) m.ids.push(p.readUInt32LE(3 + i * 4)); break;
    case T.SC_CONTENTS: Object.assign(m, { id: p.readUInt32LE(2), special: p[6], durability: p.readUInt16LE(7), bandages: p[9] }); break;
    case T.SC_AIRDROP: Object.assign(m, { id: p.readUInt32LE(2), x: p.readFloatLE(6), z: p.readFloatLE(10), sx: p[14], sy: p[15], isNew: p[16] }); break;
  }
  return m;
}

class Bot {
  constructor(label) { this.label = label; this.msgs = []; this.all = []; this.buf = Buffer.alloc(0); this.closed = false; this.t0 = Date.now(); this.moveSeq = 0; this.offset = 0; this.shotSeq = 0; }
  connect() {
    return new Promise((res, rej) => {
      this.s = net.connect(PORT, HOST, res); this.s.on('error', rej);
      this.s.on('data', d => {
        this.buf = Buffer.concat([this.buf, d]);
        while (this.buf.length >= 5) {
          const len = this.buf.readUInt16LE(1); if (this.buf.length < 5 + len) break;
          const m = parse(this.buf.subarray(5, 5 + len)); this.buf = this.buf.subarray(5 + len);
          m.at = Date.now();
          this.msgs.push(m); this.all.push(m);
          if (m.type === T.SC_CORRECT) { this.x = m.x; this.z = m.z; }
        }
      });
      this.s.on('close', () => { this.closed = true; });
    });
  }
  send(w) { this.s.write(w.packet()); }
  async wait(type, pred = () => true, ms = 2000) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const i = this.msgs.findIndex(m => m.type === type && pred(m));
      if (i >= 0) return this.msgs.splice(i, 1)[0];
      await sleep(10);
    }
    return null;
  }
  drop(type) { this.msgs = this.msgs.filter(m => m.type !== type); }
  async enter(name) {
    this.send(new W(T.CS_ENTER_GAME).u32(6).name(name));
    this.me = await this.wait(T.SC_ENTER_GAME);
    this.x = this.me.x; this.z = this.me.z; this.id = this.me.id;
    const sent = this.now(); this.send(new W(T.CS_PING).u32(sent));
    const pg = await this.wait(T.SC_PONG);
    this.offset = pg.server + (this.now() - sent) / 2 - this.now();
    return this.me;
  }
  now() { return Date.now() - this.t0; }
  serverNow() { return Math.floor(this.now() + this.offset); }
  move(x, z, vx, vz) { this.x = x; this.z = z; this.send(new W(T.CS_MOVE).f(x).f(z).f(vx).f(vz).f(0).u16(++this.moveSeq)); }
  // 목표까지 100ms마다 1.3m (13 m/s, 달리기 이내), 도착 후 정지 패킷
  async walkTo(tx, tz, stopDist = 1.0) {
    for (let i = 0; i < 400; i++) {
      const dx = tx - this.x, dz = tz - this.z, d = Math.hypot(dx, dz);
      if (d <= stopDist) break;
      const step = Math.min(1.3, d - stopDist * 0.5);
      this.move(this.x + dx / d * step, this.z + dz / d * step, dx / d * 13, dz / d * 13);
      await sleep(100);
    }
    this.move(this.x, this.z, 0, 0);
  }
  fire(weapon, tx, tz, seed = 0) {
    const dx = tx - this.x, dz = tz - this.z, d = Math.hypot(dx, dz) || 1;
    const seq = ++this.shotSeq;
    this.send(new W(T.CS_FIRE).u32(seq).u8(weapon).f(this.x).f(this.z).f(dx / d).f(dz / d).u32(this.serverNow() - 60).u8(seed).u8(0));
    return { seq, dist: d, dx: dx / d, dz: dz / d };
  }
  hit(shot, target, pellet = 0) {
    this.send(new W(T.CS_HIT_REPORT).u8(1).u32(shot.seq).u8(pellet).u32(target.id).f(target.x - shot.dx * 0.4).f(target.z - shot.dz * 0.4));
  }
}

(async () => {
  console.log('== 입장 (v6 무기표·인벤토리)');
  const a = new Bot('A'); await a.connect(); await a.enter('Alpha');
  const wd = a.all.find(m => m.type === T.SC_WEAPON_DEFS);
  const W1 = wd && wd.list.find(w => w.id === 1), W2 = wd && wd.list.find(w => w.id === 2), W3 = wd && wd.list.find(w => w.id === 3);
  check(wd && wd.list.length === 3, `SC_WEAPON_DEFS 3종`);
  check(W1 && Math.abs(W1.jitter - 3) < 1e-4 && W1.durability === 0 && W1.slot === 2, '권총: 흔들림 3°, 내구도 무한, 슬롯 2');
  check(W2 && W2.pellets === 5 && Math.abs(W2.spread - 10) < 1e-4 && W2.jitter === 0 && W2.durability === 80 && W2.slot === 1 && W2.interval === 1000 && W2.damage === 20 && Math.abs(W2.range - 25) < 1e-4,
    '샷건: 5발 ±5°, 산탄당 20, 1초, 내구도 80, 사거리 25');
  check(W3 && W3.damage === 60 && W3.interval === 2000 && W3.durability === 50 && Math.abs(W3.range - 45) < 1e-4 && Math.abs(W3.speed - 200) < 1e-4, '저격총: 60, 2초, 내구도 50, 사거리 45/탄속 200');
  const inv0 = await a.wait(T.SC_INVENTORY);
  check(inv0 && inv0.equipped === 2 && inv0.special === 0 && inv0.bandages === 2, `SC_INVENTORY 초기 (권총, 특수 총 없음, 붕대 2) → ${inv0 && JSON.stringify(inv0)}`);
  const order = a.all.map(m => m.type).filter(t => [T.SC_WEAPON_DEFS, T.SC_INVENTORY].includes(t));
  check(order[0] === T.SC_WEAPON_DEFS && order[1] === T.SC_INVENTORY, '입장 순서: WEAPON_DEFS → INVENTORY');

  const b = new Bot('B'); await b.connect(); await b.enter('Bravo');
  await a.wait(T.SC_CREATE, m => m.list.some(x => x.id === b.id));
  // A와 B 사이 거리를 8m로 맞춘다
  await b.walkTo(a.x + 8, a.z, 0.3);
  await sleep(300);

  console.log('== 무기 전환');
  a.send(new W(T.CS_SWITCH).u8(1));
  const resync = await a.wait(T.SC_INVENTORY, () => true, 800);
  check(resync && resync.equipped === 2, '특수 총 없이 1번 전환 → SC_INVENTORY 재동기화 (권총 유지)');
  a.fire(2, b.x, b.z);
  check((await b.wait(T.SC_FIRE, () => true, 500)) === null, '장착하지 않은 무기(샷건) 사격 거부');
  const s0 = a.fire(1, b.x, b.z, 77);
  const f0 = await b.wait(T.SC_FIRE, m => m.seq === s0.seq);
  check(f0 && f0.weapon === 1 && f0.seed === 77, `SC_FIRE가 CS_FIRE의 SpreadSeed를 그대로 전달 → ${f0 && f0.seed}`);
  await sleep(300);

  console.log('== 붕대');
  b.send(new W(T.CS_BANDAGE));
  check((await b.wait(T.SC_HP, () => true, 2500)) === null, '체력 가득 → 붕대 사용 안 됨');
  // B에게 2발 → 60
  for (let i = 0; i < 2; i++) {
    const s = a.fire(1, b.x, b.z); await sleep(s.dist / 100 * 1000 + 30); a.hit(s, b);
    await b.wait(T.SC_DAMAGE, m => m.seq === s.seq, 800); await sleep(280);
  }
  b.drop(T.SC_DAMAGE);
  // 사용 후 0.5초 뒤 사격 → 취소
  b.send(new W(T.CS_BANDAGE)); await sleep(500);
  b.fire(1, a.x, a.z);
  check((await b.wait(T.SC_HP, () => true, 2500)) === null, '붕대 사용 중 사격 → 취소 (회복 없음)');
  b.send(new W(T.CS_BANDAGE));
  const t0 = Date.now();
  const hpB = await b.wait(T.SC_HP, m => m.id === b.id, 3000);
  const took = Date.now() - t0;
  check(hpB && hpB.hp === 100, `붕대 2초 사용 → HP 60+50 → 100 (SC_HP ${hpB && hpB.hp}, ${took}ms)`);
  check(took >= 1800 && took <= 2600, `붕대 사용 시간 약 2초 (${took}ms)`);
  const hpA = await a.wait(T.SC_HP, m => m.id === b.id, 500);
  check(hpA && hpA.hp === 100, '주변(A)도 SC_HP 수신');
  const invB = await b.wait(T.SC_INVENTORY, m => m.bandages === 1, 500);
  check(invB !== null, 'SC_INVENTORY 붕대 2 → 1');

  console.log('== 구르기');
  await sleep(200);
  a.drop(T.SC_CORRECT);
  const r0x = a.x, r0z = a.z;
  a.send(new W(T.CS_ROLL).f(a.x).f(a.z).f(0).f(1));
  const roll = await b.wait(T.SC_ROLL, m => m.id === a.id, 800);
  check(roll && Math.abs(roll.sx - r0x) < 0.01 && Math.abs(roll.ez - (r0z + 9)) < 0.3 && Math.abs(roll.ex - r0x) < 0.01,
    `SC_ROLL 3x3 방송: (${roll && roll.sx.toFixed(1)},${roll && roll.sz.toFixed(1)}) → (${roll && roll.ex.toFixed(1)},${roll && roll.ez.toFixed(1)}) 약 9m`);
  check((await a.wait(T.SC_ROLL, () => true, 300)) === null, '본인은 SC_ROLL 받지 않음');
  a.x = r0x; a.z = r0z + 9;
  await sleep(100);
  a.send(new W(T.CS_ROLL).f(a.x).f(a.z).f(1).f(0));
  const cd = await a.wait(T.SC_CORRECT, () => true, 800);
  check(cd && Math.abs(cd.z - (r0z + 9)) < 0.3, '쿨타임(3초) 중 구르기 → 거부 + 서버 위치로 보정');
  check((await b.wait(T.SC_ROLL, () => true, 300)) === null, '거부된 구르기는 방송 없음');
  a.move(a.x, a.z, 0, 0);
  check((await a.wait(T.SC_CORRECT, () => true, 400)) === null, '구르기 도착점에서 이동 → 보정 없음');
  await sleep(3000);
  const r1x = a.x, r1z = a.z;
  a.send(new W(T.CS_ROLL).f(a.x).f(a.z).f(0).f(-1));
  const roll2 = await b.wait(T.SC_ROLL, m => m.id === a.id, 800);
  check(roll2 !== null, '쿨타임 후 다시 구르기 가능');
  await sleep(350);
  a.move(r1x, r1z, 0, 0);   // 도착점이 아닌 시작점에서 이동 보고 → 서버 계산과 9m 차이
  const cor2 = await a.wait(T.SC_CORRECT, () => true, 800);
  check(cor2 && Math.abs(cor2.z - (r1z - 9)) < 0.3, `구르기 후 도착점 불일치(>1m) → SC_POSITION_CORRECT (${cor2 && cor2.z.toFixed(1)})`);
  a.x = cor2 ? cor2.x : r1x; a.z = cor2 ? cor2.z : r1z - 9;
  a.move(a.x, a.z, 0, 0);
  await sleep(300);

  console.log('== 가방 (사망 시)');
  b.drop(T.SC_DAMAGE);
  // B는 붕대 1개. A가 권총 5발로 처치
  let killed = false;
  for (let i = 0; i < 6 && !killed; i++) {
    const s = a.fire(1, b.x, b.z); await sleep(s.dist / 100 * 1000 + 30); a.hit(s, b);
    const dm = await a.wait(T.SC_DAMAGE, m => m.seq === s.seq, 800);
    if (dm && dm.hp === 0) killed = true;
    await sleep(280);
  }
  check(killed, 'B 처치');
  const bag = await a.wait(T.SC_C_CREATE, m => m.list.some(c => c.ctype === 1), 1000);
  const bagItem = bag && bag.list[0];
  check(bagItem && Math.abs(bagItem.x - b.x) < 0.5 && Math.abs(bagItem.z - b.z) < 0.5, `사망 위치에 가방 SC_CONTAINER_CREATE (type 1) id ${bagItem && bagItem.id}`);
  const c = new Bot('C'); await c.connect(); await c.enter('Charlie');
  const cBag = await c.wait(T.SC_C_CREATE, m => m.list.some(x => x.id === bagItem.id), 1000);
  check(cBag !== null, '새로 입장한 C가 입장 시퀀스에서 시야 안 가방 수신');
  // 이동 직후 열기 → 거부
  await a.walkTo(bagItem.x, bagItem.z, 1.0);
  a.send(new W(T.CS_OPEN).u32(bagItem.id));
  check((await a.wait(T.SC_CONTENTS, () => true, 500)) === null, '움직인 직후 열기 → 거부 (1초 정지 필요)');
  await sleep(1000);
  a.send(new W(T.CS_OPEN).u32(bagItem.id));
  const bc = await a.wait(T.SC_CONTENTS, m => m.id === bagItem.id, 800);
  check(bc && bc.special === 0 && bc.bandages === 1, `1초 정지 후 열기 → 내용물 (특수 총 없음, 붕대 1) ${bc && JSON.stringify(bc)}`);
  a.send(new W(T.CS_TAKE).u32(bagItem.id).u8(1));
  const bcSame = await a.wait(T.SC_CONTENTS, m => m.id === bagItem.id, 800);
  check(bcSame && bcSame.bandages === 1, '없는 특수 총 획득 요청 → 최신 내용 재전송');
  a.send(new W(T.CS_TAKE).u32(bagItem.id).u8(3));
  const invA1 = await a.wait(T.SC_INVENTORY, m => m.bandages === 3, 800);
  check(invA1 !== null, '가방 붕대 획득 → A 붕대 3');
  const bc2 = await a.wait(T.SC_CONTENTS, m => m.id === bagItem.id && m.bandages === 0, 800);
  check(bc2 !== null, '내용 갱신 SC_CONTAINER_CONTENTS (붕대 0)');
  check((await a.wait(T.SC_C_DELETE, m => m.ids.includes(bagItem.id), 500)) === null, '빈 가방은 즉시 사라지지 않음');

  console.log('== 에어드랍');
  const ad1 = await a.wait(T.SC_AIRDROP, m => m.isNew === 1, 4000);
  check(ad1 !== null, `에어드랍 공지 SC_AIRDROP(IsNew=1) 섹터 (${ad1 && ad1.sx},${ad1 && ad1.sy})`);
  check(ad1 && ad1.sx === Math.floor(ad1.x / 50) && ad1.sy === Math.floor(ad1.z / 50), '섹터 번호가 위치와 일치');
  const asx = Math.floor(a.x / 50), asy = Math.floor(a.z / 50);
  check(ad1 && Math.max(Math.abs(ad1.sx - asx), Math.abs(ad1.sy - asy)) <= 2, `첫 에어드랍은 인원이 있는 묶음 (A 섹터 ${asx},${asy})`);
  check(c.all.some(m => m.type === T.SC_AIRDROP && m.id === ad1.id), '같은 방의 C도 에어드랍 수신 (방 전체)');
  const ad2 = await a.wait(T.SC_AIRDROP, m => m.isNew === 1, 4000);
  check(ad2 && Math.max(Math.abs(ad2.sx - ad1.sx), Math.abs(ad2.sy - ad1.sy)) >= 3, `두 번째 에어드랍은 다른 묶음 (${ad2 && ad2.sx},${ad2 && ad2.sy})`);
  check((await a.wait(T.SC_AIRDROP, m => m.isNew === 1, 3500)) === null, '방에 2개 있으면 다음 회차 건너뜀');
  const d = new Bot('D'); await d.connect(); await d.enter('Delta');
  const dAd = d.all.filter(m => m.type === T.SC_AIRDROP && m.isNew === 0).map(m => m.id);
  check(dAd.includes(ad1.id) && dAd.includes(ad2.id), '입장 시 기존 에어드랍 2개를 IsNew=0으로 수신');

  // A와 D가 첫 에어드랍으로 이동
  await Promise.all([a.walkTo(ad1.x, ad1.z, 1.2), d.walkTo(ad1.x + 0.8, ad1.z + 0.8, 1.2)]);
  a.move(a.x + 0.05, a.z, 0, 0); d.move(d.x + 0.05, d.z, 0, 0);   // 둘 다 같은 시각에 멈춤
  await sleep(1000);
  a.send(new W(T.CS_OPEN).u32(ad1.id));
  check((await a.wait(T.SC_CONTENTS, () => true, 500)) === null, '에어드랍은 1초로는 열리지 않음 (2초)');
  await sleep(1000);
  a.send(new W(T.CS_OPEN).u32(ad1.id));
  d.send(new W(T.CS_OPEN).u32(ad1.id));
  const ac = await a.wait(T.SC_CONTENTS, m => m.id === ad1.id, 800);
  const dc = await d.wait(T.SC_CONTENTS, m => m.id === ad1.id, 800);
  const special = ac && ac.special;
  const maxDur = special === 2 ? 80 : 50;
  check(ac && (special === 2 || special === 3) && ac.durability === maxDur && ac.bandages === 5, `에어드랍 내용물: 특수 총 ${special} (내구도 ${ac && ac.durability}), 붕대 5`);
  check(dc !== null, '여러 명이 동시에 열 수 있음 (D)');
  a.send(new W(T.CS_TAKE).u32(ad1.id).u8(1));
  d.send(new W(T.CS_TAKE).u32(ad1.id).u8(1));
  const invA2 = await a.wait(T.SC_INVENTORY, m => m.special === special, 800);
  check(invA2 && invA2.durability === maxDur && invA2.equipped === 2, `A 특수 총 획득 (장착은 권총 유지) ${invA2 && JSON.stringify(invA2)}`);
  const dInv = await d.wait(T.SC_INVENTORY, m => m.special !== 0, 500);
  check(dInv === null, '같은 특수 총은 먼저 도착한 요청(A)만 성공');
  const dSeen = await d.wait(T.SC_CONTENTS, m => m.id === ad1.id && m.special === 0, 800);
  check(dSeen !== null, '열어 둔 D에게 내용 갱신 (특수 총 없음)');
  a.send(new W(T.CS_TAKE).u32(ad1.id).u8(3));
  const invA3 = await a.wait(T.SC_INVENTORY, m => m.bandages === 5, 800);
  check(invA3 !== null, 'A 붕대 3 → 5 (최대 5, 2개만 가져감)');
  const left = await d.wait(T.SC_CONTENTS, m => m.id === ad1.id && m.bandages === 3, 800);
  check(left !== null, '최대를 넘는 붕대 3개는 에어드랍에 남음');
  d.send(new W(T.CS_TAKE).u32(ad1.id).u8(3));
  const delA = await a.wait(T.SC_C_DELETE, m => m.ids.includes(ad1.id), 1000);
  const delC = await c.wait(T.SC_C_DELETE, m => m.ids.includes(ad1.id), 1000);
  check(delA !== null && delC !== null, '에어드랍이 비면 즉시 제거 (방 전체 SC_CONTAINER_DELETE)');
  const ad3 = await a.wait(T.SC_AIRDROP, m => m.isNew === 1, 4000);
  check(ad3 !== null, '제거 후 다음 회차에 새 에어드랍');

  console.log('== 특수 총 사용');
  a.send(new W(T.CS_SWITCH).u8(1));
  await sleep(100);
  const s1 = a.fire(special, d.x, d.z, 5);
  const df = await d.wait(T.SC_FIRE, m => m.seq === s1.seq, 800);
  check(df && df.weapon === special, `1번 전환 후 특수 총(${special}) 사격`);
  a.fire(1, d.x, d.z);
  check((await d.wait(T.SC_FIRE, () => true, 500)) === null, '특수 총 장착 중 권총 사격 거부');
  if (special === 2) {
    await sleep(Math.min(1500, s1.dist / 100 * 1000) + 30);
    for (let p = 0; p < 5; p++) a.hit(s1, d, p);
    let total = 0; for (let p = 0; p < 5; p++) { const dm = await d.wait(T.SC_DAMAGE, m => m.seq === s1.seq, 500); if (dm) total += dm.dmg; }
    check(total === 100, `샷건 산탄 5발 명중 → 20 x 5 = ${total}`);
  } else {
    await sleep(Math.min(1500, s1.dist / 200 * 1000) + 30);
    a.hit(s1, d, 0);
    const dm = await d.wait(T.SC_DAMAGE, m => m.seq === s1.seq, 800);
    check(dm && dm.dmg === 60, `저격총 명중 → 60`);
  }

  console.log('== 가방 만료');
  const exp = await c.wait(T.SC_C_DELETE, m => m.ids.includes(bagItem.id), 12000);
  const age = exp ? exp.at - bag.at : -1;
  check(exp !== null && age >= 7500 && age <= 9000, `가방 생성 8초(bag_lifetime_ms) 뒤 제거 (${age}ms, 시야 안 C 기준)`);

  console.log(failures ? `\n${failures} FAILED` : '\nALL PASSED');
  process.exit(failures ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
