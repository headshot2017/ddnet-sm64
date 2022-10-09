#ifndef GAME_SERVER_ENTITIES_MARIO_H
#define GAME_SERVER_ENTITIES_MARIO_H

#define MARIO_SCALE 1.f
#define IMARIO_SCALE 1

#include <game/server/entity.h>
#include <game/server/player.h>

extern "C" {
	#include <libsm64.h>
}

class CMario : public CEntity
{
	int marioId;
	float m_Tick;
	std::vector<int> vertexIDs;
	int m_Owner;

public:
	SM64MarioState state;
	SM64MarioInputs input;
	SM64MarioGeometryBuffers geometry;

	CMario(CGameWorld *pGameWorld, vec2 Pos, int owner);

	int ID() const {return marioId;}
	int Owner() const {return m_Owner;}

	void Destroy() override;
	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_ENTITIES_MARIO_H
