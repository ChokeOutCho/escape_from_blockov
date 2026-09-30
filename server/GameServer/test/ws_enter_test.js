// 웹서버 /ws 경유 입장 테스트 (브라우저와 같은 경로)
// 사용: node ws_enter_test.js ws://<host>:8090/ws [이름] [동시접속수=1]
'use strict';
const net = require('net'), crypto = require('crypto');
const url = new URL(process.argv[2] || 'ws://127.0.0.1:8090/ws');
const NAME = process.argv[3] || 'WsTest';
const COUNT = +(process.argv[4] || 1);

function enterPacket(name) {
  const p = Buffer.alloc(5 + 30);
  p[0] = 0x77; p.writeUInt16LE(30, 1);
  p.writeUInt16LE(3000, 5); p.writeUInt32LE(5, 7);
  const n = Buffer.from(name.slice(0, 12), 'utf16le'); n.copy(p, 11);
  return p;
}
function wsFrame(payload) {                      // 클라 → 서버: 바이너리, 마스크 필수
  const mask = crypto.randomBytes(4);
  const h = Buffer.from([0x82, 0x80 | payload.length]);
  const body = Buffer.from(payload.map((b, i) => b ^ mask[i & 3]));
  return Buffer.concat([h, mask, body]);
}
function one(idx) {
  return new Promise((resolve) => {
    const s = net.connect(+url.port || 80, url.hostname);
    const key = crypto.randomBytes(16).toString('base64');
    let buf = Buffer.alloc(0), upgraded = false, t0 = Date.now();
    const done = (r) => { s.destroy(); resolve(`#${idx} ${r} (${Date.now() - t0}ms)`); };
    setTimeout(() => done('TIMEOUT'), 8000);
    s.on('connect', () => s.write(`GET ${url.pathname} HTTP/1.1\r\nHost: ${url.host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n` +
      `Sec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\nOrigin: http://${url.host}\r\n\r\n`));
    s.on('error', (e) => done('ERROR ' + e.code));
    s.on('data', (d) => {
      buf = Buffer.concat([buf, d]);
      if (!upgraded) {
        const e = buf.indexOf('\r\n\r\n'); if (e < 0) return;
        const head = buf.slice(0, e).toString(); buf = buf.slice(e + 4);
        if (!/^HTTP\/1.1 101/.test(head)) return done('HANDSHAKE ' + head.split('\r\n')[0]);
        upgraded = true; s.write(wsFrame(enterPacket(COUNT > 1 ? NAME + idx : NAME)));
      }
      // 서버 프레임 (마스크 없음) → 페이로드 모으기
      while (buf.length >= 2) {
        let len = buf[1] & 0x7f, off = 2;
        if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); off = 4; }
        if (buf.length < off + len) return;
        const op = buf[0] & 0x0f;
        const pl = buf.slice(off, off + len); buf = buf.slice(off + len);
        if (op === 8) return done('CLOSED');
        if (pl.length >= 8 && pl[0] === 0x77 && pl.readUInt16LE(5) === 3100) {
          const r = pl[7];
          return done(r === 0 ? `ENTER OK playerId=${pl.readUInt32LE(8)} room=${pl[12]} pos=(${pl.readFloatLE(13).toFixed(1)}, ${pl.readFloatLE(17).toFixed(1)})` + (pl.length >= 70 ? ` sprint=${pl.readFloatLE(66).toFixed(2)}` : '') : `ENTER FAIL result=${r}`);
        }
      }
    });
  });
}
(async () => {
  const res = await Promise.all(Array.from({ length: COUNT }, (_, i) => one(i + 1)));
  res.forEach((r) => console.log(r));
  process.exit(res.every((r) => r.includes('ENTER OK')) ? 0 : 1);
})();
