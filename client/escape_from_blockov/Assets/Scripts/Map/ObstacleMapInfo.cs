using UnityEngine;

namespace Blockov.Game
{
    /// <summary>씬의 Obstacles 루트에 붙는 가져오기 정보 (확인용)</summary>
    public sealed class ObstacleMapInfo : MonoBehaviour
    {
        public string SourceBmp;
        public string Hash;
        public int RectCount;
        public long WallCells;
        public long LowCells;
        public string ImportedAt;
    }
}
