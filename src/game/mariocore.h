#ifndef GAME_MARIOCORE_H
#define GAME_MARIOCORE_H

#define MAX_SURFACES 128

#include <inttypes.h>

extern "C" {
	#include <libsm64.h>
}

#include "gamecore.h"


class CMarioCore
{
	friend class CMario;
	CWorldCore *m_pWorld = nullptr;
	CCollision *m_pCollision;
	vec2 m_SpawnPos;

	int marioId;
	float m_Tick;
	float m_Scale;
	uint32_t m_currSurfaces[MAX_SURFACES];

	void deleteBlocks();
	bool addBlock(int x, int y, int *i);
	void loadNewBlocks(int x, int y);

	void exportMap(int spawnX, int spawnY);

public:
	SM64MarioState state;
	SM64MarioInputs input;
	SM64MarioGeometryBuffers geometry;

	vec2 m_Pos, m_LastPos, m_CurrPos;
	vec3 m_GeometryPos[SM64_GEO_MAX_TRIANGLES * 3], m_LastGeometryPos[SM64_GEO_MAX_TRIANGLES * 3], m_CurrGeometryPos[SM64_GEO_MAX_TRIANGLES * 3];

	void Init(CWorldCore *pWorld, CCollision *pCollision, vec2 spawnpos, float scale);
	void Destroy();
	void Reset();
	void Tick(float tickspeed);

	int ID() const {return marioId;}
	float Scale() const {return m_Scale;}
	bool Spawned() const {return marioId != -1;}
	CCollision *Collision() { return m_pCollision; }
};

#endif
