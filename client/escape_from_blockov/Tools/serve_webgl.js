// WebGL 빌드 웹서버 + 게임 WebSocket 프록시 (의존성 없음)
// 사용: node Tools/serve_webgl.js [port=8090] [dir=Builds/WebGL]
//   - 모든 인터페이스(0.0.0.0)에서 대기 → 같은 LAN / 포트포워딩된 외부에서 접속 가능
//   - 같은 포트의 /ws 경로 WebSocket을 게이트웨이(GW_HOST:GW_PORT, 기본 127.0.0.1:8080)로 중계
//     → 외부에는 이 포트 하나만 열면 된다. 클라(WebGL)는 ?server= 가 없으면 ws(s)://<페이지 주소>/ws 로 접속
//   - 게이트웨이에는 X-Forwarded-For(실제 접속 IP)를 넘긴다 (게이트웨이 TRUST_XFF=1 필요: IP당 접속 수 제한용)
// 환경변수: SERVE_HOST(0.0.0.0) GW_HOST(127.0.0.1) GW_PORT(8080)
'use strict';
const http = require('http'), net = require('net'), fs = require('fs'), path = require('path'), os = require('os');
const PORT = +(process.argv[2] || 8090);
const ROOT = path.resolve(process.argv[3] || path.join(__dirname, '..', 'Builds', 'WebGL'));
const HOST = process.env.SERVE_HOST || '0.0.0.0';
const GW_HOST = process.env.GW_HOST || '127.0.0.1';
const GW_PORT = +(process.env.GW_PORT || 8080);
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'application/javascript', '.wasm': 'application/wasm',
  '.data': 'application/octet-stream', '.unityweb': 'application/octet-stream', '.png': 'image/png', '.ico': 'image/x-icon', '.css': 'text/css' };

const stamp = () => new Date().toTimeString().slice(0, 8);
const peer = (sock) => (sock.remoteAddress || '?').replace(/^::ffff:/, '');

const server = http.createServer((req, res) => {
  if (req.method !== 'GET' && req.method !== 'HEAD') { res.writeHead(405); return res.end(); }
  let p;
  try { p = decodeURIComponent(req.url.split('?')[0]); } catch { res.writeHead(400); return res.end(); }
  if (p.endsWith('/')) p += 'index.html';
  const file = path.resolve(ROOT, '.' + p);
  if (file !== ROOT && !file.startsWith(ROOT + path.sep)) { res.writeHead(403); return res.end(); }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); return res.end('not found'); }
    const ext = path.extname(file);
    const headers = { 'Content-Type': TYPES[ext] || 'application/octet-stream', 'Cache-Control': 'no-cache' };
    // 압축 파일(.br/.gz)을 그대로 두는 빌드 설정일 때를 위한 헤더 (br은 브라우저가 https에서만 받음)
    if (ext === '.br') { headers['Content-Encoding'] = 'br'; headers['Content-Type'] = TYPES[path.extname(file.slice(0, -3))] || 'application/octet-stream'; }
    if (ext === '.gz') { headers['Content-Encoding'] = 'gzip'; headers['Content-Type'] = TYPES[path.extname(file.slice(0, -3))] || 'application/octet-stream'; }
    if (p.endsWith('/index.html')) console.log(`[${stamp()}] page ${peer(req.socket)}`);
    res.writeHead(200, headers); res.end(req.method === 'HEAD' ? undefined : data);
  });
});

// /ws WebSocket → 게이트웨이로 바이트 그대로 중계 (핸드셰이크 포함)
server.on('upgrade', (req, socket, head) => {
  const url = req.url.split('?')[0];
  if (url !== '/ws' && url !== '/ws/') { socket.end('HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n'); return; }
  const ip = peer(socket);
  const up = net.connect({ host: GW_HOST, port: GW_PORT });
  let lines = `GET / HTTP/1.1\r\n`;
  for (let i = 0; i < req.rawHeaders.length; i += 2) {
    const k = req.rawHeaders[i];
    if (/^x-forwarded-for$/i.test(k)) continue;           // 클라가 보낸 값은 버림 (위조 방지)
    lines += `${k}: ${req.rawHeaders[i + 1]}\r\n`;
  }
  lines += `X-Forwarded-For: ${ip}\r\n\r\n`;
  up.on('connect', () => {
    up.setNoDelay(true); socket.setNoDelay(true);
    up.write(lines);
    if (head && head.length) up.write(head);
    socket.pipe(up); up.pipe(socket);
    console.log(`[${stamp()}] ws open  ${ip}`);
  });
  const close = () => { socket.destroy(); up.destroy(); };
  up.on('error', (e) => { console.log(`[${stamp()}] ws gateway error ${e.code} (게이트웨이 ${GW_HOST}:${GW_PORT} 실행 중인지 확인)`); close(); });
  socket.on('error', close);
  // http 서버 소켓은 half-open 허용이라 FIN만 받고 끝나지 않는다 → 한쪽이 끝나면 둘 다 닫음
  socket.on('end', close);
  up.on('end', close);
  up.on('close', () => { socket.destroy(); });
  socket.on('close', () => { up.destroy(); console.log(`[${stamp()}] ws close ${ip}`); });
});

server.listen(PORT, HOST, () => {
  console.log(`WebGL root ${ROOT}`);
  console.log(`  이 PC     : http://localhost:${PORT}/`);
  for (const [name, list] of Object.entries(os.networkInterfaces()))
    for (const a of list || [])
      if (a.family === 'IPv4' && !a.internal) console.log(`  LAN       : http://${a.address}:${PORT}/   (${name})`);
  console.log(`  외부      : http://<공인 IP>:${PORT}/   (공유기에서 TCP ${PORT} → 이 PC LAN IP:${PORT} 포트포워딩 필요)`);
  console.log(`  게임 WS   : /ws → ${GW_HOST}:${GW_PORT}`);
});
server.on('error', (e) => { console.log(`listen 실패: ${e.message}`); process.exit(1); });
