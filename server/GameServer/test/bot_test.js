// [테스트 전용] GameServer 프로토콜 시나리오 테스트 (Node.js, 의존성 없음)
// 사용: node bot_test.js [host] [port]
//  서버는 test/test_config 설정(스폰 1개, 짧은 타임아웃)으로 띄워야 한다.
'use strict';
const net = require('net');
const HOST = process.argv[2] || '127.0.0.1';
const PORT = parseInt(process.argv[3] || '10301', 10);

const T = { CS_ENTER_GAME: 3000, CS_MOVE: 3001, CS_FIRE: 3002, CS_HIT_REPORT: 3003, CS_PING: 3004, CS_HEARTBEAT: 3005,
  SC_ENTER_GAME: 3100, SC_WEAPON_DEFS: 3101, SC_CREATE: 3102, SC_DELETE: 3103, SC_MOVE: 3104, SC_CORRECT: 3105,
  SC_FIRE: 3106, SC_DAMAGE: 3107, SC_DIE: 3108, SC_DEATH_RESULT: 3109, SC_SCORE: 3110, SC_RANK: 3111, SC_KICK: 3112, SC_PONG: 3113 };
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
function readName(b, o) { let s = ''; for (let i = 0; i < 12; i++) { const c = b.readUInt16LE(o + i * 2); if (!c) break; s += String.fromCharCode(c); } return s; }

function parse(p) {
  const t = p.readUInt16LE(0); const m = { type: t, name: NAMES[t] || t, len: p.length };
  switch (t) {
    case T.SC_ENTER_GAME: Object.assign(m, { result: p[2], id: p.readUInt32LE(3), room: p[7], x: p.readFloatLE(8), z: p.readFloatLE(12),
      hp: p.readUInt16LE(16), maxHp: p.readUInt16LE(18), speed: p.readFloatLE(20), radius: p.readFloatLE(24), weapon: p[28],
      serverTime: p.readUInt32LE(29), myName: readName(p, 33),
      mapHash: p.length >= 61 ? p.readUInt32LE(57) : 0, sprint: p.length >= 65 ? p.readFloatLE(61) : 0 }); break;
    case T.SC_WEAPON_DEFS: m.count = p[2]; m.first = { id: p[3], damage: p.readUInt16LE(4), range: p.readFloatLE(6), speed: p.readFloatLE(10),
      interval: p.readUInt16LE(14), radius: p.readFloatLE(16), pellets: p[28], pierce: p[29] }; break;
    case T.SC_CREATE: m.list = []; for (let i = 0; i < p[2]; i++) { const o = 3 + i * 54; m.list.push({ id: p.readUInt32LE(o), name: readName(p, o + 4), x: p.readFloatLE(o + 28), z: p.readFloatLE(o + 32), hp: p.readUInt16LE(o + 48) }); } break;
    case T.SC_DELETE: m.ids = []; for (let i = 0; i < p[2]; i++) m.ids.push(p.readUInt32LE(3 + i * 4)); break;
    case T.SC_MOVE: Object.assign(m, { id: p.readUInt32LE(2), x: p.readFloatLE(6), z: p.readFloatLE(10) }); break;
    case T.SC_CORRECT: Object.assign(m, { x: p.readFloatLE(2), z: p.readFloatLE(6), seq: p.readUInt16LE(10) }); break;
    case T.SC_FIRE: Object.assign(m, { shooter: p.readUInt32LE(2), seq: p.readUInt32LE(6) }); break;
    case T.SC_DAMAGE: Object.assign(m, { attacker: p.readUInt32LE(2), victim: p.readUInt32LE(6), seq: p.readUInt32LE(10), dmg: p.readUInt16LE(14), hp: p.readUInt16LE(16) }); break;
    case T.SC_DIE: Object.assign(m, { victim: p.readUInt32LE(2), killer: p.readUInt32LE(6) }); break;
    case T.SC_DEATH_RESULT: Object.assign(m, { killerName: readName(p, 2), score: p.readUInt32LE(26), kills: p.readUInt32LE(30), sec: p.readUInt32LE(34) }); break;
    case T.SC_SCORE: Object.assign(m, { score: p.readUInt32LE(2), kills: p.readUInt16LE(6) }); break;
    case T.SC_RANK: m.list = []; for (let i = 0; i < p[2]; i++) { const o = 3 + i * 33; m.list.push({ rank: p[o], id: p.readUInt32LE(o + 1), name: readName(p, o + 5), score: p.readUInt32LE(o + 29) }); } break;
    case T.SC_KICK: m.reason = p[2]; break;
    case T.SC_PONG: Object.assign(m, { client: p.readUInt32LE(2), server: p.readUInt32LE(6) }); break;
  }
  return m;
}

class Bot {
  constructor(label) { this.label = label; this.msgs = []; this.buf = Buffer.alloc(0); this.closed = false; this.t0 = Date.now(); }
  connect() {
    return new Promise((res, rej) => {
      this.s = net.connect(PORT, HOST, res); this.s.on('error', rej);
      this.s.on('data', d => {
        this.buf = Buffer.concat([this.buf, d]);
        while (this.buf.length >= 5) {
          if (this.buf[0] !== 0x77) throw new Error('bad code');
          const len = this.buf.readUInt16LE(1); if (this.buf.length < 5 + len) break;
          const m = parse(this.buf.subarray(5, 5 + len)); this.buf = this.buf.subarray(5 + len);
          this.msgs.push(m);
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
  async enter(name) { this.send(new W(T.CS_ENTER_GAME).u32(4).name(name)); this.me = await this.wait(T.SC_ENTER_GAME); return this.me; }
  now() { return Date.now() - this.t0; }
}

(async () => {
  console.log('== 입장');
  const a = new Bot('A'); await a.connect();
  const ea = await a.enter('  홍길동  ');
  check(ea && ea.result === 0, 'A SC_ENTER_GAME OK');
  check(ea && ea.myName === '홍길동', `이름 정규화 (공백 제거) → "${ea && ea.myName}"`);
  check(ea && ea.hp === 100 && Math.abs(ea.speed - 12) < 1e-4 && ea.weapon === 1, 'HP/속도/무기 초기값');
  check(ea && ea.len === 65 && Math.abs(ea.sprint - 1.2) < 1e-4, `SC_ENTER_GAME 65B, 달리기 배율 ${ea && ea.sprint}`);
  const wd = await a.wait(T.SC_WEAPON_DEFS);
  check(wd && wd.count === 1 && wd.first.damage === 20 && Math.abs(wd.first.range - 64) < 1e-4, 'SC_WEAPON_DEFS');
  const rk0 = await a.wait(T.SC_RANK);
  check(rk0 !== null, 'SC_RANKING_TOP3 입장 시 수신');

  const b = new Bot('B'); await b.connect();
  const eb = await b.enter('');
  check(eb && eb.result === 0 && /^Guest\d{4}$/.test(eb.myName), `빈 이름 → ${eb && eb.myName}`);
  const bc = await b.wait(T.SC_CREATE);
  check(bc && bc.list.some(x => x.id === ea.id), 'B는 입장 시 A를 CREATE로 받음');
  const ac = await a.wait(T.SC_CREATE, m => m.list.some(x => x.id === eb.id));
  check(ac !== null, 'A는 B 입장 CREATE 수신');

  console.log('== Ping');
  a.send(new W(T.CS_PING).u32(12345));
  const pong = await a.wait(T.SC_PONG);
  check(pong && pong.client === 12345 && pong.server > 0, 'SC_PONG 에코');

  console.log('== 이동');
  let ax = ea.x, az = ea.z;
  const bx = eb.x, bz = eb.z;
  // A가 B 방향으로 0.5초 이동 (예산 내)
  const dx = bx - ax, dz = bz - az, dl = Math.hypot(dx, dz) || 1;
  for (let i = 1; i <= 5; i++) {
    ax += dx / dl * 1.0; az += dz / dl * 1.0;
    a.send(new W(T.CS_MOVE).f(ax).f(az).f(dx / dl * 10).f(dz / dl * 10).f(45).u16(i));
    await sleep(100);
  }
  const mv = await b.wait(T.SC_MOVE, m => m.id === ea.id);
  check(mv !== null, 'B가 A의 SC_MOVE 수신');
  check((await a.wait(T.SC_CORRECT, () => true, 300)) === null, '정상 이동은 보정 없음');
  // 달리기: 지나온 경로로 14 m/s(= 12 x 1.2 이내) 3스텝 되돌아가기 → 보정 없어야 함
  for (let i = 1; i <= 3; i++) {
    ax -= dx / dl * 1.4; az -= dz / dl * 1.4;
    a.send(new W(T.CS_MOVE).f(ax).f(az).f(-dx / dl * 14).f(-dz / dl * 14).f(45).u16(10 + i));
    await sleep(100);
  }
  check((await a.wait(T.SC_CORRECT, () => true, 300)) === null, '달리기 속도(14 m/s) 이동은 보정 없음');
  a.send(new W(T.CS_MOVE).f(ax + 500).f(az).f(0).f(0).f(0).u16(99));
  const cor = await a.wait(T.SC_CORRECT);
  check(cor && cor.seq === 99 && Math.abs(cor.x - ax) < 0.01, '순간이동 → SC_POSITION_CORRECT(원위치)');

  console.log('== 사격/피격/사망');
  // 서버 시각 추정 (ping 1회)
  const sent = a.now(); a.send(new W(T.CS_PING).u32(sent));
  const pg = await a.wait(T.SC_PONG);
  const offset = pg.server + (a.now() - sent) / 2 - a.now();
  const serverNow = () => Math.floor(a.now() + offset);
  await sleep(300); // B의 위치 이력이 쌓이도록
  const dist = Math.hypot(bx - ax, bz - az);
  check(dist < 60, `A-B 거리 ${dist.toFixed(1)} (사거리 64 이내)`);
  const dirx = (bx - ax) / dist, dirz = (bz - az) / dist;
  let seq = 0, damageSeen = 0, lastHp = 100;
  for (let shot = 0; shot < 5; shot++) {
    seq++;
    const vt = serverNow() - 100;
    a.send(new W(T.CS_FIRE).u32(seq).u8(1).f(ax).f(az).f(dirx).f(dirz).u32(vt).u16(0));
    const bf = await b.wait(T.SC_FIRE, m => m.seq === seq);
    if (shot === 0) check(bf && bf.shooter === ea.id, 'B가 SC_FIRE 수신 (연출용)');
    await sleep(Math.min(1500, dist / 60 * 1000));
    a.send(new W(T.CS_HIT_REPORT).u8(1).u32(seq).u8(0).u32(eb.id).f(bx - dirx * 0.4).f(bz - dirz * 0.4));
    const dmg = await b.wait(T.SC_DAMAGE, m => m.seq === seq);
    if (dmg) { damageSeen++; lastHp = dmg.hp; }
    await a.wait(T.SC_DAMAGE, m => m.seq === seq, 500);
    await sleep(270);
  }
  check(damageSeen === 5 && lastHp === 0, `피격 5회 → HP 0 (seen ${damageSeen}, hp ${lastHp})`);
  const die = await a.wait(T.SC_DIE);
  check(die && die.victim === eb.id && die.killer === ea.id, 'SC_PLAYER_DIE');
  const sc = await a.wait(T.SC_SCORE);
  check(sc && sc.score === 1 && sc.kills === 1, `SC_SCORE (+1) → ${sc && sc.score}`);
  const dr = await b.wait(T.SC_DEATH_RESULT);
  check(dr && dr.killerName === '홍길동', 'SC_DEATH_RESULT 처치자 이름');
  const rk = await a.wait(T.SC_RANK, m => m.list.length === 1 && m.list[0].id === ea.id && m.list[0].score === 1, 1500);
  check(rk !== null, 'SC_RANKING_TOP3 갱신 (A 1점)');
  await sleep(3500);
  check(b.closed, '사망 3초 후 서버가 끊음');

  console.log('== 중복 피격/위조 보고');
  const c = new Bot('C'); await c.connect(); const ec = await c.enter('Charlie');
  check(ec && ec.result === 0, 'C 입장');
  a.send(new W(T.CS_HIT_REPORT).u8(1).u32(seq).u8(0).u32(ec.id).f(ec.x).f(ec.z));
  check((await c.wait(T.SC_DAMAGE, () => true, 500)) === null, '만료/다른 대상 재사용 보고 거부');
  a.send(new W(T.CS_HIT_REPORT).u8(1).u32(777).u8(0).u32(ec.id).f(ec.x).f(ec.z));
  check((await c.wait(T.SC_DAMAGE, () => true, 500)) === null, '존재하지 않는 ShotSeq 거부');

  console.log('== 오류 처리');
  const v = new Bot('V'); await v.connect();
  v.send(new W(T.CS_ENTER_GAME).u32(1).name('old'));
  const ev = await v.wait(T.SC_ENTER_GAME);
  check(ev && ev.result === 2 && ev.len === 65, "버전 불일치 → Result 2 (65B)");
  await sleep(1300); check(v.closed, '거절 1초 후 끊김');

  const k = new Bot('K'); await k.connect(); await k.enter('Kick');
  k.send(new W(9999));
  const kick = await k.wait(T.SC_KICK);
  check(kick && kick.reason === 2, '알 수 없는 패킷 → SC_KICK(INVALID_PACKET)');

  const t = new Bot('T'); await t.connect();
  const tk = await t.wait(T.SC_KICK, () => true, 4000);
  check(tk && tk.reason === 1, '입장 타임아웃 → SC_KICK(TIMEOUT)');

  console.log(failures ? `\n${failures} FAILED` : '\nALL PASSED');
  process.exit(failures ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
