#include "mario.h"
#include "character.h"

#include <stdlib.h>
#include <string.h>

#include <engine/shared/config.h>

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include "../quickhull/QuickHull.hpp"
#include "../ConvexHull/ConvexHull.h"

extern "C" {
	#include <decomp/include/sm64shared.h>
	#include <decomp/include/audio_defines.h>
}

CMario::CMario(CGameWorld *pGameWorld, vec2 Pos, int owner) : CEntity(pGameWorld, CGameWorld::ENTTYPE_MARIO, Pos)
{
	GameWorld()->InsertEntity(this);
	m_Owner = owner;

	CCharacter *character = GameServer()->GetPlayerChar(m_Owner);
	if (!character)
	{
		GameServer()->SendChatTarget(owner, "You must be in-game to spawn Mario");
		m_MarkedForDestroy = true;
		return;
	}

	m_Core.Init(&GameServer()->m_World.m_Core, Collision(), Pos, g_Config.m_MarioScale/100.f, character->m_pTeleOuts);
	GameServer()->m_World.m_Core.m_apMarios[owner] = &m_Core;

	if (!m_Core.Spawned())
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "libsm64", "Failed to spawn Mario");
		GameServer()->SendChatTarget(owner, "Failed to spawn Mario");
		m_MarkedForDestroy = true;
		return;
	}

	//GameServer()->m_apPlayers[m_Owner]->Pause(CPlayer::PAUSE_SPEC, true);
	//GameServer()->m_apPlayers[m_Owner]->m_SpectatorID = m_Owner;

	// disable jumps, ground and air speed prediction
	character->SetJumping(false);

	CTuningParams TuningParams;
	CTuningParams oldTuning = GameServer()->TuningCopy();

	TuningParams.Set("ground_control_speed", 0);
	TuningParams.Set("ground_jump_impulse", 0);
	TuningParams.Set("air_control_speed", 0);
	TuningParams.Set("air_jump_impulse", 0);
	TuningParams.Set("gravity", 0);

	GameServer()->SetTuning(TuningParams);
	GameServer()->SendTuningParams(m_Owner);
	GameServer()->SetTuning(oldTuning);
}

void CMario::Destroy()
{
	m_Core.Destroy();
	GameServer()->m_World.m_Core.m_apMarios[m_Owner] = 0;
	m_MarkedForDestroy = true;
	if (GameServer()->m_apPlayers[m_Owner])
	{
		GameServer()->SendTuningParams(m_Owner);
		//GameServer()->m_apPlayers[m_Owner]->Pause(CPlayer::PAUSE_NONE, true);

		CCharacter *character = GameServer()->GetPlayerChar(m_Owner);
		if (character) character->SetJumping(true);
	}
}

void CMario::Reset()
{
	Destroy();
}

void CMario::Tick()
{
	if (!m_Core.Spawned()) return;

	CPlayer *player = GameServer()->m_apPlayers[m_Owner];
	CCharacter *character = GameServer()->GetPlayerChar(m_Owner);
	if (!player || !character)
	{
		Destroy();
		return;
	}

	player->m_SpectatorID = m_Owner;
	m_Core.input.stickX = -character->GetLatestInput()->m_Direction;
	m_Core.input.buttonA = character->GetLatestInput()->m_Jump;
	m_Core.input.buttonB = character->GetLatestInput()->m_Fire & 1;
	m_Core.input.buttonZ = character->GetLatestInput()->m_Hook;

	if (g_Config.m_MarioAttackTees)
	{
		for (int i=0; i<MAX_CLIENTS; i++)
		{
			CCharacter *Char = GameServer()->GetPlayerChar(i);
			if (!Char || i == m_Owner) continue;

			float dist = distance(Char->m_Pos, m_Core.m_Pos);
			if (dist < 48 && sm64_mario_attack(m_Core.ID(), Char->m_Pos.x/m_Core.Scale(), Char->m_Pos.y/-m_Core.Scale(), 0, 0))
			{
				// tee attacked
				if (m_Core.state.action == ACT_GROUND_POUND)
				{
					sm64_set_mario_action(m_Core.ID(), ACT_TRIPLE_JUMP);
					sm64_play_sound_global(SOUND_ACTION_HIT);

					GameServer()->CreateDamageInd(Char->m_Pos, -atan2(0, 1), 5);
				}
				else
					GameServer()->CreateDamageInd(Char->m_Pos, -atan2(0, 1), 1);

				GameServer()->CreateHammerHit(Char->m_Pos);
			}
		}
	}

	m_Core.Tick(1.f/Server()->TickSpeed());

	m_Pos = m_Core.m_Pos;
	player->m_ViewPos = vec2(m_Pos.x, m_Pos.y-48);

	character->Core()->m_Pos = character->m_Pos = vec2(m_Pos.x, m_Pos.y-8);
	character->Core()->m_Vel = vec2(0,0);
	character->ResetHook();
}

void CMario::Snap(int SnappingClient)
{
	if (!m_Core.Spawned()) return;
	if (!GameServer()->m_apPlayers[m_Owner] || !GameServer()->GetPlayerChar(m_Owner)) return;
	if (NetworkClipped(SnappingClient, m_Pos)) return;

	std::vector<ivec2> verticesSnapped;
	for (int id : vertexIDs)
		Server()->SnapFreeID(id);
	vertexIDs.clear();

	// QuickHull
	std::vector<size_t> indexBuffer;
	quickhull::VertexDataSource<float> vertexBuffer;

	// ConvexHull
	std::vector<Coordinate> convexHull;

	size_t end = 3 * m_Core.geometry.numTrianglesUsed;

	switch(g_Config.m_MarioDrawMode)
	{
		case 1:
			{
				quickhull::QuickHull<float> qh; // Could be double as well
				std::vector<quickhull::Vector3<float> > pointCloud;

				for (int i=0; i<3 * m_Core.geometry.numTrianglesUsed; i++)
					pointCloud.push_back(quickhull::Vector3<float>(m_Core.geometry.position[i*3+0], m_Core.geometry.position[i*3+1], m_Core.geometry.position[i*3+2]));

				quickhull::ConvexHull<float> hull = qh.getConvexHull(pointCloud, true, false);
				indexBuffer = hull.getIndexBuffer();
				vertexBuffer = hull.getVertexBuffer();
				end = (indexBuffer.empty()) ? 0 : indexBuffer.size()-1;
			}
			break;

		case 2:
			{
				std::vector<Coordinate> polygonPoints;

				for (int i=0; i<3 * m_Core.geometry.numTrianglesUsed; i++)
					polygonPoints.push_back({m_Core.geometry.position[i*3+0], m_Core.geometry.position[i*3+1]});

				Polygon polygon(polygonPoints);
				convexHull = polygon.ComputeConvexHull();
				end = convexHull.size()-1;
			}
			break;
	}

	for (size_t i=0; i<end; i++)
	{
		ivec2 vertex, vertexTo;
		switch(g_Config.m_MarioDrawMode)
		{
			case 0:
				vertex = ivec2((int)m_Core.geometry.position[i*3+0], (int)m_Core.geometry.position[i*3+1]);
				vertexTo = vertex;
				break;

			case 1:
				vertex = ivec2((int)vertexBuffer[indexBuffer[i]].x, (int)vertexBuffer[indexBuffer[i]].y);
				vertexTo = ivec2((int)vertexBuffer[indexBuffer[i+1]].x, (int)vertexBuffer[indexBuffer[i+1]].y);
				break;

			case 2:
				vertex = ivec2((int)convexHull[i].GetX(), (int)convexHull[i].GetY());
				vertexTo = ivec2((int)convexHull[i+1].GetX(), (int)convexHull[i+1].GetY());
				break;
		}

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

		pObj->m_FromX = vertex.x;
		pObj->m_FromY = vertex.y;
		pObj->m_X = vertex.x;
		pObj->m_Y = vertex.y;
		pObj->m_StartTick = Server()->Tick() + Server()->TickSpeed();
	}
}
