// WebGL 텍스트 입력 오버레이.
// Unity WebGL의 입력 필드는 한글 IME 조합을 제대로 처리하지 못하므로, 캔버스 위에 HTML <input>을 띄워 입력받는다.
// 상태: 0 = 닫힘, 1 = 입력 중, 2 = 확인, 3 = 취소
var BlockovTextInputLib = {
  $BTI: { state: 0, value: '', root: null },

  BlockovInput_Open: function (titlePtr, valuePtr, maxLen) {
    var title = UTF8ToString(titlePtr);
    var value = UTF8ToString(valuePtr);
    if (BTI.root) { BTI.root.remove(); BTI.root = null; }
    BTI.state = 1;
    BTI.value = value;

    var root = document.createElement('div');
    root.style.cssText = 'position:fixed;inset:0;display:flex;align-items:center;justify-content:center;' +
      'background:rgba(0,0,0,0.55);z-index:10000;font-family:sans-serif;';
    var box = document.createElement('div');
    box.style.cssText = 'background:#1f2430;padding:20px 24px;border-radius:10px;min-width:300px;color:#fff;box-shadow:0 8px 30px rgba(0,0,0,.5)';
    var label = document.createElement('div');
    label.textContent = title;
    label.style.cssText = 'margin-bottom:10px;font-size:16px;';
    var input = document.createElement('input');
    input.type = 'text';
    input.value = value;
    input.maxLength = maxLen;
    input.style.cssText = 'width:100%;box-sizing:border-box;font-size:20px;padding:8px;border-radius:6px;border:1px solid #556;background:#fff;color:#000;';
    var row = document.createElement('div');
    row.style.cssText = 'margin-top:12px;display:flex;gap:8px;justify-content:flex-end;';
    function mk(text, primary) {
      var b = document.createElement('button');
      b.textContent = text;
      b.style.cssText = 'font-size:16px;padding:6px 16px;border-radius:6px;border:0;cursor:pointer;' +
        (primary ? 'background:#4a8cff;color:#fff;' : 'background:#444a58;color:#fff;');
      return b;
    }
    var ok = mk('확인', true), cancel = mk('취소', false);
    function finish(st) {
      if (BTI.state !== 1) return;
      BTI.value = input.value;
      BTI.state = st;
      root.remove();
      BTI.root = null;
    }
    ok.onclick = function () { finish(2); };
    cancel.onclick = function () { finish(3); };
    input.addEventListener('keydown', function (e) {
      e.stopPropagation();
      if (e.key === 'Enter' && !e.isComposing) finish(2);
      else if (e.key === 'Escape') finish(3);
    });
    input.addEventListener('keyup', function (e) { e.stopPropagation(); });
    input.addEventListener('keypress', function (e) { e.stopPropagation(); });
    row.appendChild(cancel); row.appendChild(ok);
    box.appendChild(label); box.appendChild(input); box.appendChild(row);
    root.appendChild(box);
    document.body.appendChild(root);
    BTI.root = root;
    setTimeout(function () { input.focus(); input.select(); }, 0);
  },

  BlockovInput_State: function () {
    return BTI.state;
  },

  BlockovInput_Value: function () {
    var s = BTI.value || '';
    var size = lengthBytesUTF8(s) + 1;
    var buf = _malloc(size);
    stringToUTF8(s, buf, size);
    return buf;
  },

  BlockovInput_Reset: function () {
    BTI.state = 0;
  }
};

autoAddDeps(BlockovTextInputLib, '$BTI');
mergeInto(LibraryManager.library, BlockovTextInputLib);
