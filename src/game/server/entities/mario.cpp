#include "mario.h"
#include "character.h"
extern "C" {
	#include <decomp/include/sm64shared.h>
	#include <decomp/include/surface_terrains.h>
}

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

CMario::CMario(CGameWorld *pGameWorld, vec2 Pos, int owner) : CEntity(pGameWorld, CGameWorld::ENTTYPE_MARIO, Pos)
{
	GameWorld()->InsertEntity(this);
	m_Owner = owner;

	// on teeworlds, up coordinate is Y-, SM64 is Y+. flip the Y coordinate
	int spawnX = Pos.x/IMARIO_SCALE;
	int spawnY = -Pos.y/IMARIO_SCALE;

	geometry.position = (float*)malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
	geometry.normal   = (float*)malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
	geometry.color    = (float*)malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
	geometry.uv       = (float*)malloc( sizeof(float) * 6 * SM64_GEO_MAX_TRIANGLES );
	geometry.numTrianglesUsed = 0;
	memset(&input, 0, sizeof(SM64MarioInputs));
	memset(&state, 0, sizeof(SM64MarioState));

	uint32_t surfaceCount = 2;
	SM64Surface surfaces[surfaceCount];

	for (uint32_t i=0; i<surfaceCount; i++)
	{
		surfaces[i].type = SURFACE_DEFAULT;
		surfaces[i].force = 0;
		surfaces[i].terrain = TERRAIN_STONE;
	}
	
	surfaces[surfaceCount-2].vertices[0][0] = spawnX + 16384;	surfaces[surfaceCount-2].vertices[0][1] = spawnY;	surfaces[surfaceCount-2].vertices[0][2] = +128;
	surfaces[surfaceCount-2].vertices[1][0] = spawnX - 16384;	surfaces[surfaceCount-2].vertices[1][1] = spawnY;	surfaces[surfaceCount-2].vertices[1][2] = -128;
	surfaces[surfaceCount-2].vertices[2][0] = spawnX - 16384;	surfaces[surfaceCount-2].vertices[2][1] = spawnY;	surfaces[surfaceCount-2].vertices[2][2] = +128;

	surfaces[surfaceCount-1].vertices[0][0] = spawnX - 16384;	surfaces[surfaceCount-1].vertices[0][1] = spawnY;	surfaces[surfaceCount-1].vertices[0][2] = -128;
	surfaces[surfaceCount-1].vertices[1][0] = spawnX + 16384;	surfaces[surfaceCount-1].vertices[1][1] = spawnY;	surfaces[surfaceCount-1].vertices[1][2] = +128;
	surfaces[surfaceCount-1].vertices[2][0] = spawnX + 16384;	surfaces[surfaceCount-1].vertices[2][1] = spawnY;	surfaces[surfaceCount-1].vertices[2][2] = -128;

	sm64_static_surfaces_load(surfaces, surfaceCount);

	marioId = sm64_mario_create(spawnX, spawnY, 0, 0,0,0,0);
	if (marioId == -1)
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "libsm64", "Failed to spawn Mario");
		m_MarkedForDestroy = true;
		return;
	}

	//GameServer()->m_apPlayers[m_Owner]->Pause(CPlayer::PAUSE_PAUSED, true);
	//GameServer()->m_apPlayers[m_Owner]->m_SpectatorID = m_Owner;
}

void CMario::Destroy()
{
	if (marioId != -1)
	{
		free(geometry.position);
		free(geometry.normal);
		free(geometry.color);
		free(geometry.uv);
		sm64_mario_delete(marioId);
		marioId = -1;
	}
	m_MarkedForDestroy = true;
	//if (GameServer()->m_apPlayers[m_Owner])
		//GameServer()->m_apPlayers[m_Owner]->Pause(CPlayer::PAUSE_NONE, true);
}

void CMario::Reset()
{
	Destroy();
}

void CMario::Tick()
{
	if (marioId == -1) return;

	CPlayer *player = GameServer()->m_apPlayers[m_Owner];
	CCharacter *character = GameServer()->GetPlayerChar(m_Owner);
	if (!player || !character)
	{
		Destroy();
		return;
	}

	player->m_SpectatorID = m_Owner;
	input.stickX = -character->GetInput()->m_Direction;
	input.buttonA = character->GetInput()->m_Jump;
	input.buttonB = character->GetInput()->m_Fire & 1;
	input.buttonZ = character->GetInput()->m_Hook;

	m_Tick += 1.f/Server()->TickSpeed();
	while (m_Tick >= 1.f/30)
	{
		m_Tick -= 1.f/30;

		sm64_reset_mario_z(marioId);
		sm64_mario_tick(marioId, &input, &state, &geometry);

		m_Pos.x = state.position[0]*MARIO_SCALE;
		m_Pos.y = -state.position[1]*MARIO_SCALE;

		player->m_ViewPos = vec2(m_Pos.x, m_Pos.y-48);
	}

	character->Core()->m_Pos = m_Pos;
	character->Core()->m_Vel = vec2(0,0);
	character->ResetHook();
}

void CMario::Snap(int SnappingClient)
{
	if (marioId == -1) return;
	if (!GameServer()->m_apPlayers[m_Owner] || !GameServer()->GetPlayerChar(m_Owner)) return;
	if (NetworkClipped(SnappingClient, m_Pos)) return;

	std::vector<ivec2> verticesSnapped;
	for (int id : vertexIDs)
		Server()->SnapFreeID(id);
	vertexIDs.clear();

	for (int i=0; i<3 * geometry.numTrianglesUsed; i++)
	{
		//if (geometry.position[i*3+2] < 0) continue; // Z coordinate
		ivec2 vertex((int)geometry.position[i*3+0]*MARIO_SCALE, -(int)geometry.position[i*3+1]*MARIO_SCALE);
		bool repeated = false;

		for (size_t j=0; j<verticesSnapped.size(); j++)
		{
			if (verticesSnapped[j] == vertex)
			{
				repeated = true;
				break;
			}
		}

		if (repeated) continue;
		verticesSnapped.push_back(vertex);
		int id = Server()->SnapNewID();
		vertexIDs.push_back(id);

		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, id, sizeof(CNetObj_Laser)));
		if(!pObj)
			continue;

		pObj->m_FromX = pObj->m_X = vertex.x;
		pObj->m_FromY = pObj->m_Y = vertex.y;
		pObj->m_StartTick = Server()->Tick() + Server()->TickSpeed();
	}
}
