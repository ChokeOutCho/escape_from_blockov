using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 2.5D 사선 시점 카메라 (정사영). 대상(로컬 플레이어)을 따라간다.
    /// 보이는 반경 = orthographicSize(화면 세로 절반). [ ] 키로 minZoom ~ CameraViewHalfExtent 사이 조절.
    /// game-spec 3.1: CameraViewHalfExtent = 200 (디버그 설정).
    /// </summary>
    [RequireComponent(typeof(Camera))]
    public sealed class CameraRig : MonoBehaviour
    {
        public Transform Target;

        [Tooltip("최대 보이는 반경 (game-spec 3.1, 디버그 200)")]
        public float CameraViewHalfExtent = 200f;
        public float MinZoom = 15f;
        public float Zoom = 25f;
        public float Pitch = 55f;
        public float Distance = 400f;
        public float FollowSharpness = 12f;
        [Tooltip("[ ] 키 줌 속도 (로그 배율/초, 0.9 ≈ 초당 x2.5)")]
        public float ZoomSpeed = 0.9f;

        Camera _cam;
        Vector3 _focus;

        void Awake()
        {
            _cam = GetComponent<Camera>();
            _cam.orthographic = true;
            _cam.nearClipPlane = 1f;
            _cam.farClipPlane = Distance + 600f;
        }

        public void SnapTo(Vector3 p)
        {
            _focus = p;
            Apply();
        }

        void LateUpdate()
        {
#if ENABLE_INPUT_SYSTEM
            // 줌: [ 줌인 / ] 줌아웃, 누르는 동안 연속 (game-spec 20.3). 마우스 휠은 전체 맵 줌 전용
            var kb = UnityEngine.InputSystem.Keyboard.current;
            if (kb != null)
            {
                float dir = (kb.rightBracketKey.isPressed ? 1f : 0f) - (kb.leftBracketKey.isPressed ? 1f : 0f);
                if (dir != 0f)
                    Zoom = Mathf.Clamp(Zoom * Mathf.Exp(dir * ZoomSpeed * Time.unscaledDeltaTime), MinZoom, CameraViewHalfExtent);
            }
#endif
            if (Target != null)
                _focus = Vector3.Lerp(_focus, Target.position, 1f - Mathf.Exp(-FollowSharpness * Time.deltaTime));
            Apply();
        }

        void Apply()
        {
            _cam.orthographicSize = Mathf.Clamp(Zoom, MinZoom, CameraViewHalfExtent);
            var rot = Quaternion.Euler(Pitch, 0, 0);
            transform.rotation = rot;
            transform.position = _focus - rot * Vector3.forward * Distance;
        }

        /// <summary>마우스 위치의 지면(y=0) 좌표</summary>
        public bool MouseGround(Vector2 screenPos, out Vector3 point)
        {
            var ray = _cam.ScreenPointToRay(screenPos);
            var plane = new Plane(Vector3.up, Vector3.zero);
            if (plane.Raycast(ray, out float d))
            {
                point = ray.GetPoint(d);
                return true;
            }
            point = Vector3.zero;
            return false;
        }
    }
}
