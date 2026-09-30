using Blockov.Net;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace Blockov.Game
{
    /// <summary>
    /// Title 씬 (빌드 0): 이름 입력 → [시작] → 접속·입장 → TestArena 로드.
    /// 입장 실패/연결 실패/게임 중 끊김 사유를 표시한다.
    /// </summary>
    public sealed class TitleController : MonoBehaviour
    {
        public const string ArenaScene = "TestArena";
        const string PrefName = "blockov.name";

        string _name = "";
        bool _loading;
        float _connectStart;

        void Start()
        {
            try { _name = PlayerPrefs.GetString(PrefName, ""); } catch { _name = ""; }
            if (string.IsNullOrEmpty(_name)) _name = GameSession.RequestedName ?? "";

            var net = NetworkManager.Instance;
            if (net != null)
            {
                // 게임에서 돌아온 경우 연결이 남아 있으면 정리 (구독 전에 해서 오류 메시지가 뜨지 않게)
                if (net.CurrentState != NetworkManager.State.Disconnected) net.Disconnect("return to title");
                net.HoldDispatch = false;
                net.EnterGameReceived += OnEnterGame;
                net.StateChanged += OnStateChanged;
            }
            Cursor.visible = true;
        }

        void OnDestroy()
        {
            var net = NetworkManager.Instance;
            if (net == null) return;
            net.EnterGameReceived -= OnEnterGame;
            net.StateChanged -= OnStateChanged;
        }

        void Update()
        {
            WebTextInput.Poll();
#if ENABLE_INPUT_SYSTEM
            var kb = UnityEngine.InputSystem.Keyboard.current;
            if (kb != null && kb.enterKey.wasPressedThisFrame && !WebTextInput.IsOpen) StartGame();
#endif
        }

        void StartGame()
        {
            var net = NetworkManager.Instance;
            if (net == null || _loading || net.CurrentState != NetworkManager.State.Disconnected) return;
            _name = (_name ?? "").Trim();
            if (_name.Length > NetConst.NameLength) _name = _name.Substring(0, NetConst.NameLength);
            try { PlayerPrefs.SetString(PrefName, _name); PlayerPrefs.Save(); } catch { }
            GameSession.RequestedName = _name;
            GameSession.TitleMessage = null;
            _connectStart = Time.unscaledTime;
            net.Connect(_name);
        }

        void OnEnterGame(PacketReader r, EnterResult result)
        {
            if (result != EnterResult.Ok)
            {
                GameSession.TitleMessage = result switch
                {
                    EnterResult.ServerFull => "서버가 가득 찼습니다. 잠시 후 다시 시도해 주세요.",
                    EnterResult.VersionMismatch => "클라이언트 버전이 서버와 다릅니다. 페이지를 새로고침해 주세요.",
                    EnterResult.InvalidName => "사용할 수 없는 이름입니다.",
                    _ => "입장 실패: " + result,
                };
                return;
            }
            GameSession.ReadEnter(r);
            GameSession.EnterRealtime = Time.realtimeSinceStartup;
            _loading = true;
            SceneManager.LoadSceneAsync(ArenaScene);
        }

        void OnStateChanged(NetworkManager.State s)
        {
            if (s == NetworkManager.State.Disconnected && !_loading && GameSession.TitleMessage == null)
            {
                var net = NetworkManager.Instance;
                GameSession.TitleMessage = "서버에 연결할 수 없습니다." + (net != null && !string.IsNullOrEmpty(net.LastError) ? $"\n({net.LastError})" : "");
            }
        }

        void OnGUI()
        {
            UiKit.Begin();
            float w = Screen.width, h = Screen.height;
            UiKit.Rect(new Rect(0, 0, w, h), new Color(0.07f, 0.08f, 0.11f, 1f));

            float cx = w * 0.5f;
            float y = h * 0.22f;
            var titleStyle = new GUIStyle(UiKit.Title) { fontSize = Mathf.Min(UiKit.Title.fontSize, Mathf.RoundToInt(w / 14f)) };
            UiKit.ShadowLabel(new Rect(0, y, w, UiKit.Px(80)), "ESCAPE FROM BLOCKOV", titleStyle, new Color(0.95f, 0.8f, 0.3f));
            y += UiKit.Px(90);
            GUI.Label(new Rect(0, y, w, UiKit.Px(30)), "2.5D PvP 슈팅 · 처치하고 점수를 빼앗아 상위 3위에 오르세요", UiKit.LabelCenter);
            y += UiKit.Px(80);

            var net = NetworkManager.Instance;
            bool busy = _loading || (net != null && net.CurrentState != NetworkManager.State.Disconnected);

            float fw = Mathf.Min(UiKit.Px(420), w - 40), fh = UiKit.Px(48);
            GUI.Label(new Rect(cx - fw / 2, y, fw, UiKit.Px(30)), "이름 (1~12자, 비우면 Guest####)", UiKit.Label);
            y += UiKit.Px(34);
            var fieldRect = new Rect(cx - fw / 2, y, fw, fh);
            GUI.enabled = !busy;
            if (WebTextInput.Supported)
            {
                // WebGL: 클릭 시 HTML 입력창(한글 IME 지원)
                if (GUI.Button(fieldRect, string.IsNullOrEmpty(_name) ? "<color=#888888>클릭해서 이름 입력</color>" : _name,
                        new GUIStyle(UiKit.Field) { richText = true }) && !WebTextInput.IsOpen)
                {
                    WebTextInput.Open("이름 (1~12자)", _name, NetConst.NameLength, v => { if (v != null) _name = v; });
                }
            }
            else
            {
                GUI.SetNextControlName("name");
                _name = GUI.TextField(fieldRect, _name ?? "", NetConst.NameLength, UiKit.Field);
            }
            y += fh + UiKit.Px(16);

            if (GUI.Button(new Rect(cx - fw / 2, y, fw, UiKit.Px(56)), busy ? "접속 중..." : "시작", UiKit.Button))
                StartGame();
            GUI.enabled = true;
            y += UiKit.Px(76);

            string status = null;
            if (net == null) status = "<color=#ff6b6b>NetworkManager가 없습니다 (Title 씬 구성 확인)</color>";
            else if (_loading) status = "전투 지역으로 이동 중...";
            else if (net.CurrentState == NetworkManager.State.Connecting) status = $"서버 연결 중... ({Time.unscaledTime - _connectStart:0.0}s)";
            else if (net.CurrentState == NetworkManager.State.Entering) status = "입장 중...";
            else if (!string.IsNullOrEmpty(GameSession.TitleMessage)) status = "<color=#ff8a80>" + GameSession.TitleMessage + "</color>";
            if (status != null)
                GUI.Label(new Rect(0, y, w, UiKit.Px(60)), status, new GUIStyle(UiKit.LabelCenter) { wordWrap = true });

            var small = UiKit.Sized(UiKit.Label, 14);
            small.normal.textColor = new Color(1, 1, 1, 0.4f);
            GUI.Label(new Rect(UiKit.Px(10), h - UiKit.Px(28), w, UiKit.Px(24)),
                $"server: {(net != null ? net.ServerUrl : "-")}   protocol v{NetConst.ProtocolVersion}", small);
        }
    }
}
