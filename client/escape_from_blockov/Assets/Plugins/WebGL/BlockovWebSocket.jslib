// Blockov WebSocket bridge for Unity WebGL.
// 이벤트 타입: 0 = 없음, 1 = Open, 2 = Message, 4 = Close  (C# WsEventType과 일치)
var BlockovWebSocketLib = {
  $BWS: {
    nextId: 1,
    sockets: {},
    encodeText: function (s) { return new TextEncoder().encode(s); },
    clearKeepAlive: function (s) {
      if (s.keepAlive) { clearInterval(s.keepAlive); s.keepAlive = 0; }
    }
  },

  BlockovWS_Create: function (urlPtr) {
    var url = UTF8ToString(urlPtr);
    var id = BWS.nextId++;
    var s = { ws: null, events: [], keepAlive: 0 };
    BWS.sockets[id] = s;
    try {
      var ws = new WebSocket(url);
      ws.binaryType = 'arraybuffer';
      s.ws = ws;
      ws.onopen = function () { s.events.push({ t: 1, d: null }); };
      ws.onmessage = function (e) {
        if (typeof e.data === 'string') { ws.close(1003, 'text frame not allowed'); return; }
        s.events.push({ t: 2, d: new Uint8Array(e.data) });
      };
      ws.onclose = function (e) {
        BWS.clearKeepAlive(s);
        s.events.push({ t: 4, d: BWS.encodeText('code=' + e.code + (e.reason ? ' ' + e.reason : '')) });
      };
      // onerror 뒤에는 항상 onclose가 오므로 별도 이벤트를 만들지 않는다.
    } catch (err) {
      s.events.push({ t: 4, d: BWS.encodeText('connect failed: ' + err) });
    }
    return id;
  },

  BlockovWS_Send: function (id, dataPtr, length) {
    var s = BWS.sockets[id];
    if (!s || !s.ws || s.ws.readyState !== 1) return 0;
    s.ws.send(HEAPU8.slice(dataPtr, dataPtr + length));
    return 1;
  },

  BlockovWS_SetKeepAlive: function (id, dataPtr, length, intervalMs) {
    var s = BWS.sockets[id];
    if (!s) return;
    BWS.clearKeepAlive(s);
    if (intervalMs <= 0 || length <= 0) return;
    var bytes = HEAPU8.slice(dataPtr, dataPtr + length);
    // 백그라운드 탭에서도 동작 (브라우저 타이머 스로틀링: 최대 1분 1회 수준이라 60초 주기면 충분)
    s.keepAlive = setInterval(function () {
      if (s.ws && s.ws.readyState === 1) s.ws.send(bytes);
    }, intervalMs);
  },

  BlockovWS_PeekEventType: function (id) {
    var s = BWS.sockets[id];
    return (s && s.events.length) ? s.events[0].t : 0;
  },

  BlockovWS_PeekEventLength: function (id) {
    var s = BWS.sockets[id];
    if (!s || !s.events.length || !s.events[0].d) return 0;
    return s.events[0].d.length;
  },

  BlockovWS_PopEvent: function (id, bufferPtr, length) {
    var s = BWS.sockets[id];
    if (!s || !s.events.length) return 0;
    var e = s.events.shift();
    if (e.d && length > 0) HEAPU8.set(e.d.subarray(0, Math.min(length, e.d.length)), bufferPtr);
    return e.t;
  },

  BlockovWS_Close: function (id) {
    var s = BWS.sockets[id];
    if (!s) return;
    BWS.clearKeepAlive(s);
    if (s.ws && (s.ws.readyState === 0 || s.ws.readyState === 1)) {
      try { s.ws.close(1000, 'client close'); } catch (e) {}
    }
  },

  BlockovWS_Free: function (id) {
    var s = BWS.sockets[id];
    if (s) BWS.clearKeepAlive(s);
    delete BWS.sockets[id];
  }
};

autoAddDeps(BlockovWebSocketLib, '$BWS');
mergeInto(LibraryManager.library, BlockovWebSocketLib);
