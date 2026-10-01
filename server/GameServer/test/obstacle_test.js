// [테스트 전용] 엄폐물(BMP) 서버 처리 테스트
//  1) node obstacle_test.js make <out.bmp>          : 테스트 맵 생성 (1024x1024, 24bit)
//  2) 서버 설정: obstacle_map=<out.bmp>, test_mode + test_spawn_sector 10,10 + test_spawn_radius 0 (중심 525,525), move_speed 200 (test/obs)
//  3) node obstacle_test.js run <host> <port> <out.bmp>
// 맵: WALL x[537,539) z[513,543)  /  LOW x[511,513) z[513,543)   스폰 (525,525)
'use strict';
const fs = require('fs');
const net = require('net');
const W = 1024, H = 1024;
const WALL = { x0: 537, x1: 539, z0: 513, z1: 543 };
const LOW = { x0: 511, x1: 513, z0: 513, z1: 543 };

function cellType(x, z) {
  if (x >= WALL.x0 && x < WALL.x1 && z >= WALL.z0 && z < WALL.z1) return 2;
  if (x >= LOW.x0 && x < LOW.x1 && z >= LOW.z0 && z < LOW.z1) return 1;
  return 0;
}

function makeMap(out) {
  const stride = Math.ceil(W * 3 / 4) * 4;
  const img = Buffer.alloc(54 + stride * H, 0xff);
  img.write('BM', 0); img.writeUInt32LE(img.length, 2); img.writeUInt32LE(54, 10);
  img.writeUInt32LE(40, 14); img.writeInt32LE(W, 18); img.writeInt32LE(H, 22);
  img.writeUInt16LE(1, 26); img.writeUInt16LE(24, 28); img.writeUInt32LE(0, 30); img.writeUInt32LE(stride * H, 34);
  for (let row = 0; row < H; row++) {           // bottom-up: row 0 = 이미지 맨 아래 = z 0
    const z = row;
    for (let x = 0; x < W; x++) {
      const t = cellType(x, z);
      const v = t === 2 ? 0 : t === 1 ? 128 : 255;
      const o = 54 + row * stride + x * 3;
      img[o] = img[o + 1] = img[o + 2] = v;
    }
  }
  fs.writeFileSync(out, img);
  console.log('map written', out);
}

function expectedHash() {
  let h = 2166136261 >>> 0;
  const N = 1500;
  for (let z = 0; z < N; z++) for (let x = 0; x < N; x++) {
    h ^= (x < W && z < H) ? cellType(x, z) : 0;
    h = Math.imul(h, 16777619) >>> 0;
  }
  return h >>> 0;
}

const sleep = ms => new Promise(r => setTimeout(r, ms));
let failures = 0;
const check = (c, m) => { console.log(`${c ? '  ok  ' : '  FAIL'} ${m}`); if (!c) failures++; };

function pk(type, fill) { const b = Buffer.alloc(128); b.writeUInt16LE(type, 0); const n = fill ? fill(b, 2) : 2; const p = b.subarray(0, n); return Buffer.concat([Buffer.from([0x77, p.length & 255, p.length >> 8, 0, 0]), p]); }

class Bot {
  constructor(host, port, name) { this.host = host; this.port = port; this.name = name; this.msgs = []; this.buf = Buffer.alloc(0); this.t0 = Date.now(); this.seq = 0; this.shot = 0; }
  now() { return Date.now() - this.t0; }
  connect() { return new Promise(r => { this.s = net.connect(this.port, this.host, r); this.s.on('data', d => { this.buf = Buffer.concat([this.buf, d]); while (this.buf.length >= 5) { const l = this.buf.readUInt16LE(1); if (this.buf.length < 5 + l) break; this.msgs.push(Buffer.from(this.buf.subarray(5, 5 + l))); this.buf = this.buf.subarray(5 + l); } }); }); }
  async wait(type, pred = () => true, ms = 1500) { const end = Date.now() + ms; while (Date.now() < end) { const i = this.msgs.findIndex(p => p.readUInt16LE(0) === type && pred(p)); if (i >= 0) return this.msgs.splice(i, 1)[0]; await sleep(10); } return null; }
  async enter() {
    this.s.write(pk(3000, (b, o) => { b.writeUInt32LE(7, o); o += 4; for (let i = 0; i < 12; i++) { b.writeUInt16LE(i < this.name.length ? this.name.charCodeAt(i) : 0, o); o += 2; } return o; }));
    const e = await this.wait(3100);
    this.id = e.readUInt32LE(3); this.x = e.readFloatLE(8); this.z = e.readFloatLE(12); this.offset = e.readUInt32LE(29) - this.now(); this.mapHash = e.readUInt32LE(57);
    return e;
  }
  move(x, z, vx, vz) { this.s.write(pk(3001, (b, o) => { b.writeFloatLE(x, o); b.writeFloatLE(z, o + 4); b.writeFloatLE(vx, o + 8); b.writeFloatLE(vz, o + 12); b.writeFloatLE(0, o + 16); b.writeUInt16LE(++this.seq, o + 20); return o + 22; })); }
  // 목표까지 5m씩 이동 (속도 100 보고). 보정 받으면 멈추고 false
  async walkTo(tx, tz) {
    while (Math.hypot(tx - this.x, tz - this.z) > 0.01) {
      const d = Math.hypot(tx - this.x, tz - this.z), st = Math.min(5, d);
      const nx = this.x + (tx - this.x) / d * st, nz = this.z + (tz - this.z) / d * st;
      this.move(nx, nz, (tx - this.x) / d * 100, (tz - this.z) / d * 100);
      const c = await this.wait(3105, () => true, 60);
      if (c) { this.x = c.readFloatLE(2); this.z = c.readFloatLE(6); return false; }
      this.x = nx; this.z = nz;
    }
    this.move(this.x, this.z, 0, 0);
    return true;
  }
  async shootAt(target) {
    const dx = target.x - this.x, dz = target.z - this.z, dist = Math.hypot(dx, dz);
    const seq = ++this.shot, vt = Math.floor(this.now() + this.offset - 100);
    this.s.write(pk(3002, (b, o) => { b.writeUInt32LE(seq, o); b[o + 4] = 1; b.writeFloatLE(this.x, o + 5); b.writeFloatLE(this.z, o + 9); b.writeFloatLE(dx / dist, o + 13); b.writeFloatLE(dz / dist, o + 17); b.writeUInt32LE(vt >>> 0, o + 21); b.writeUInt16LE(0, o + 25); return o + 27; }));
    await sleep(dist / 100 * 1000);
    const hx = target.x - dx / dist * 0.4, hz = target.z - dz / dist * 0.4;
    this.s.write(pk(3003, (b, o) => { b[o] = 1; b.writeUInt32LE(seq, o + 1); b[o + 5] = 0; b.writeUInt32LE(target.id, o + 6); b.writeFloatLE(hx, o + 10); b.writeFloatLE(hz, o + 14); return o + 18; }));
    return seq;
  }
}

async function run(host, port) {
  const a = new Bot(host, port, 'A'), b = new Bot(host, port, 'B');
  await a.connect(); await a.enter(); await b.connect(); await b.enter();
  const eh = expectedHash();
  check(a.mapHash === eh, `SC_ENTER_GAME MapHash 0x${a.mapHash.toString(16)} == 기대값 0x${eh.toString(16)}`);
  check(Math.abs(a.x - 525) < 0.01 && Math.abs(a.z - 525) < 0.01, `A 스폰 (525,525) → (${a.x.toFixed(1)},${a.z.toFixed(1)})`);

  console.log('== 이동 차단');
  const okWall = await a.walkTo(543, 525);
  check(!okWall && a.x < 537, `벽 통과 이동 → 보정 (x=${a.x.toFixed(2)})`);
  await a.walkTo(525, 525);
  const okLow = await a.walkTo(503, 525);
  check(!okLow && a.x > 513, `낮은 엄폐물 통과 이동 → 보정 (x=${a.x.toFixed(2)})`);
  await a.walkTo(525, 525);

  console.log('== 사격 차단');
  check(await b.walkTo(525, 553) && await b.walkTo(548, 553) && await b.walkTo(548, 525), 'B가 벽을 돌아 동쪽(548,525)으로 이동');
  await sleep(600);
  let seq = await a.shootAt(b);
  check((await b.wait(3107, p => p.readUInt32LE(10) === seq, 800)) === null, '벽 너머 피격 보고 → 거부');
  check(await b.walkTo(548, 553) && await b.walkTo(503, 553) && await b.walkTo(503, 525), 'B가 서쪽(503,525)으로 이동');
  await sleep(600);
  seq = await a.shootAt(b);
  const dmg = await b.wait(3107, p => p.readUInt32LE(10) === seq, 800);
  check(dmg !== null, '낮은 엄폐물 너머 피격 → 인정 (총알 통과)');

  console.log(failures ? `\n${failures} FAILED` : '\nALL PASSED');
  process.exit(failures ? 1 : 0);
}

const [, , mode, ...rest] = process.argv;
if (mode === 'make') makeMap(rest[0]);
else if (mode === 'run') run(rest[0], +rest[1]).catch(e => { console.error(e); process.exit(2); });
else console.log('usage: make <out.bmp> | run <host> <port>');
