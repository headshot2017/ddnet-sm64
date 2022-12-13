#ifndef GAME_SERVER_ENTITIES_MARIO_H
#define GAME_SERVER_ENTITIES_MARIO_H

#include <game/server/entity.h>
#include <game/server/player.h>
#include <game/mariocore.h>

class CMario : public CEntity
{
	CMarioCore m_Core;
	std::vector<int> vertexIDs;
	int m_Owner;

public:
	CMario(CGameWorld *pGameWorld, vec2 Pos, int owner);

	int Owner() const {return m_Owner;}

	void Destroy() override;
	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_ENTITIES_MARIO_H
