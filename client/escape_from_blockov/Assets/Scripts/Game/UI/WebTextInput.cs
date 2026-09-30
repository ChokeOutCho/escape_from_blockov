using System;
#if UNITY_WEBGL && !UNITY_EDITOR
using System.Runtime.InteropServices;
using UnityEngine;
#endif

namespace Blockov.Game
{
    /// <summary>
    /// WebGL 한글 입력용 HTML 오버레이 (Assets/Plugins/WebGL/BlockovTextInput.jslib).
    /// WebGL이 아니면 Supported = false → IMGUI TextField를 그대로 쓴다.
    /// </summary>
    public static class WebTextInput
    {
#if UNITY_WEBGL && !UNITY_EDITOR
        [DllImport("__Internal")] static extern void BlockovInput_Open(string title, string value, int maxLen);
        [DllImport("__Internal")] static extern int BlockovInput_State();
        [DllImport("__Internal")] static extern string BlockovInput_Value();
        [DllImport("__Internal")] static extern void BlockovInput_Reset();

        public static bool Supported => true;
        static Action<string> _onDone;

        public static bool IsOpen => _onDone != null;

        public static void Open(string title, string value, int maxLen, Action<string> onDone)
        {
            _onDone = onDone;
            WebGLInput.captureAllKeyboardInput = false;
            BlockovInput_Open(title, value ?? "", maxLen);
        }

        /// <summary>매 프레임 호출. 확인/취소되면 콜백(취소 시 null)</summary>
        public static void Poll()
        {
            if (_onDone == null) return;
            int st = BlockovInput_State();
            if (st < 2) return;
            string v = st == 2 ? BlockovInput_Value() : null;
            BlockovInput_Reset();
            WebGLInput.captureAllKeyboardInput = true;
            var cb = _onDone;
            _onDone = null;
            cb(v);
        }
#else
        public static bool Supported => false;
        public static bool IsOpen => false;
        public static void Open(string title, string value, int maxLen, Action<string> onDone) => onDone(value);
        public static void Poll() { }
#endif
    }
}
