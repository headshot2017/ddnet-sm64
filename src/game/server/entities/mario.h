#ifndef GAME_SERVER_ENTITIES_MARIO_H
#define GAME_SERVER_ENTITIES_MARIO_H

#define MAX_SURFACES 128

#include <inttypes.h>

#include <game/server/entity.h>
#include <game/server/player.h>

extern "C" {
	#include <libsm64.h>
}

class CMario : public CEntity
{
	int marioId;
	float m_Tick;
	float m_Scale;
	std::vector<int> vertexIDs;
	int m_Owner;
	uint32_t m_currSurfaces[MAX_SURFACES];

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

	void deleteBlocks();
	bool addBlock(int x, int y, int *i);
	void loadNewBlocks(int x, int y);

	void exportMap(int spawnX, int spawnY);
};

#endif // GAME_SERVER_ENTITIES_MARIO_H
