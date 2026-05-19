#pragma once
#include <glm/glm.hpp>
#include <cstdint>

// ���F�T�O���P�q��/�sĶ���������O�������@�P�A�ڭ̱j�� 1 byte ���
#pragma pack(push, 1)

inline constexpr int kLobbySlotCount = 6;

// �ʥ]���� ID
enum class PacketType : uint8_t {
    // --- �s�u�޲z ---
    C2S_JOIN_REQUEST,    // Client -> Server: �ڷQ�[�J
    S2C_JOIN_ACCEPT,     // Server -> Client: �w��A�A�� ID �O�o��

    // --- �C���P�B ---
    C2S_PLAYER_STATE,    // Client -> Server: �ڲ��ʨ�F����
    S2C_WORLD_STATE,     // Server -> Client: �Ҧ��H����m�b�o��
	S2C_GAME_STATE,      // Server -> Client: �C�����A��s (���)

    // --- �C���ƥ� ---
    C2S_LOBBY_CHANGE_WEAPON, // Client �q�� Server �ڴ��Z���F
    C2S_SHOOT,           // Client -> Server: �ڶ}�j�F
    S2C_SHOOT_EVENT,     // Server -> Client: �Y�H�}�j�F (�j�a�ͦ��l�u)
    C2S_THROW_BOMB,      // Client -> Server: �ڥᬵ�u�F
    S2C_SPAWN_BOMB,      // Server -> All: ���H�ᬵ�u�F�A�Цb�A�̪��@�ɥͦ�
    S2C_SPLAT_UPDATE,    // Server -> Client: �a�O�o��ż�F (�j�a�e��)
    S2C_LOBBY_UPDATE,    // Server -> Client: ��s�j�U 8 �Ӯ�l�����A
    S2C_GAME_START,      // Server -> Client: �C���}�l�I
    S2C_KILL_EVENT,      // �����q��
    C2S_SPECIAL_ATTACK,  // Client -> Server: �ڭn�}�j
    S2C_SPECIAL_ATTACK   // Server -> Clients: ���H�}�j
};

// �Ҧ��ʥ]���@�q���Y
struct PacketHeader {
    PacketType type;
};

// �Z�������w�q
enum class WeaponType : uint8_t {
    SHOOTER = 0,
    BRUSH   = 1,
    SLOSHER = 2
};

// ���Z���ШD�ʥ]
struct PacketLobbyChangeWeapon {
    PacketHeader header;
    int playerID;
    WeaponType newWeapon;
};

// �j�U��Ӯ�l����T
struct LobbySlotInfo {
    int playerID;   // -1 �N���S�H
    int teamID;     // 1=��, 2=��
    bool isReady;   // (�i��)
    WeaponType weaponType;
};

// �j�U���A�ʥ]
struct PacketLobbyState {
    PacketHeader header;
    LobbySlotInfo slots[kLobbySlotCount];
};

// �}�l�C���ʥ]
struct PacketGameStart {
    PacketHeader header;
};

// 1. �[�J�ШD
struct PacketJoinRequest {
    PacketHeader header;
    // �i�H�[ char name[32];
};

// 2. �[�J�\�i
struct PacketJoinAccept {
    PacketHeader header;
    int yourPlayerID;   // Server ���t���A�� ID
    int yourTeamID;     // 1=Red, 2=Green
};

// 3. ���a���A (��m�P�B)
struct PacketPlayerState {
    PacketHeader header;
    int playerID;       // �֪����A
    glm::vec3 position;
    glm::vec3 velocity; // �Ω�w������
    float rotationY;
    bool isSwimming;
    bool isDead;
    bool isSharking;
};

enum class ProjectileType : uint8_t {
    BULLET = 0,
    BOMB = 1,
    ROCKET = 2
};

// 4. �g���ШD
struct PacketShoot {
    PacketHeader header;
    int playerID;
    glm::vec3 origin;
    glm::vec3 direction;
    int weaponType;
    float speed;
    float scale;
    glm::vec3 color;
    ProjectileType type = ProjectileType::BULLET;
};

// 5. ��a�P�B
struct PacketSplatUpdate {
    PacketHeader header;
    float u;
    float v;
    float radius;
    int teamID;
};

struct PacketSpecialLaser {
    PacketHeader header;
    int playerID;       // �֮g�� (attackerID)
    int teamID;         // �����C��
    glm::vec3 origin;   // �o�g��m
    glm::vec3 direction;// �o�g��V
};

// �j�ۧ����ʥ]
struct PacketSpecialAttack {
    PacketHeader header;
    int playerID;
    int teamID;
    glm::vec3 position;
};

// ���ƻP�C�����A�ʥ]
struct PacketGameState {
    PacketHeader header;
    float scoreTeam1;   // ��������
    float scoreTeam2;   // �񶤤���
    float timeRemaining; // �Ѿl�ɶ� (��)
};

struct PacketKillEvent {
    PacketHeader header;
    int killerID; // ���� ID
    int victimID; // ���� ID
    int killerTeam; // ���ⶤ�� (�Ψ�����C��)
    int victimTeam; // ���̶���
};

#pragma pack(pop)
