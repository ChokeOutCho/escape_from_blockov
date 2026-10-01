using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// IMGUI 공통 스타일/폰트. 한글 표시를 위해 Resources/Fonts/NotoSansKR-Subset(한글 음절·자모·ASCII 부분집합)을 사용한다.
    /// (WebGL은 OS 폰트 대체가 없어 한글 폰트를 반드시 포함해야 한다)
    /// </summary>
    public static class UiKit
    {
        static Font _font;
        static Texture2D _white;
        static GUIStyle _label, _labelCenter, _labelRight, _title, _button, _field, _box;
        static int _styleScaleKey = -1;

        public static Font Font
        {
            get
            {
                if (_font == null) _font = Resources.Load<Font>("Fonts/NotoSansKR-Subset");
                return _font;
            }
        }

        public static Texture2D White
        {
            get
            {
                if (_white == null)
                {
                    _white = new Texture2D(1, 1, TextureFormat.RGBA32, false);
                    _white.SetPixel(0, 0, Color.white);
                    _white.Apply();
                }
                return _white;
            }
        }

        /// <summary>해상도 기준 UI 배율 (세로 1080 = 1.0)</summary>
        public static float Scale => Mathf.Clamp(Screen.height / 1080f, 0.6f, 2f);

        public static int Px(float v) => Mathf.RoundToInt(v * Scale);

        /// <summary>OnGUI 시작 시 호출</summary>
        public static void Begin()
        {
            if (Font != null) GUI.skin.font = Font;
            int key = Mathf.RoundToInt(Scale * 100);
            if (key == _styleScaleKey && _label != null) return;
            _styleScaleKey = key;

            // 한글 폰트 줄 높이(글자 크기 x 약 1.45)가 라벨 사각형보다 커도 잘리지 않게 (game-spec 12.2)
            _label = new GUIStyle(GUI.skin.label) { font = Font, fontSize = Px(20), richText = true, wordWrap = false, clipping = TextClipping.Overflow, padding = new RectOffset(2, 2, 0, 0) };
            _label.normal.textColor = Color.white;
            _labelCenter = new GUIStyle(_label) { alignment = TextAnchor.MiddleCenter };
            _labelRight = new GUIStyle(_label) { alignment = TextAnchor.MiddleRight };
            _title = new GUIStyle(_labelCenter) { fontSize = Px(56), fontStyle = FontStyle.Bold };
            _button = new GUIStyle(GUI.skin.button) { font = Font, fontSize = Px(24), clipping = TextClipping.Overflow };
            _field = new GUIStyle(GUI.skin.textField) { font = Font, fontSize = Px(24), alignment = TextAnchor.MiddleLeft, padding = new RectOffset(Px(10), Px(10), 0, 0) };
            _box = new GUIStyle(GUI.skin.box);
        }

        public static GUIStyle Label => _label;
        public static GUIStyle LabelCenter => _labelCenter;
        public static GUIStyle LabelRight => _labelRight;
        public static GUIStyle Title => _title;
        public static GUIStyle Button => _button;
        public static GUIStyle Field => _field;

        public static GUIStyle Sized(GUIStyle baseStyle, float size, TextAnchor? anchor = null)
        {
            var s = new GUIStyle(baseStyle) { fontSize = Px(size) };
            if (anchor.HasValue) s.alignment = anchor.Value;
            return s;
        }

        public static void Rect(Rect r, Color c)
        {
            var old = GUI.color;
            GUI.color = c;
            GUI.DrawTexture(r, White);
            GUI.color = old;
        }

        public static void Panel(Rect r, float alpha = 0.55f) => Rect(r, new Color(0, 0, 0, alpha));

        /// <summary>그림자 있는 라벨</summary>
        public static void ShadowLabel(Rect r, string text, GUIStyle style, Color? color = null)
        {
            var old = style.normal.textColor;
            style.normal.textColor = new Color(0, 0, 0, 0.85f);
            GUI.Label(new Rect(r.x + 1, r.y + 1, r.width, r.height), text, style);
            style.normal.textColor = color ?? old;
            GUI.Label(r, text, style);
            style.normal.textColor = old;
        }

        public static void Bar(Rect r, float t, Color fill, Color back)
        {
            Rect(r, back);
            Rect(new Rect(r.x, r.y, r.width * Mathf.Clamp01(t), r.height), fill);
        }
    }
}
