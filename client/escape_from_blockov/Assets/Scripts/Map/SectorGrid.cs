using UnityEngine;

/// <summary>
/// 섹터 기반 맵 그리드. 월드 원점(0,0,0)이 섹터 (0,0)의 좌하단 모서리.
/// 월드 X → 섹터 X, 월드 Z → 섹터 Y.
/// </summary>
public class SectorGrid : MonoBehaviour
{
    public const int DefaultSectorSize = 64;
    public const int DefaultSectorCount = 100;

    [Tooltip("섹터 한 변 길이 (유닛)")]
    public int sectorSize = DefaultSectorSize;
    [Tooltip("한 축당 섹터 개수")]
    public int sectorCountX = DefaultSectorCount;
    public int sectorCountY = DefaultSectorCount;

    [Header("Gizmo")]
    public bool drawGrid = true;
    public Color gridColor = new Color(1f, 1f, 1f, 0.25f);

    public float WorldSizeX => sectorSize * sectorCountX;
    public float WorldSizeY => sectorSize * sectorCountY;

    public bool IsInside(Vector3 worldPos) =>
        worldPos.x >= 0 && worldPos.z >= 0 && worldPos.x < WorldSizeX && worldPos.z < WorldSizeY;

    public Vector2Int WorldToSector(Vector3 worldPos) =>
        new Vector2Int(
            Mathf.Clamp(Mathf.FloorToInt(worldPos.x / sectorSize), 0, sectorCountX - 1),
            Mathf.Clamp(Mathf.FloorToInt(worldPos.z / sectorSize), 0, sectorCountY - 1));

    public Vector3 SectorCenter(int x, int y) =>
        new Vector3((x + 0.5f) * sectorSize, 0f, (y + 0.5f) * sectorSize);

    public int SectorIndex(Vector2Int s) => s.y * sectorCountX + s.x;

    void OnDrawGizmos()
    {
        if (!drawGrid) return;
        Gizmos.color = gridColor;
        for (int x = 0; x <= sectorCountX; x++)
            Gizmos.DrawLine(new Vector3(x * sectorSize, 0.05f, 0), new Vector3(x * sectorSize, 0.05f, WorldSizeY));
        for (int y = 0; y <= sectorCountY; y++)
            Gizmos.DrawLine(new Vector3(0, 0.05f, y * sectorSize), new Vector3(WorldSizeX, 0.05f, y * sectorSize));
    }
}
