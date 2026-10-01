using System.Collections.Generic;
using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 효과음 (game-spec 12.4). Resources/Audio/*.wav 를 읽어 AudioSource 풀(16개)로 재생한다.
    ///  - Play: 내 소리 (최대 음량)
    ///  - PlayAt: 월드 위치의 소리. 내 캐릭터와의 거리로 감쇠(10m까지 최대, 100m에서 0, 로그 감쇠), 카메라 좌우 기준 스테레오 팬
    ///  - 같은 소리가 한 프레임에 여러 번 오면(산탄 등) 한 번만 재생
    /// </summary>
    public static class SoundManager
    {
        public const string Pistol = "sfx_pistol", Shotgun = "sfx_shotgun", Sniper = "sfx_sniper";
        public const string Hit = "sfx_hit", Hurt = "sfx_hurt", Kill = "sfx_kill";
        public const string CoverHit = "sfx_cover_hit", CoverBreak = "sfx_cover_break", Spawn = "sfx_spawn";

        const int MaxVoices = 16;
        const float FullVolumeDistance = 10f, SilentDistance = 100f, PanDistance = 30f, MaxPan = 0.8f;
        public static float MasterVolume = 0.8f;

        static GameObject _root;
        static AudioSource[] _voices;
        static readonly Dictionary<string, AudioClip> _clips = new Dictionary<string, AudioClip>();
        static readonly Dictionary<string, int> _lastFrame = new Dictionary<string, int>();

        static void Ensure()
        {
            if (_root != null) return;
            _root = new GameObject("SoundManager");
            _voices = new AudioSource[MaxVoices];
            for (int i = 0; i < MaxVoices; i++)
            {
                var src = _root.AddComponent<AudioSource>();
                src.playOnAwake = false;
                src.spatialBlend = 0f;      // 2D: 감쇠·팬은 직접 계산
                _voices[i] = src;
            }
            if (Object.FindAnyObjectByType<AudioListener>() == null && Camera.main != null)
                Camera.main.gameObject.AddComponent<AudioListener>();
        }

        static AudioClip Clip(string name)
        {
            if (_clips.TryGetValue(name, out var c)) return c;
            c = Resources.Load<AudioClip>("Audio/" + name);
            if (c == null) Debug.LogWarning($"[SoundManager] Resources/Audio/{name} 없음");
            _clips[name] = c;
            return c;
        }

        /// <summary>무기 발사 소리: 산탄 여러 발 = 샷건, 큰 데미지(50 이상) = 저격총, 그 외 권총</summary>
        public static string FireClip(WeaponDef w)
        {
            if (w == null) return Pistol;
            if (w.Pellets > 1) return Shotgun;
            if (w.Damage >= 50) return Sniper;
            return Pistol;
        }

        /// <summary>거리(m) → 음량 0~1</summary>
        public static float Attenuation(float d)
        {
            if (d <= FullVolumeDistance) return 1f;
            if (d >= SilentDistance) return 0f;
            return 1f - Mathf.Log10(d / FullVolumeDistance);
        }

        public static void Play(string name, float volume = 1f, float pan = 0f)
        {
            if (volume <= 0.01f) return;
            var clip = Clip(name);
            if (clip == null) return;
            if (_lastFrame.TryGetValue(name, out int f) && f == Time.frameCount) return;
            _lastFrame[name] = Time.frameCount;
            Ensure();
            // 비어 있는 소스, 없으면 재생이 가장 많이 진행된 소스를 끊고 쓴다
            AudioSource pick = null;
            float mostDone = -1f;
            foreach (var v in _voices)
            {
                if (!v.isPlaying) { pick = v; break; }
                float done = v.clip != null ? v.time / Mathf.Max(0.01f, v.clip.length) : 1f;
                if (done > mostDone) { mostDone = done; pick = v; }
            }
            pick.Stop();
            pick.clip = clip;
            pick.volume = Mathf.Clamp01(volume) * MasterVolume;
            pick.panStereo = Mathf.Clamp(pan, -1f, 1f);
            pick.pitch = Random.Range(0.95f, 1.05f);
            pick.Play();
        }

        /// <summary>월드 위치(X-Z)의 소리: 내 캐릭터와의 거리로 감쇠, 카메라 좌우 기준 팬</summary>
        public static void PlayAt(string name, Vector2 worldXZ, float volume = 1f)
        {
            Vector2 listener = worldXZ;
            var gc = GameController.Instance;
            if (gc != null && gc.LocalView != null) listener = gc.LocalView.PosXZ;
            else if (Camera.main != null) listener = new Vector2(Camera.main.transform.position.x, Camera.main.transform.position.z);
            Vector2 d = worldXZ - listener;
            float v = Attenuation(d.magnitude) * volume;
            if (v <= 0.01f) return;
            float pan = 0f;
            var cam = Camera.main;
            if (cam != null)
            {
                var right = new Vector2(cam.transform.right.x, cam.transform.right.z);
                if (right.sqrMagnitude > 1e-4f) pan = Mathf.Clamp(Vector2.Dot(d, right.normalized) / PanDistance, -MaxPan, MaxPan);
            }
            Play(name, v, pan);
        }
    }
}
