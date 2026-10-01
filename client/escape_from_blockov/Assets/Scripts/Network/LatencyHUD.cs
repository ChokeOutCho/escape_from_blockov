using UnityEngine;

namespace Blockov.Net
{
    /// <summary>
    /// 화면 좌상단에 서버 연결 상태와 RTT(클라↔서버 왕복 지연)를 표시한다.
    /// RTT는 CS_PING → SC_PONG 왕복 시간(ms). 편도 지연은 대략 RTT/2.
    /// </summary>
    public sealed class LatencyHUD : MonoBehaviour
    {
        [SerializeField] bool visible = true;
#if !ENABLE_INPUT_SYSTEM && ENABLE_LEGACY_INPUT_MANAGER
        [SerializeField] KeyCode toggleKey = KeyCode.F3;
#endif
        [SerializeField] int fontSize = 16;
        [SerializeField] int goodMs = 80;
        [SerializeField] int warnMs = 150;

        GUIStyle _style;
        GUIStyle _shadow;

        void Update()
        {
#if ENABLE_INPUT_SYSTEM
            var kb = UnityEngine.InputSystem.Keyboard.current;
            if (kb != null && kb.f3Key.wasPressedThisFrame) visible = !visible;
#elif ENABLE_LEGACY_INPUT_MANAGER
            if (Input.GetKeyDown(toggleKey)) visible = !visible;
#endif
        }

        void OnGUI()
        {
            if (!visible) return;
            var net = NetworkManager.Instance;
            if (net == null) return;

            int size = Mathf.RoundToInt(fontSize * Mathf.Clamp(Screen.height / 1080f, 0.6f, 2f));
            if (_style == null || _style.fontSize != size)
            {
                _style = new GUIStyle(GUI.skin.label) { font = Blockov.Game.UiKit.Font, fontSize = size, fontStyle = FontStyle.Bold, richText = true, clipping = TextClipping.Overflow };
                _shadow = new GUIStyle(_style) { richText = false };
                _shadow.normal.textColor = new Color(0, 0, 0, 0.8f);
            }

            string text;
            if (net.CurrentState == NetworkManager.State.InGame && net.LastRttMs >= 0)
            {
                string color = net.AvgRttMs <= goodMs ? "#6EE06E" : net.AvgRttMs <= warnMs ? "#F2C94C" : "#FF6B6B";
                text = $"<color={color}>Ping {net.LastRttMs} ms</color>  (avg {net.AvgRttMs:0} / min {net.MinRttMs})";
            }
            else if (net.CurrentState == NetworkManager.State.Disconnected)
            {
                return; // 타이틀/게임 화면이 자체적으로 사유를 표시한다
            }
            else
            {
                text = $"<color=#F2C94C>{net.CurrentState}...</color>";
            }

            var rect = new Rect(10, 8, 700, size + 12);
            GUI.Label(new Rect(rect.x + 1, rect.y + 1, rect.width, rect.height), StripTags(text), _shadow);
            GUI.Label(rect, text, _style);
        }

        static string StripTags(string s) => System.Text.RegularExpressions.Regex.Replace(s, "<.*?>", "");
    }
}
