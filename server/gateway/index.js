// escape_from_blockov WS <-> TCP gateway (game-spec 14.1, 1단계 전용)
// - 외부 패키지 없음 (Node.js 18+ 내장 모듈만 사용). 실행: node index.js
// - WebSocket 연결 1개 <-> 게임 서버 TCP 연결 1개
// - 바이트를 해석하지 않고 그대로 중계 (클라 -> 서버: WS 메시지 페이로드 그대로, 서버 -> 클라: TCP 청크 = WS 메시지 1개)
// - 텍스트 프레임 거부, 메시지 최대 크기 제한, IP당 동시 연결 수 제한, Origin 화이트리스트(옵션), ping/pong/close 처리
//
// 환경변수: GW_HOST(0.0.0.0, 127.0.0.1이면 이 PC에서만 접속 가능) GW_PORT(8080) GAME_HOST(127.0.0.1) GAME_PORT(10301) MAX_MSG(4096) MAX_PER_IP(3)
//          ALLOWED_ORIGINS(쉼표 구분, 비우면 전부 허용) TRUST_XFF(1이면 X-Forwarded-For를 클라 IP로 사용: 앞단 프록시가 있을 때만)

'use strict';
const http = require('http');
const net = require('net');
const crypto = require('crypto');

const GW_HOST = process.env.GW_HOST || '0.0.0.0';
const GW_PORT = parseInt(process.env.GW_PORT || '8080', 10);
const GAME_HOST = process.env.GAME_HOST || '127.0.0.1';
const GAME_PORT = parseInt(process.env.GAME_PORT || '10301', 10);
const MAX_MSG = parseInt(process.env.MAX_MSG || '4096', 10);
const MAX_PER_IP = parseInt(process.env.MAX_PER_IP || '3', 10);
const ALLOWED_ORIGINS = (process.env.ALLOWED_ORIGINS || '').split(',').map(s => s.trim()).filter(Boolean);
const TRUST_XFF = process.env.TRUST_XFF === '1';
const WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

const perIp = new Map();

function clientIp(req) {
  const xff = TRUST_XFF && req.headers['x-forwarded-for'];
  return (xff ? xff.split(',')[0].trim() : req.socket.remoteAddress) || 'unknown';
}

function reject(socket, code, text) {
  socket.end(`HTTP/1.1 ${code} ${text}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
}

// 서버 -> 클라 프레임 (FIN=1, 마스크 없음)
function frame(opcode, payload) {
  const len = payload.length;
  let header;
  if (len < 126) {
    header = Buffer.from([0x80 | opcode, len]);
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | opcode; header[1] = 126; header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | opcode; header[1] = 127; header.writeBigUInt64BE(BigInt(len), 2);
  }
  return Buffer.concat([header, payload]);
}

const server = http.createServer((req, res) => {
  res.writeHead(426, { 'Content-Type': 'text/plain' });
  res.end('WebSocket only\n');
});

server.on('upgrade', (req, socket, head) => {
  const key = req.headers['sec-websocket-key'];
  if ((req.headers.upgrade || '').toLowerCase() !== 'websocket' || !key) return reject(socket, 400, 'Bad Request');
  if (ALLOWED_ORIGINS.length && !ALLOWED_ORIGINS.includes(req.headers.origin || '')) return reject(socket, 403, 'Forbidden');
  const ip = clientIp(req);
  if ((perIp.get(ip) || 0) >= MAX_PER_IP) return reject(socket, 429, 'Too Many Requests');

  const accept = crypto.createHash('sha1').update(key + WS_GUID).digest('base64');
  socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
               `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
  socket.setNoDelay(true);
  perIp.set(ip, (perIp.get(ip) || 0) + 1);
  const tag = `${ip}#${Math.random().toString(36).slice(2, 7)}`;

  const tcp = net.connect({ host: GAME_HOST, port: GAME_PORT });
  tcp.setNoDelay(true);
  const pending = [];
  let tcpReady = false;
  let closed = false;

  const closeBoth = (why, wsCode = 1000) => {
    if (closed) return;
    closed = true;
    const n = (perIp.get(ip) || 1) - 1;
    if (n <= 0) perIp.delete(ip); else perIp.set(ip, n);
    tcp.destroy();
    if (!socket.destroyed) {
      const reason = Buffer.from(why.slice(0, 100));
      const body = Buffer.alloc(2 + reason.length);
      body.writeUInt16BE(wsCode, 0); reason.copy(body, 2);
      socket.end(frame(0x8, body));
    }
    console.log(`[gw] close ${tag}: ${why}`);
  };

  const toServer = (data) => { if (tcpReady) tcp.write(data); else pending.push(data); };

  tcp.on('connect', () => {
    tcpReady = true;
    for (const b of pending) tcp.write(b);
    pending.length = 0;
    console.log(`[gw] open ${tag} -> ${GAME_HOST}:${GAME_PORT}`);
  });
  tcp.on('data', (chunk) => { if (!socket.destroyed) socket.write(frame(0x2, chunk)); });
  tcp.on('close', () => closeBoth('server closed', 1001));
  tcp.on('error', (e) => closeBoth('server error: ' + e.code, 1011));

  // 클라 -> 서버 프레임 파서
  let buf = head && head.length ? Buffer.from(head) : Buffer.alloc(0);
  let fragments = [];
  let fragmentsLen = 0;

  const parse = () => {
    while (buf.length >= 2) {
      const fin = (buf[0] & 0x80) !== 0;
      const opcode = buf[0] & 0x0f;
      const masked = (buf[1] & 0x80) !== 0;
      let len = buf[1] & 0x7f;
      let off = 2;
      if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); off = 4; }
      else if (len === 127) {
        if (buf.length < 10) return;
        const big = buf.readBigUInt64BE(2);
        if (big > BigInt(MAX_MSG)) return closeBoth('message too big', 1009);
        len = Number(big); off = 10;
      }
      if (!masked) return closeBoth('unmasked client frame', 1002);
      if (len > MAX_MSG) return closeBoth('message too big', 1009);
      if (buf.length < off + 4 + len) return;
      const mask = buf.subarray(off, off + 4);
      const payload = Buffer.from(buf.subarray(off + 4, off + 4 + len));
      for (let i = 0; i < len; i++) payload[i] ^= mask[i & 3];
      buf = buf.subarray(off + 4 + len);

      switch (opcode) {
        case 0x0: // continuation
        case 0x2: // binary
          if (opcode === 0x2 && fragments.length) return closeBoth('protocol error', 1002);
          if (opcode === 0x0 && !fragments.length) return closeBoth('protocol error', 1002);
          if (fin && !fragments.length) { toServer(payload); break; }
          fragments.push(payload); fragmentsLen += len;
          if (fragmentsLen > MAX_MSG) return closeBoth('message too big', 1009);
          if (fin) { toServer(Buffer.concat(fragments)); fragments = []; fragmentsLen = 0; }
          break;
        case 0x1: return closeBoth('text frame not allowed', 1003);
        case 0x8: return closeBoth('client closed');
        case 0x9: socket.write(frame(0xA, payload)); break; // ping -> pong
        case 0xA: break;                                     // pong
        default: return closeBoth('unknown opcode', 1002);
      }
    }
  };

  socket.on('data', (d) => { if (closed) return; buf = Buffer.concat([buf, d]); parse(); });
  socket.on('close', () => closeBoth('client disconnected'));
  socket.on('end', () => closeBoth('client end'));   // http 서버 소켓은 half-open 허용 → FIN만 오면 close가 오지 않음
  socket.on('error', (e) => closeBoth('client error: ' + e.code));
  if (buf.length) parse();
});

server.listen(GW_PORT, GW_HOST, () => {
  console.log(`[gw] ws://${GW_HOST}:${GW_PORT} -> tcp://${GAME_HOST}:${GAME_PORT} ` +
              `(maxMsg ${MAX_MSG}, perIp ${MAX_PER_IP}, origins ${ALLOWED_ORIGINS.join('|') || '*'}, xff ${TRUST_XFF ? 'trusted' : 'ignored'})`);
});
