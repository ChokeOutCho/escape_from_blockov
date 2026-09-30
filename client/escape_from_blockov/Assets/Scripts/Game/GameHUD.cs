using Blockov.Net;
using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 전투 HUD (IMGUI, game-spec 11.2):
    ///  좌상단 RTT(LatencyHUD) + 킬 피드 / 우상단 TOP 3 / 좌하단 HP·점수·킬 / 캐릭터 머리 위 이름·HP / 히트 마커 / 사망 결과창
    /// </summary>
    public sealed class GameHUD : MonoBehaviour
    {
        GameController _gc;

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
            DrawMapWarning();
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
                $"({p.x:0.0}, {p.y:0.0})  sector ({Mathf.FloorToInt(p.x / SectorGrid.DefaultSectorSize)}, {Mathf.FloorToInt(p.y / SectorGrid.DefaultSectorSize)})  {clock}  zoom {(_gc.Rig ? _gc.Rig.Zoom : 0):0}", dbg);
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

        void DrawMapWarning()
        {
            if (ObstacleMap.Hash == GameSession.MapHash) return;
            var style = UiKit.Sized(UiKit.LabelCenter, 16);
            UiKit.ShadowLabel(new Rect(0, UiKit.Px(70), Screen.width, UiKit.Px(24)),
                $"맵 데이터가 서버와 다릅니다 (서버 0x{GameSession.MapHash:X8} / 클라 0x{ObstacleMap.Hash:X8}) — Blockov/Map 도구로 BMP를 다시 가져오세요",
                style, new Color(1f, 0.45f, 0.4f));
        }

        Texture2D _minimap;

        /// <summary>M: 전체 맵. 엄폐물(검정=벽, 황토=낮은 엄폐물), 나(파랑), 보이는 적(빨강), 카메라 범위</summary>
        void DrawMinimap(Camera cam)
        {
            if (_minimap == null) _minimap = ObstacleMap.BuildMinimap(800);
            float size = Mathf.Min(Screen.width, Screen.height) * 0.86f;
            var r = new Rect((Screen.width - size) / 2, (Screen.height - size) / 2 + UiKit.Px(10), size, size);
            UiKit.Rect(new Rect(r.x - 4, r.y - UiKit.Px(34), r.width + 8, r.height + UiKit.Px(38)), new Color(0, 0, 0, 0.75f));
            GUI.Label(new Rect(r.x, r.y - UiKit.Px(32), r.width, UiKit.Px(28)),
                "<b>전체 맵</b>  <color=#aaaaaa>(M 닫기 · 격자 640m · 검정 벽 / 황토 낮은 엄폐물)</color>", UiKit.Sized(UiKit.Label, 16));
            GUI.DrawTexture(r, _minimap, ScaleMode.StretchToFill, false);

            Vector2 ToScreen(Vector2 w) => new Vector2(r.x + w.x / ObstacleMap.Size * r.width, r.yMax - w.y / ObstacleMap.Size * r.height);

            // 카메라가 보는 범위
            if (cam != null && cam.orthographic)
            {
                float pitch = Mathf.Max(10f, cam.transform.eulerAngles.x);
                float halfH = cam.orthographicSize / Mathf.Sin(pitch * Mathf.Deg2Rad);
                float halfW = cam.orthographicSize * cam.aspect;
                var c = _gc.LocalView.PosXZ;
                var a = ToScreen(new Vector2(c.x - halfW, c.y + halfH));
                var b = ToScreen(new Vector2(c.x + halfW, c.y - halfH));
                var box = new Rect(a.x, a.y, Mathf.Max(2, b.x - a.x), Mathf.Max(2, b.y - a.y));
                var line = new Color(1, 1, 1, 0.6f);
                UiKit.Rect(new Rect(box.x, box.y, box.width, 1), line);
                UiKit.Rect(new Rect(box.x, box.yMax, box.width, 1), line);
                UiKit.Rect(new Rect(box.x, box.y, 1, box.height), line);
                UiKit.Rect(new Rect(box.xMax, box.y, 1, box.height), line);
            }
            foreach (var rc in _gc.RemoteCharacters)
            {
                if (rc == null || rc.IsDying) continue;
                var p = ToScreen(rc.PosXZ);
                UiKit.Rect(new Rect(p.x - 3, p.y - 3, 6, 6), new Color(1f, 0.3f, 0.25f));
            }
            var me = ToScreen(_gc.LocalView.PosXZ);
            UiKit.Rect(new Rect(me.x - 5, me.y - 5, 10, 10), new Color(0.3f, 0.7f, 1f));
            float ang = _gc.LocalView.AimAngle * Mathf.Deg2Rad;
            for (int i = 1; i <= 4; i++)
                UiKit.Rect(new Rect(me.x + Mathf.Cos(ang) * i * 4 - 1, me.y - Mathf.Sin(ang) * i * 4 - 1, 3, 3), new Color(0.3f, 0.7f, 1f));
        }

        void OnDestroy()
        {
            if (_minimap != null) Destroy(_minimap);
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
