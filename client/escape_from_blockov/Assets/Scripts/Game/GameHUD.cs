using Blockov.Net;
using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 전투 HUD (IMGUI, game-spec 11.2, 19.8):
    ///  좌상단 RTT(LatencyHUD) + 킬 피드 / 우상단 TOP 3 / 상단 가운데 접속 인원·에어드랍 공지 / 좌하단 HP·점수·킬
    ///  하단 가운데 슬롯 바(1 특수 총 · 2 권총 · 3 붕대)·붕대 게이지 / 우하단 조작법(키보드·마우스 그림, 누르는 동안 반투명)
    ///  F 상호작용 문구·원형 게이지, 루팅 창 / 캐릭터 머리 위 이름·HP / 히트 마커 / 전체 맵(M) / 사망 결과창
    /// </summary>
    public sealed class GameHUD : MonoBehaviour
    {
        GameController _gc;

        // 마우스가 IMGUI 창 위에 있으면 사격하지 않는다 (LocalPlayerController가 조회)
        static Rect s_lootRect;
        static bool s_lootVisible;

        /// <summary>화면 좌표(아래가 0)가 클릭 가능한 HUD 창 위인가</summary>
        public static bool IsPointerOverUi(Vector2 screenPos)
        {
            if (!s_lootVisible) return false;
            return s_lootRect.Contains(new Vector2(screenPos.x, Screen.height - screenPos.y));
        }

        void Awake() => _gc = GetComponent<GameController>();

        void OnGUI()
        {
            if (_gc == null || _gc.LocalView == null) return;
            UiKit.Begin();
            var cam = _gc.Rig != null ? _gc.Rig.GetComponent<Camera>() : Camera.main;

            DrawNameplates(cam);
            DrawKillFeed();
            DrawTop3();
            DrawStatus();
            DrawHitMarker();
            DrawOnlineCount();
            DrawAirdropNotice();
            DrawMapWarning();
            if (!_gc.LocalDead)
            {
                DrawSlotBar();
                DrawControls();
                DrawInteraction(cam);
                DrawLoot();
            }
            else s_lootVisible = false;
            if (_gc.ShowMinimap) DrawMinimap(cam);
            if (_gc.ShowDeathResult) DrawDeathResult();
            else if (_gc.LocalDead) DrawCenterMessage("사망했습니다...");
        }

        void DrawNameplates(Camera cam)
        {
            if (cam == null) return;
            var style = UiKit.Sized(UiKit.LabelCenter, 15);
            float barW = UiKit.Px(60), barH = UiKit.Px(6);

            void Plate(CharacterView v, bool local)
            {
                if (v == null || v.IsDying) return;
                Vector3 sp = cam.WorldToScreenPoint(v.transform.position + Vector3.up * 2.4f);
                if (sp.z < 0 || sp.x < -100 || sp.x > Screen.width + 100 || sp.y < -100 || sp.y > Screen.height + 100) return;
                float x = sp.x, y = Screen.height - sp.y;
                UiKit.ShadowLabel(new Rect(x - 100, y - UiKit.Px(30), 200, UiKit.Px(22)), v.DisplayName, style,
                    local ? new Color(0.6f, 0.85f, 1f) : Color.white);
                float t = v.MaxHp > 0 ? (float)v.Hp / v.MaxHp : 0;
                UiKit.Bar(new Rect(x - barW / 2, y - UiKit.Px(6), barW, barH), t,
                    local ? new Color(0.3f, 0.75f, 1f) : new Color(0.95f, 0.3f, 0.25f), new Color(0, 0, 0, 0.6f));
            }

            foreach (var r in _gc.RemoteCharacters) Plate(r, false);
            Plate(_gc.LocalView, true);
        }

        void DrawKillFeed()
        {
            float y = UiKit.Px(40);
            var style = UiKit.Sized(UiKit.Label, 16);
            foreach (var f in _gc.Feed)
            {
                UiKit.ShadowLabel(new Rect(UiKit.Px(12), y, UiKit.Px(500), UiKit.Px(24)), f.Text, style, new Color(1f, 0.85f, 0.6f));
                y += UiKit.Px(24);
            }
        }

        void DrawTop3()
        {
            float w = UiKit.Px(260), row = UiKit.Px(30);
            var top = GameSession.Top3;
            float h = UiKit.Px(40) + row * Mathf.Max(1, top.Count) + UiKit.Px(8);
            var r = new Rect(Screen.width - w - UiKit.Px(12), UiKit.Px(12), w, h);
            UiKit.Panel(r);
            var head = UiKit.Sized(UiKit.Label, 18);
            GUI.Label(new Rect(r.x + UiKit.Px(12), r.y + UiKit.Px(6), w, UiKit.Px(28)), "<b>TOP 3</b>", head);
            var style = UiKit.Sized(UiKit.Label, 18);
            var right = UiKit.Sized(UiKit.LabelRight, 18);
            float y = r.y + UiKit.Px(40);
            if (top.Count == 0)
                GUI.Label(new Rect(r.x + UiKit.Px(12), y, w, row), "<color=#999999>-</color>", style);
            Color[] medal = { new Color(1f, 0.84f, 0.2f), new Color(0.8f, 0.85f, 0.9f), new Color(0.85f, 0.55f, 0.3f) };
            foreach (var e in top)
            {
                bool me = e.PlayerId == GameSession.MyPlayerId;
                var c = e.Rank >= 1 && e.Rank <= 3 ? medal[e.Rank - 1] : Color.white;
                string name = me ? $"<color=#8fd3ff>{e.Name}</color>" : e.Name;
                GUI.Label(new Rect(r.x + UiKit.Px(12), y, UiKit.Px(30), row), $"<color=#{ColorUtility.ToHtmlStringRGB(c)}>{e.Rank}</color>", style);
                GUI.Label(new Rect(r.x + UiKit.Px(44), y, w - UiKit.Px(130), row), name, style);
                GUI.Label(new Rect(r.x, y, w - UiKit.Px(12), row), e.Score.ToString(), right);
                y += row;
            }
        }

        void DrawStatus()
        {
            float w = UiKit.Px(340), h = UiKit.Px(110);
            var r = new Rect(UiKit.Px(12), Screen.height - h - UiKit.Px(12), w, h);
            UiKit.Panel(r);
            var style = UiKit.Sized(UiKit.Label, 18);
            GUI.Label(new Rect(r.x + UiKit.Px(12), r.y + UiKit.Px(8), w, UiKit.Px(26)),
                $"<b>{GameSession.MyName}</b>  <color=#999999>#{GameSession.MyPlayerId} · Room {GameSession.RoomNo}</color>", style);
            var v = _gc.LocalView;
            float t = v.MaxHp > 0 ? (float)GameSession.Hp / v.MaxHp : 0;
            var bar = new Rect(r.x + UiKit.Px(12), r.y + UiKit.Px(40), w - UiKit.Px(24), UiKit.Px(20));
            UiKit.Bar(bar, t, Color.Lerp(new Color(0.9f, 0.25f, 0.2f), new Color(0.3f, 0.85f, 0.4f), t), new Color(0.15f, 0.15f, 0.15f, 0.9f));
            GUI.Label(bar, $"HP {GameSession.Hp} / {v.MaxHp}", UiKit.Sized(UiKit.LabelCenter, 15));
            GUI.Label(new Rect(r.x + UiKit.Px(12), r.y + UiKit.Px(70), w, UiKit.Px(28)),
                $"점수 <b>{GameSession.Score}</b>    킬 <b>{GameSession.Kills}</b>", style);

            // 디버그: 좌표·섹터·시계
            var p = v.PosXZ;
            var dbg = UiKit.Sized(UiKit.LabelRight, 13);
            dbg.normal.textColor = new Color(1, 1, 1, 0.55f);
            var net = NetworkManager.Instance;
            string clock = net != null ? $"offset {net.Clock.OffsetMs:0}ms" : "";
            GUI.Label(new Rect(0, Screen.height - UiKit.Px(24), Screen.width - UiKit.Px(10), UiKit.Px(20)),
                $"({p.x:0.0}, {p.y:0.0})  {GameSession.RegionLabelAt(p)}  {clock}  zoom {(_gc.Rig ? _gc.Rig.Zoom : 0):0}", dbg);
        }

        void DrawHitMarker()
        {
            float age = Time.time - _gc.LastHitMarkerTime;
            if (age > 0.15f) return;
#if ENABLE_INPUT_SYSTEM
            var mouse = UnityEngine.InputSystem.Mouse.current;
            if (mouse == null) return;
            var mp = mouse.position.ReadValue();
            float x = mp.x, y = Screen.height - mp.y;
            float s = UiKit.Px(10), th = UiKit.Px(3);
            var c = new Color(1f, 1f, 1f, 1f - age / 0.15f);
            var m = GUI.matrix;
            GUIUtility.RotateAroundPivot(45, new Vector2(x, y));
            UiKit.Rect(new Rect(x - s, y - th / 2, s * 2, th), c);
            UiKit.Rect(new Rect(x - th / 2, y - s, th, s * 2), c);
            GUI.matrix = m;
#endif
        }

        /// <summary>화면 상단 가운데: 서버 전체 접속 인원</summary>
        void DrawOnlineCount()
        {
            if (GameSession.OnlineCount <= 0) return;
            float w = UiKit.Px(180), h = UiKit.Px(34);
            var r = new Rect((Screen.width - w) * 0.5f, UiKit.Px(12), w, h);
            UiKit.Panel(r);
            UiKit.ShadowLabel(r, $"접속 {GameSession.OnlineCount:N0}명", UiKit.Sized(UiKit.LabelCenter, 18), new Color(0.75f, 0.95f, 0.8f));
        }

        /// <summary>상단 가운데(접속 인원 아래): 에어드랍 공지 6초</summary>
        void DrawAirdropNotice()
        {
            var cm = _gc.Containers;
            if (cm == null || string.IsNullOrEmpty(cm.LastNotice.Text)) return;
            float age = Time.time - cm.LastNotice.Time;
            if (age > 6f) return;
            float a = Mathf.Clamp01((6f - age) / 0.6f);
            float w = UiKit.Px(460), h = UiKit.Px(46);
            var r = new Rect((Screen.width - w) * 0.5f, UiKit.Px(54), w, h);
            UiKit.Rect(r, new Color(0.55f, 0.25f, 0.02f, 0.78f * a));
            UiKit.Rect(new Rect(r.x, r.yMax - UiKit.Px(3), r.width, UiKit.Px(3)), new Color(1f, 0.85f, 0.2f, a));
            UiKit.ShadowLabel(r, $"<b>{cm.LastNotice.Text}</b>", UiKit.Sized(UiKit.LabelCenter, 22), new Color(1f, 0.95f, 0.7f, a));
        }

        void DrawMapWarning()
        {
            if (ObstacleMap.Hash == GameSession.MapHash) return;
            var style = UiKit.Sized(UiKit.LabelCenter, 16);
            UiKit.ShadowLabel(new Rect(0, UiKit.Px(106), Screen.width, UiKit.Px(24)),
                $"맵 데이터가 서버와 다릅니다 (서버 0x{GameSession.MapHash:X8} / 클라 0x{ObstacleMap.Hash:X8}) — Blockov/Map 도구로 BMP를 다시 가져오세요",
                style, new Color(1f, 0.45f, 0.4f));
        }

        ////////////////////////////////////////////////////////////////
        // 슬롯 바 · 붕대 게이지 (19.1, 19.4)
        ////////////////////////////////////////////////////////////////
        void DrawSlotBar()
        {
            float sw = UiKit.Px(150), sh = UiKit.Px(64), gap = UiKit.Px(8);
            float total = sw * 3 + gap * 2;
            float x0 = (Screen.width - total) * 0.5f, y = Screen.height - sh - UiKit.Px(16);
            var small = UiKit.Sized(UiKit.Label, 13);
            var name = UiKit.Sized(UiKit.LabelCenter, 18);
            var sub = UiKit.Sized(UiKit.LabelCenter, 13);

            byte sid = GameSession.SpecialWeaponId;
            GameSession.Weapons.TryGetValue(sid, out var sw1);
            string specialSub = sid == 0 ? "비어 있음" : (sw1 != null && sw1.Durability > 0 ? $"내구도 {GameSession.SpecialDurability} / {sw1.Durability}" : "");
            float bandage = _gc.Local != null ? _gc.Local.BandageProgress : -1f;

            for (int i = 0; i < 3; i++)
            {
                byte slot = (byte)(i + 1);
                var r = new Rect(x0 + i * (sw + gap), y, sw, sh);
                bool selected = slot == GameSession.Equipped || (slot == 3 && bandage >= 0);
                bool empty = (slot == 1 && sid == 0) || (slot == 3 && GameSession.Bandages == 0);
                UiKit.Panel(r, selected ? 0.72f : 0.5f);
                if (slot == 3 && bandage >= 0)
                    UiKit.Rect(new Rect(r.x, r.y, r.width * bandage, r.height), new Color(0.3f, 0.8f, 0.4f, 0.35f));
                if (selected) Border(r, UiKit.Px(2), new Color(1f, 0.85f, 0.25f));
                GUI.Label(new Rect(r.x + UiKit.Px(6), r.y + UiKit.Px(2), UiKit.Px(20), UiKit.Px(18)), $"<color=#bbbbbb>{slot}</color>", small);
                string title, detail;
                if (slot == 1) { title = sid == 0 ? "특수 총" : GameSession.WeaponName(sid); detail = specialSub; }
                else if (slot == 2) { title = GameSession.WeaponName(GameSession.PistolWeaponId); detail = "무한"; }
                else { title = $"붕대  x{GameSession.Bandages}"; detail = bandage >= 0 ? $"사용 중 {(1f - bandage) * GameSession.BandageSeconds:0.0}s" : $"+{GameSession.BandageHeal} HP · 2초"; }
                var col = empty ? new Color(0.6f, 0.6f, 0.6f) : Color.white;
                UiKit.ShadowLabel(new Rect(r.x, r.y + UiKit.Px(8), r.width, UiKit.Px(26)), title, name, col);
                UiKit.ShadowLabel(new Rect(r.x, r.y + UiKit.Px(36), r.width, UiKit.Px(20)), detail, sub, new Color(0.85f, 0.85f, 0.85f));
            }

            // 붕대 사용 게이지 (슬롯 바 위)
            if (bandage >= 0)
            {
                float bw = UiKit.Px(260), bh = UiKit.Px(14);
                var br = new Rect((Screen.width - bw) * 0.5f, y - UiKit.Px(30), bw, bh);
                UiKit.Bar(br, bandage, new Color(0.35f, 0.85f, 0.45f), new Color(0, 0, 0, 0.6f));
                UiKit.ShadowLabel(new Rect(br.x, br.y - UiKit.Px(20), bw, UiKit.Px(18)), "붕대 사용 중 · 이동 속도 절반 (사격·구르기·전환 시 취소)", UiKit.Sized(UiKit.LabelCenter, 13));
            }
        }

        static void Border(Rect r, float t, Color c)
        {
            UiKit.Rect(new Rect(r.x, r.y, r.width, t), c);
            UiKit.Rect(new Rect(r.x, r.yMax - t, r.width, t), c);
            UiKit.Rect(new Rect(r.x, r.y, t, r.height), c);
            UiKit.Rect(new Rect(r.xMax - t, r.y, t, r.height), c);
        }

        /// <summary>원형 게이지: 원 둘레의 점 N개, 12시부터 시계 방향으로 t만큼 채움</summary>
        static void Ring(Vector2 c, float radius, float t, Color fill, Color back, int n = 36)
        {
            float dot = Mathf.Max(3f, radius * 0.2f);
            for (int i = 0; i < n; i++)
            {
                float a = (i + 0.5f) / n * Mathf.PI * 2f;
                var p = new Vector2(c.x + Mathf.Sin(a) * radius, c.y - Mathf.Cos(a) * radius);
                UiKit.Rect(new Rect(p.x - dot / 2, p.y - dot / 2, dot, dot), (i + 0.5f) / n <= t ? fill : back);
            }
        }

        ////////////////////////////////////////////////////////////////
        // 조작법 (19.8): 키캡 + 마우스 그림, 누르는 동안 반투명
        ////////////////////////////////////////////////////////////////
        void DrawControls()
        {
            bool W = false, A = false, S = false, D = false, shift = false, space = false, k1 = false, k2 = false, k3 = false, f = false, m = false, lb = false, rb = false;
            bool lmb = false, moved = false, wheel = false;
#if ENABLE_INPUT_SYSTEM
            var kb = UnityEngine.InputSystem.Keyboard.current;
            if (kb != null)
            {
                W = kb.wKey.isPressed; A = kb.aKey.isPressed; S = kb.sKey.isPressed; D = kb.dKey.isPressed;
                shift = kb.shiftKey.isPressed; space = kb.spaceKey.isPressed;
                k1 = kb.digit1Key.isPressed; k2 = kb.digit2Key.isPressed; k3 = kb.digit3Key.isPressed;
                f = kb.fKey.isPressed; m = kb.mKey.isPressed; lb = kb.leftBracketKey.isPressed; rb = kb.rightBracketKey.isPressed;
            }
            var mouse = UnityEngine.InputSystem.Mouse.current;
            if (mouse != null)
            {
                lmb = mouse.leftButton.isPressed;
                moved = mouse.delta.ReadValue().sqrMagnitude > 0.5f;
                wheel = Mathf.Abs(mouse.scroll.ReadValue().y) > 0.01f;
            }
#endif
            float k = UiKit.Px(34), g = UiKit.Px(4);
            float pw = UiKit.Px(480), ph = UiKit.Px(150);
            var p = new Rect(Screen.width - pw - UiKit.Px(12), Screen.height - ph - UiKit.Px(32), pw, ph);
            UiKit.Panel(p, 0.35f);
            var cap = UiKit.Sized(UiKit.LabelCenter, 11);
            float x = p.x + UiKit.Px(10), y = p.y + UiKit.Px(8);

            // 이동 WASD
            Key(new Rect(x + k + g, y, k, k), "W", W);
            Key(new Rect(x, y + k + g, k, k), "A", A);
            Key(new Rect(x + k + g, y + k + g, k, k), "S", S);
            Key(new Rect(x + (k + g) * 2, y + k + g, k, k), "D", D);
            Caption(new Rect(x, y + k * 2 + g * 2, k * 3 + g * 2, UiKit.Px(14)), "이동", cap);

            // Shift / Space
            float row3 = y + k * 2 + g * 2 + UiKit.Px(18);
            Key(new Rect(x, row3, UiKit.Px(64), k), "Shift", shift);
            Caption(new Rect(x, row3 + k + 1, UiKit.Px(64), UiKit.Px(14)), "달리기", cap);
            var spaceR = new Rect(x + UiKit.Px(64) + g, row3, UiKit.Px(120), k);
            float cd = _gc.Local != null ? _gc.Local.RollCooldown01 : 0;
            Key(spaceR, cd > 0 ? $"{cd * GameSession.RollCooldown:0.0}s" : "Space", space);
            if (cd > 0)
            {
                UiKit.Rect(new Rect(spaceR.x, spaceR.y, spaceR.width * cd, spaceR.height), new Color(0, 0, 0, 0.45f));
                Ring(new Vector2(spaceR.xMax - UiKit.Px(14), spaceR.center.y), UiKit.Px(10), 1f - cd, new Color(1f, 0.85f, 0.3f), new Color(1, 1, 1, 0.2f), 20);
            }
            Caption(new Rect(spaceR.x, row3 + k + 1, spaceR.width, UiKit.Px(14)), "구르기 (쿨 3초)", cap);

            // 1 2 3 / F / M
            float x2 = x + (k + g) * 3 + UiKit.Px(14);
            Key(new Rect(x2, y, k, k), "1", k1);
            Key(new Rect(x2 + k + g, y, k, k), "2", k2);
            Key(new Rect(x2 + (k + g) * 2, y, k, k), "3", k3);
            Caption(new Rect(x2, y + k + 1, k * 3 + g * 2, UiKit.Px(14)), "특수총·권총·붕대", cap);
            float y2 = y + k + g + UiKit.Px(14);
            Key(new Rect(x2, y2, k, k), "F", f);
            Caption(new Rect(x2 - UiKit.Px(6), y2 + k + 1, k + UiKit.Px(12), UiKit.Px(14)), "열기", cap);
            Key(new Rect(x2 + (k + g), y2, k, k), "M", m);
            Caption(new Rect(x2 + (k + g) - UiKit.Px(6), y2 + k + 1, k + UiKit.Px(12), UiKit.Px(14)), "지도", cap);
            Key(new Rect(x2 + (k + g) * 2, y2, k, k), "[", lb);
            Key(new Rect(x2 + (k + g) * 3, y2, k, k), "]", rb);
            Caption(new Rect(x2 + (k + g) * 2, y2 + k + 1, k * 2 + g, UiKit.Px(14)), "줌 인/아웃", cap);

            // 마우스 그림
            float mx = p.xMax - UiKit.Px(150), my = p.y + UiKit.Px(10);
            float mw = UiKit.Px(56), mh = UiKit.Px(84);
            var body = new Rect(mx, my, mw, mh);
            var oldC = GUI.color;
            GUI.color = new Color(1, 1, 1, moved ? 0.35f : 1f);
            UiKit.Rect(body, new Color(0.22f, 0.22f, 0.25f, 0.95f));
            Border(body, UiKit.Px(2), new Color(0.85f, 0.85f, 0.85f));
            UiKit.Rect(new Rect(mx + mw / 2 - 1, my, 2, mh * 0.42f), new Color(0.85f, 0.85f, 0.85f));
            UiKit.Rect(new Rect(mx, my + mh * 0.42f, mw, 2), new Color(0.85f, 0.85f, 0.85f));
            GUI.color = new Color(1, 1, 1, lmb ? 0.35f : 1f);
            UiKit.Rect(new Rect(mx + 2, my + 2, mw / 2 - 3, mh * 0.42f - 2), new Color(0.95f, 0.45f, 0.3f));
            GUI.color = new Color(1, 1, 1, wheel ? 0.35f : 1f);
            UiKit.Rect(new Rect(mx + mw / 2 - UiKit.Px(4), my + UiKit.Px(8), UiKit.Px(8), UiKit.Px(16)), new Color(0.4f, 0.8f, 1f));
            GUI.color = oldC;
            var lab = UiKit.Sized(UiKit.Label, 12);
            float lx = mx + mw + UiKit.Px(8);
            GUI.Label(new Rect(lx, my, UiKit.Px(90), UiKit.Px(18)), "<color=#f08060>좌클릭</color> 사격", lab);
            GUI.Label(new Rect(lx, my + UiKit.Px(20), UiKit.Px(90), UiKit.Px(18)), "<color=#66ccff>휠</color> 지도 줌", lab);
            GUI.Label(new Rect(lx, my + UiKit.Px(40), UiKit.Px(90), UiKit.Px(18)), "움직여 조준", lab);
        }

        static void Key(Rect r, string label, bool pressed)
        {
            var old = GUI.color;
            GUI.color = new Color(1, 1, 1, pressed ? 0.35f : 1f);
            UiKit.Rect(r, new Color(0.18f, 0.18f, 0.2f, 0.95f));
            UiKit.Rect(new Rect(r.x, r.yMax - UiKit.Px(4), r.width, UiKit.Px(4)), new Color(0.08f, 0.08f, 0.1f, 0.95f));
            Border(r, 1, new Color(0.8f, 0.8f, 0.8f, 0.9f));
            GUI.Label(new Rect(r.x, r.y, r.width, r.height - UiKit.Px(3)), label, UiKit.Sized(UiKit.LabelCenter, label.Length > 1 ? 13 : 16));
            GUI.color = old;
        }

        static void Caption(Rect r, string text, GUIStyle style)
        {
            var old = style.normal.textColor;
            style.normal.textColor = new Color(1, 1, 1, 0.7f);
            GUI.Label(r, text, style);
            style.normal.textColor = old;
        }

        ////////////////////////////////////////////////////////////////
        // 상호작용 · 루팅 창 (19.5)
        ////////////////////////////////////////////////////////////////
        void DrawInteraction(Camera cam)
        {
            var cm = _gc.Containers;
            if (cm == null || cam == null || cm.Target == null || cm.OpenContainer != null) return;
            var t = cm.Target;
            Vector3 sp = cam.WorldToScreenPoint(new Vector3(t.Pos.x, t.Type == ContainerManager.TypeAirdrop ? 1.8f : 1.0f, t.Pos.y));
            if (sp.z < 0) return;
            var c = new Vector2(sp.x, Screen.height - sp.y);
            float rad = UiKit.Px(26);
            if (cm.HoldProgress >= 0)
            {
                UiKit.Rect(new Rect(c.x - rad * 0.7f, c.y - rad * 0.7f, rad * 1.4f, rad * 1.4f), new Color(0, 0, 0, 0.35f));
                Ring(c, rad, cm.HoldProgress, new Color(1f, 0.85f, 0.3f), new Color(1, 1, 1, 0.25f));
                UiKit.ShadowLabel(new Rect(c.x - rad, c.y - rad, rad * 2, rad * 2), $"{cm.HoldProgress * 100:0}%", UiKit.Sized(UiKit.LabelCenter, 13));
            }
            string what = t.Type == ContainerManager.TypeAirdrop ? "에어드랍" : "가방";
            string text = cm.WaitingOpen ? $"{what} 여는 중..." : $"<b>F</b>키를 눌러 열기  <color=#bbbbbb>({what})</color>";
            var r = new Rect(c.x - UiKit.Px(150), c.y + rad + UiKit.Px(6), UiKit.Px(300), UiKit.Px(30));
            UiKit.Panel(new Rect(r.x + UiKit.Px(40), r.y, r.width - UiKit.Px(80), r.height), 0.5f);
            UiKit.ShadowLabel(r, text, UiKit.Sized(UiKit.LabelCenter, 17));
        }

        void DrawLoot()
        {
            var cm = _gc.Containers;
            s_lootVisible = cm != null && cm.OpenContainer != null;
            if (!s_lootVisible) return;
            var info = cm.OpenContainer;
            var c = cm.OpenContents;
            float w = UiKit.Px(380), h = UiKit.Px(200);
            var r = new Rect(Screen.width * 0.5f + UiKit.Px(80), Screen.height * 0.5f - h * 0.5f, w, h);
            s_lootRect = r;
            UiKit.Panel(r, 0.82f);
            Border(r, UiKit.Px(2), info.Type == ContainerManager.TypeAirdrop ? new Color(1f, 0.55f, 0.1f) : new Color(0.7f, 0.5f, 0.3f));
            string title = info.Type == ContainerManager.TypeAirdrop ? $"에어드랍  <color=#aaaaaa>{GameSession.RegionLabel(info.SectorX, info.SectorY)}</color>" : "가방";
            GUI.Label(new Rect(r.x + UiKit.Px(14), r.y + UiKit.Px(8), w, UiKit.Px(30)), $"<b>{title}</b>", UiKit.Sized(UiKit.Label, 20));
            if (GUI.Button(new Rect(r.xMax - UiKit.Px(40), r.y + UiKit.Px(8), UiKit.Px(30), UiKit.Px(28)), "X", UiKit.Sized(UiKit.Button, 16)))
                cm.CloseLoot();

            var style = UiKit.Sized(UiKit.Label, 18);
            var btn = UiKit.Sized(UiKit.Button, 17);
            float y = r.y + UiKit.Px(50), rowH = UiKit.Px(52);

            // 특수 총
            var row1 = new Rect(r.x + UiKit.Px(12), y, w - UiKit.Px(24), rowH - UiKit.Px(6));
            UiKit.Rect(row1, new Color(1, 1, 1, 0.06f));
            string gun;
            if (c.SpecialWeaponId == 0) gun = "<color=#888888>특수 총 없음</color>";
            else
            {
                GameSession.Weapons.TryGetValue(c.SpecialWeaponId, out var wd);
                gun = $"{GameSession.WeaponName(c.SpecialWeaponId)}  <color=#bbbbbb>내구도 {c.Durability}{(wd != null && wd.Durability > 0 ? " / " + wd.Durability : "")}</color>";
            }
            GUI.Label(new Rect(row1.x + UiKit.Px(10), row1.y, row1.width, row1.height), gun, style);
            GUI.enabled = c.SpecialWeaponId != 0;
            if (GUI.Button(new Rect(row1.xMax - UiKit.Px(90), row1.y + UiKit.Px(6), UiKit.Px(82), row1.height - UiKit.Px(12)), "획득", btn))
                cm.Take(ContainerManager.ItemSpecial);
            GUI.enabled = true;
            y += rowH;

            // 붕대
            var row2 = new Rect(r.x + UiKit.Px(12), y, w - UiKit.Px(24), rowH - UiKit.Px(6));
            UiKit.Rect(row2, new Color(1, 1, 1, 0.06f));
            string band = c.Bandages == 0 ? "<color=#888888>붕대 없음</color>" : $"붕대  x{c.Bandages}  <color=#bbbbbb>(보유 {GameSession.Bandages}/{GameSession.MaxBandages})</color>";
            GUI.Label(new Rect(row2.x + UiKit.Px(10), row2.y, row2.width, row2.height), band, style);
            GUI.enabled = c.Bandages != 0 && GameSession.Bandages < GameSession.MaxBandages;
            if (GUI.Button(new Rect(row2.xMax - UiKit.Px(90), row2.y + UiKit.Px(6), UiKit.Px(82), row2.height - UiKit.Px(12)), "획득", btn))
                cm.Take(ContainerManager.ItemBandage);
            GUI.enabled = true;

            var hint = UiKit.Sized(UiKit.LabelCenter, 12);
            Caption(new Rect(r.x, r.yMax - UiKit.Px(24), w, UiKit.Px(18)), "특수 총은 덮어씁니다 · 붕대는 최대 5개 · F/Esc 닫기 · 3m 벗어나면 닫힘", hint);
        }

        ////////////////////////////////////////////////////////////////
        // 전체 맵 (M): 구역 번호 1~100(3x3 섹터), 나, 카메라 범위, 에어드랍. 적은 표시하지 않음 (19.8, 20.3, 21.2~21.3)
        //  휠 = 1~8배 줌(마우스 지점 고정), 좌클릭 드래그 = 이동. 열 때마다 1배·내 위치 기준
        ////////////////////////////////////////////////////////////////
        const float MapMaxZoom = 8f;
        Texture2D _minimap;
        GUIStyle _regionStyle;
        int _regionStyleSize = -1;
        float _mapZoom = 1f;
        Vector2 _mapCenter;
        Rect _mapRect;
        bool _mapWasOpen, _mapDragging;

        float MapViewSize => ObstacleMap.Size / _mapZoom;

        void ClampMapCenter()
        {
            float half = MapViewSize * 0.5f;
            _mapCenter.x = Mathf.Clamp(_mapCenter.x, half, ObstacleMap.Size - half);
            _mapCenter.y = Mathf.Clamp(_mapCenter.y, half, ObstacleMap.Size - half);
        }

        // Input System 화면 좌표(아래가 0) → 월드 (x, z)
        Vector2 MapScreenToWorld(Vector2 screen)
        {
            var gui = new Vector2(screen.x, Screen.height - screen.y);
            float v = MapViewSize;
            float minX = _mapCenter.x - v * 0.5f, minZ = _mapCenter.y - v * 0.5f;
            return new Vector2(minX + (gui.x - _mapRect.x) / _mapRect.width * v, minZ + (_mapRect.yMax - gui.y) / _mapRect.height * v);
        }

        void Update()
        {
            if (_gc == null || !_gc.ShowMinimap || _gc.LocalView == null) { _mapWasOpen = false; _mapDragging = false; return; }
            if (!_mapWasOpen)
            {
                _mapWasOpen = true;
                _mapZoom = 1f;
                _mapCenter = _gc.LocalView.PosXZ;
                ClampMapCenter();
            }
#if ENABLE_INPUT_SYSTEM
            var mouse = UnityEngine.InputSystem.Mouse.current;
            if (mouse == null || _mapRect.width <= 0) return;
            var mp = mouse.position.ReadValue();
            var gui = new Vector2(mp.x, Screen.height - mp.y);
            bool inside = _mapRect.Contains(gui);

            float scroll = mouse.scroll.ReadValue().y;
            if (inside && Mathf.Abs(scroll) > 0.01f)
            {
                Vector2 before = MapScreenToWorld(mp);
                _mapZoom = Mathf.Clamp(_mapZoom * (scroll > 0 ? 1.25f : 0.8f), 1f, MapMaxZoom);
                // 마우스가 가리키던 지점이 그대로 마우스 아래에 오도록
                float v = MapViewSize;
                float fx = (gui.x - _mapRect.x) / _mapRect.width, fz = (_mapRect.yMax - gui.y) / _mapRect.height;
                _mapCenter = new Vector2(before.x - (fx - 0.5f) * v, before.y - (fz - 0.5f) * v);
                ClampMapCenter();
            }

            if (mouse.leftButton.wasPressedThisFrame && inside) _mapDragging = true;
            if (!mouse.leftButton.isPressed) _mapDragging = false;
            if (_mapDragging)
            {
                var d = mouse.delta.ReadValue();
                float k = MapViewSize / _mapRect.width;
                _mapCenter -= new Vector2(d.x * k, d.y * k);
                ClampMapCenter();
            }
#endif
        }

        void DrawMinimap(Camera cam)
        {
            if (_minimap == null) _minimap = ObstacleMap.BuildMinimap(Mathf.RoundToInt(ObstacleMap.Size));   // 1px = 1m
            if (!_mapWasOpen) { _mapZoom = 1f; _mapCenter = _gc.LocalView.PosXZ; ClampMapCenter(); }
            float band = UiKit.Px(22);
            float size = Mathf.Min(Screen.width, Screen.height) * 0.8f;
            var r = new Rect((Screen.width - size) / 2, (Screen.height - size) / 2 + UiKit.Px(16), size, size);
            _mapRect = r;
            UiKit.Rect(new Rect(r.x - band - 6, r.y - band - UiKit.Px(36), r.width + band * 2 + 12, r.height + band * 2 + UiKit.Px(42)), new Color(0, 0, 0, 0.8f));
            GUI.Label(new Rect(r.x - band, r.y - band - UiKit.Px(34), r.width + band * 2, UiKit.Px(28)),
                $"<b>전체 맵</b>  <color=#aaaaaa>(M/Esc 닫기 · 휠 줌 x{_mapZoom:0.0} · 드래그 이동 · 구역 1칸 {SectorGrid.DefaultSectorSize * GameSession.RegionSectors}m · 주황 에어드랍)</color>", UiKit.Sized(UiKit.Label, 16));

            float v = MapViewSize;
            float minX = _mapCenter.x - v * 0.5f, minZ = _mapCenter.y - v * 0.5f;
            float S = ObstacleMap.Size;
            GUI.DrawTextureWithTexCoords(r, _minimap, new Rect(minX / S, minZ / S, v / S, v / S), false);

            Vector2 ToScreen(Vector2 w) => new Vector2(r.x + (w.x - minX) / v * r.width, r.yMax - (w.y - minZ) / v * r.height);
            bool Visible(Vector2 p) => p.x >= r.x - 2 && p.x <= r.xMax + 2 && p.y >= r.y - 2 && p.y <= r.yMax + 2;

            GUI.BeginClip(r);
            Vector2 L(Vector2 p) => new Vector2(p.x - r.x, p.y - r.y);

            // 구역(3x3 섹터 = 150m) 경계선 굵게 + 가운데 큰 번호 1~100 (21.2)
            float reg = SectorGrid.DefaultSectorSize * GameSession.RegionSectors;
            int regCount = GameSession.RegionCount;
            float regPx = reg / v * r.width;
            var border = new Color(0.95f, 0.9f, 0.7f, 0.45f);
            float bw = Mathf.Max(2f, UiKit.Px(2));
            for (int i = 0; i <= regCount; i++)
            {
                var px = L(ToScreen(new Vector2(i * reg, i * reg)));
                if (px.x >= -bw && px.x <= r.width + bw) UiKit.Rect(new Rect(px.x - bw / 2, 0, bw, r.height), border);
                if (px.y >= -bw && px.y <= r.height + bw) UiKit.Rect(new Rect(0, px.y - bw / 2, r.width, bw), border);
            }
            int rfs = Mathf.Clamp(Mathf.RoundToInt(regPx * 0.32f), 10, 72);
            if (_regionStyle == null || _regionStyleSize != rfs)
            {
                _regionStyle = new GUIStyle(UiKit.LabelCenter) { fontSize = rfs, fontStyle = FontStyle.Bold, richText = false, clipping = TextClipping.Overflow };
                _regionStyle.normal.textColor = new Color(1f, 1f, 1f, 0.3f);
                _regionStyleSize = rfs;
            }
            if (Event.current.type == EventType.Repaint)
                for (int gy = 0; gy < regCount; gy++)
                    for (int gx = 0; gx < regCount; gx++)
                    {
                        var c = L(ToScreen(new Vector2((gx + 0.5f) * reg, (gy + 0.5f) * reg)));
                        if (c.x < -regPx || c.x > r.width + regPx || c.y < -regPx || c.y > r.height + regPx) continue;
                        GUI.Label(new Rect(c.x - regPx / 2, c.y - regPx / 2, regPx, regPx),
                            GameSession.RegionNumber(gx * GameSession.RegionSectors, gy * GameSession.RegionSectors).ToString(), _regionStyle);
                    }

            // 카메라가 보는 범위
            if (cam != null && cam.orthographic)
            {
                float pitch = Mathf.Max(10f, cam.transform.eulerAngles.x);
                float halfH = cam.orthographicSize / Mathf.Sin(pitch * Mathf.Deg2Rad);
                float halfW = cam.orthographicSize * cam.aspect;
                var c = _gc.LocalView.PosXZ;
                var a = L(ToScreen(new Vector2(c.x - halfW, c.y + halfH)));
                var b = L(ToScreen(new Vector2(c.x + halfW, c.y - halfH)));
                var box = new Rect(a.x, a.y, Mathf.Max(2, b.x - a.x), Mathf.Max(2, b.y - a.y));
                var line = new Color(1, 1, 1, 0.6f);
                UiKit.Rect(new Rect(box.x, box.y, box.width, 1), line);
                UiKit.Rect(new Rect(box.x, box.yMax, box.width, 1), line);
                UiKit.Rect(new Rect(box.x, box.y, 1, box.height), line);
                UiKit.Rect(new Rect(box.xMax, box.y, 1, box.height), line);
            }

            // 에어드랍 (정확한 위치)
            if (_gc.Containers != null)
            {
                float blink = 0.6f + 0.4f * Mathf.Sin(Time.time * 6f);
                var lab = UiKit.Sized(UiKit.Label, 13);
                foreach (var ad in _gc.Containers.Airdrops)
                {
                    var sp = ToScreen(ad.Pos);
                    if (!Visible(sp)) continue;
                    var p = L(sp);
                    float s = UiKit.Px(12);
                    UiKit.Rect(new Rect(p.x - s / 2 - 2, p.y - s / 2 - 2, s + 4, s + 4), new Color(1f, 1f, 1f, blink));
                    UiKit.Rect(new Rect(p.x - s / 2, p.y - s / 2, s, s), new Color(1f, 0.5f, 0.08f));
                    UiKit.ShadowLabel(new Rect(p.x + s, p.y - UiKit.Px(10), UiKit.Px(160), UiKit.Px(20)), $"에어드랍 {GameSession.RegionLabel(ad.SectorX, ad.SectorY)}", lab, new Color(1f, 0.9f, 0.5f));
                }
            }

            var meS = ToScreen(_gc.LocalView.PosXZ);
            if (Visible(meS))
            {
                var me = L(meS);
                UiKit.Rect(new Rect(me.x - 5, me.y - 5, 10, 10), new Color(0.3f, 0.7f, 1f));     // 조준 방향 점은 표시하지 않음 (21.3)
            }
            GUI.EndClip();
        }

        void OnDestroy()
        {
            if (_minimap != null) Destroy(_minimap);
            s_lootVisible = false;
        }

        void DrawCenterMessage(string text)
        {
            UiKit.ShadowLabel(new Rect(0, Screen.height * 0.4f, Screen.width, UiKit.Px(50)), text, UiKit.Sized(UiKit.LabelCenter, 32));
        }

        void DrawDeathResult()
        {
            var d = GameSession.Death;
            UiKit.Rect(new Rect(0, 0, Screen.width, Screen.height), new Color(0, 0, 0, 0.55f));
            float w = UiKit.Px(460), h = UiKit.Px(330);
            var r = new Rect((Screen.width - w) / 2, (Screen.height - h) / 2, w, h);
            UiKit.Panel(r, 0.85f);
            float y = r.y + UiKit.Px(20);
            UiKit.ShadowLabel(new Rect(r.x, y, w, UiKit.Px(44)), "사망", UiKit.Sized(UiKit.LabelCenter, 36), new Color(1f, 0.4f, 0.35f));
            y += UiKit.Px(60);
            var style = UiKit.Sized(UiKit.Label, 20);
            var right = UiKit.Sized(UiKit.LabelRight, 20);
            void Row(string k, string v)
            {
                GUI.Label(new Rect(r.x + UiKit.Px(40), y, w, UiKit.Px(30)), k, style);
                GUI.Label(new Rect(r.x, y, w - UiKit.Px(40), UiKit.Px(30)), v, right);
                y += UiKit.Px(34);
            }
            Row("처치자", string.IsNullOrEmpty(d.KillerName) ? "-" : d.KillerName);
            Row("최종 점수", d.FinalScore.ToString());
            Row("킬", d.Kills.ToString());
            Row("생존 시간", $"{d.SurvivalSec / 60}분 {d.SurvivalSec % 60}초");
            y += UiKit.Px(12);
            if (GUI.Button(new Rect(r.x + UiKit.Px(80), y, w - UiKit.Px(160), UiKit.Px(50)), "타이틀로", UiKit.Button))
            {
                GameSession.Death = null;
                GameSession.TitleMessage = null;
                _gc.ReturnToTitle();
            }
        }
    }
}
