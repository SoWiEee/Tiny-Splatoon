#pragma once
#include <glm/glm.hpp>
#include <cstdint>

// 為了確保不同電腦/編譯器之間的記憶體對齊一致，我們強制 1 byte 對齊
#pragma pack(push, 1)

// 封包類型 ID
enum class PacketType : uint8_t {
    // --- 連線管理 ---
    C2S_JOIN_REQUEST,    // Client -> Server: 我想加入
    S2C_JOIN_ACCEPT,     // Server -> Client: 歡迎，你的 ID 是這個

    // --- 遊戲同步 ---
    C2S_PLAYER_STATE,    // Client -> Server: 我移動到了哪裡
    S2C_WORLD_STATE,     // Server -> Client: 所有人的位置在這裡
	S2C_GAME_STATE,      // Server -> Client: 遊戲狀態更新 (比分)

    // --- 遊戲事件 ---
    C2S_LOBBY_CHANGE_WEAPON, // Client 通知 Server 我換武器了
    C2S_SHOOT,           // Client -> Server: 我開槍了
    S2C_SHOOT_EVENT,     // Server -> Client: 某人開槍了 (大家生成子彈)
    C2S_THROW_BOMB,     // Client -> Server: 我丟炸彈了
    S2C_SPAWN_BOMB,     // Server -> All: 有人丟炸彈了，請在你們的世界生成
    S2C_SPLAT_UPDATE,    // Server -> Client: 地板這裡髒了 (大家畫圖)
    S2C_LOBBY_UPDATE,    // Server -> Client: 更新大廳 8 個格子的狀態
    S2C_GAME_START       // Server -> Client: 遊戲開始！
};

// 所有封包的共通標頭
struct PacketHeader {
    PacketType type;
};

// 武器類型定義
enum class WeaponType : uint8_t {
    SHOOTER = 0,
    BRUSH   = 1,
    SLOSHER = 2
};

// 換武器請求封包
struct PacketLobbyChangeWeapon {
    PacketHeader header;
    int playerID;
    WeaponType newWeapon;
};

// 大廳單個格子的資訊
struct LobbySlotInfo {
    int playerID;   // -1 代表沒人
    int teamID;     // 1=紅, 2=綠
    bool isReady;   // (可選)
    WeaponType weaponType;
};

// 大廳狀態封包
struct PacketLobbyState {
    PacketHeader header;
    LobbySlotInfo slots[8]; // 固定 8 個位置
};

// 開始遊戲封包 (內容空的沒關係，只是訊號)
struct PacketGameStart {
    PacketHeader header;
};

// 1. 加入請求
struct PacketJoinRequest {
    PacketHeader header;
    // 可以加 char name[32];
};

// 2. 加入許可
struct PacketJoinAccept {
    PacketHeader header;
    int yourPlayerID;   // Server 分配給你的 ID
    int yourTeamID;     // 1=Red, 2=Green
};

// 3. 玩家狀態 (位置同步)
struct PacketPlayerState {
    PacketHeader header;
    int playerID;       // 誰的狀態
    glm::vec3 position;
    glm::vec3 velocity; // 用於預測插值
    float rotationY;
    bool isSwimming;
};

// 4. 射擊請求
struct PacketShoot {
    PacketHeader header;
    int playerID;       // 誰射的
    glm::vec3 origin;
    glm::vec3 direction;
    int weaponType;     // 武器類型
    float speed;
    float scale;
    glm::vec3 color;
};

// 5. 塗地同步 (最精簡的資料)
struct PacketSplatUpdate {
    PacketHeader header;
    float u;
    float v;
    float radius;
    int teamID;
};

struct PacketSpawnBomb {
    PacketHeader header;
    int ownerID;        // 是誰丟的 (避免炸到自己，或判斷隊伍)
    int teamID;         // 隊伍顏色
    glm::vec3 startPos; // 起始位置
    glm::vec3 startVel; // 起始速度向量 (包含方向與力道)
    glm::vec3 color;    // 墨水顏色
};

// 分數與遊戲狀態封包
struct PacketGameState {
    PacketHeader header;
    float scoreTeam1;   // 紅隊分數 (0.0 ~ 1.0 或 像素總數)
    float scoreTeam2;   // 綠隊分數
    float timeRemaining; // 剩餘時間 (秒)
};

#pragma pack(pop)