#include <stdio.h>
#include <limits.h>
#include <string.h>

#include <base/math.h>

#include "mariocore.h"
#include "collision.h"

extern "C" {
	#include <decomp/include/sm64shared.h>
	#include <decomp/include/surface_terrains.h>
}

void CMarioCore::Init(CWorldCore *pWorld, CCollision *pCollision, vec2 spawnpos, float scale)
{
	m_pWorld = pWorld;
	m_pCollision = pCollision;

	marioId = -1;
	m_Scale = scale;
	m_SpawnPos = spawnpos;
	memset(m_currSurfaces, UINT_MAX, sizeof(m_currSurfaces));
	memset(&input, 0, sizeof(SM64MarioInputs));

	Reset();
}

void CMarioCore::Destroy()
{
	if (Spawned())
	{
		free(geometry.position);
		free(geometry.normal);
		free(geometry.color);
		free(geometry.uv);
		geometry.position = 0;
		geometry.normal = 0;
		geometry.color = 0;
		geometry.uv = 0;

		sm64_mario_delete(marioId);
		marioId = -1;
	}
}

void CMarioCore::Reset()
{
	Destroy();

	m_Tick = 0;

	// on teeworlds, up coordinate is Y-, SM64 is Y+. flip the Y coordinate
	// scale conversions:
	//    teeworlds -> sm64: divide
	//    sm64 -> teeworlds: multiply
	int spawnX = m_SpawnPos.x/m_Scale;
	int spawnY = -m_SpawnPos.y/m_Scale;

	//exportMap(spawnX, spawnY);

	loadNewBlocks(m_SpawnPos.x/32, -m_SpawnPos.y/32);
	marioId = sm64_mario_create(spawnX, spawnY, 0, 0,0,0,0);

	if (Spawned())
	{
		geometry.position = (float*)malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
		geometry.normal   = (float*)malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
		geometry.color    = (float*)malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
		geometry.uv       = (float*)malloc( sizeof(float) * 6 * SM64_GEO_MAX_TRIANGLES );
		geometry.numTrianglesUsed = 0;
	}
}

void CMarioCore::Tick(float tickspeed)
{
	if (!Spawned())
		return;

	m_Tick += tickspeed;
	while (m_Tick >= 1.f/30)
	{
		m_Tick -= 1.f/30;

		m_LastPos = m_CurrPos;
		mem_copy(m_LastGeometryPos, m_CurrGeometryPos, sizeof(m_CurrGeometryPos));

		sm64_reset_mario_z(marioId);
		sm64_mario_tick(marioId, &input, &state, &geometry);

		vec2 newPos(state.position[0]*m_Scale, -state.position[1]*m_Scale);
		if ((int)(newPos.x/32) != (int)(m_Pos.x/32) || (int)(newPos.y/32) != (int)(m_Pos.y/32))
			loadNewBlocks(newPos.x/32, newPos.y/32);

		m_CurrPos = newPos;
		for (int i=0; i<geometry.numTrianglesUsed*3; i++)
		{
			m_CurrGeometryPos[i*3+0] = geometry.position[i*3+0]*m_Scale;
			m_CurrGeometryPos[i*3+1] = geometry.position[i*3+1]*-m_Scale;
			m_CurrGeometryPos[i*3+2] = geometry.position[i*3+2]*m_Scale;
		}
	}

	m_Pos = mix(m_LastPos, m_CurrPos, m_Tick / (1.f/30));
	for (int i=0; i<geometry.numTrianglesUsed*9; i++)
		geometry.position[i] = mix(m_LastGeometryPos[i], m_CurrGeometryPos[i], m_Tick / (1.f/30));
}

void CMarioCore::deleteBlocks()
{
	for (int j=0; j<MAX_SURFACES; j++)
	{
		if (m_currSurfaces[j] == UINT_MAX) continue;
		sm64_surface_object_delete(m_currSurfaces[j]);
		m_currSurfaces[j] = UINT_MAX;
	}
}

bool CMarioCore::addBlock(int x, int y, int *i)
{
	if ((*i) >= MAX_SURFACES) return false;
	bool block = Collision()->CheckPoint(x*32, y*32);
	if (!block) return false;

	struct SM64SurfaceObject obj;
	memset(&obj.transform, 0, sizeof(struct SM64ObjectTransform));
	obj.transform.position[0] = x*32 / m_Scale;
	obj.transform.position[1] = (-y*32-16) / m_Scale;
	obj.transform.position[2] = 0;
	obj.surfaceCount = 0;
	obj.surfaces = (struct SM64Surface*)malloc(sizeof(struct SM64Surface) * 4*2);

	bool up =		Collision()->CheckPoint(x*32, y*32-32);
	bool down =		Collision()->CheckPoint(x*32, y*32+32);
	bool left =		Collision()->CheckPoint(x*32-32, y*32);
	bool right =	Collision()->CheckPoint(x*32+32, y*32);

	// block ground face
	if (!up)
	{
		obj.surfaces[obj.surfaceCount+0].vertices[0][0] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[0][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[0][2] = 64 / m_Scale;
		obj.surfaces[obj.surfaceCount+0].vertices[1][0] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[1][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[1][2] = -64 / m_Scale;
		obj.surfaces[obj.surfaceCount+0].vertices[2][0] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[2][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[2][2] = 64 / m_Scale;

		obj.surfaces[obj.surfaceCount+1].vertices[0][0] = 0 / m_Scale; 		obj.surfaces[obj.surfaceCount+1].vertices[0][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[0][2] = -64 / m_Scale;
		obj.surfaces[obj.surfaceCount+1].vertices[1][0] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[1][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[1][2] = 64 / m_Scale;
		obj.surfaces[obj.surfaceCount+1].vertices[2][0] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[2][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[2][2] = -64 / m_Scale;

		obj.surfaceCount += 2;
	}

	// left (Z+)
	if (!left)
	{
		obj.surfaces[obj.surfaceCount+0].vertices[0][2] = -64 / m_Scale;	obj.surfaces[obj.surfaceCount+0].vertices[0][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[0][0] = 0 / m_Scale;
		obj.surfaces[obj.surfaceCount+0].vertices[1][2] = 64 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[1][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[1][0] = 0 / m_Scale;
		obj.surfaces[obj.surfaceCount+0].vertices[2][2] = -64 / m_Scale;	obj.surfaces[obj.surfaceCount+0].vertices[2][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[2][0] = 0 / m_Scale;

		obj.surfaces[obj.surfaceCount+1].vertices[0][2] = 64 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[0][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[0][0] = 0 / m_Scale;
		obj.surfaces[obj.surfaceCount+1].vertices[1][2] = -64 / m_Scale;	obj.surfaces[obj.surfaceCount+1].vertices[1][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[1][0] = 0 / m_Scale;
		obj.surfaces[obj.surfaceCount+1].vertices[2][2] = 64 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[2][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[2][0] = 0 / m_Scale;

		obj.surfaceCount += 2;
	}

	// right (Z-)
	if (!right)
	{
		obj.surfaces[obj.surfaceCount+0].vertices[0][2] = 64 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[0][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[0][0] = 32 / m_Scale;
		obj.surfaces[obj.surfaceCount+0].vertices[1][2] = -64 / m_Scale;	obj.surfaces[obj.surfaceCount+0].vertices[1][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[1][0] = 32 / m_Scale;
		obj.surfaces[obj.surfaceCount+0].vertices[2][2] = 64 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[2][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[2][0] = 32 / m_Scale;

		obj.surfaces[obj.surfaceCount+1].vertices[0][2] = -64 / m_Scale;	obj.surfaces[obj.surfaceCount+1].vertices[0][1] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[0][0] = 32 / m_Scale;
		obj.surfaces[obj.surfaceCount+1].vertices[1][2] = 64 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[1][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[1][0] = 32 / m_Scale;
		obj.surfaces[obj.surfaceCount+1].vertices[2][2] = -64 / m_Scale;	obj.surfaces[obj.surfaceCount+1].vertices[2][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[2][0] = 32 / m_Scale;

		obj.surfaceCount += 2;
	}

	// block bottom face
	if (!down)
	{
		obj.surfaces[obj.surfaceCount+0].vertices[0][0] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[0][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[0][2] = 64 / m_Scale;
		obj.surfaces[obj.surfaceCount+0].vertices[1][0] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[1][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[1][2] = -64 / m_Scale;
		obj.surfaces[obj.surfaceCount+0].vertices[2][0] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[2][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+0].vertices[2][2] = 64 / m_Scale;

		obj.surfaces[obj.surfaceCount+1].vertices[0][0] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[0][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[0][2] = -64 / m_Scale;
		obj.surfaces[obj.surfaceCount+1].vertices[1][0] = 32 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[1][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[1][2] = 64 / m_Scale;
		obj.surfaces[obj.surfaceCount+1].vertices[2][0] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[2][1] = 0 / m_Scale;		obj.surfaces[obj.surfaceCount+1].vertices[2][2] = -64 / m_Scale;

		obj.surfaceCount += 2;
	}

	for (uint32_t ind=0; ind<obj.surfaceCount; ind++)
	{
		obj.surfaces[ind].type = SURFACE_DEFAULT;
		obj.surfaces[ind].force = 0;
		obj.surfaces[ind].terrain = TERRAIN_STONE;
	}

	if (obj.surfaceCount)
		m_currSurfaces[(*i)++] = sm64_surface_object_create(&obj);

	free(obj.surfaces);
	return true;
}

void CMarioCore::loadNewBlocks(int x, int y)
{
	deleteBlocks();
	int yadd = 0;

	int arrayInd = 0;
	for (int xadd=-7; xadd<=7; xadd++)
	{
		// get block at floor
		for (yadd=0; y+yadd<=Collision()->GetHeight(); yadd++)
		{
			if (addBlock(x+xadd, y+yadd, &arrayInd)) break;
		}

		for (yadd=6; yadd>=0; yadd--)
		{
			addBlock(x+xadd, y-yadd, &arrayInd);
		}
	}
}

void CMarioCore::exportMap(int spawnX, int spawnY)
{
	FILE *f = fopen("level.c", "w");

	fprintf(f, "#include \"level.h\"\n#include \"../src/decomp/include/surface_terrains.h\"\nconst struct SM64Surface surfaces[] = {\n");

	for (int y=0; y<Collision()->GetHeight(); y++)
	{
		for (int x=0; x<Collision()->GetWidth(); x++)
		{
			bool block = Collision()->CheckPoint(x*32, y*32);
			if (!block) continue;

			vec3 pos(x*32 / m_Scale, (-y*32-16) / m_Scale, 0);

			bool up =		Collision()->CheckPoint(x*32, y*32-32);
			bool down =		Collision()->CheckPoint(x*32, y*32+32);
			bool left =		Collision()->CheckPoint(x*32-32, y*32);
			bool right =	Collision()->CheckPoint(x*32+32, y*32);

			int vertices1[3][3], vertices2[3][3];

			// block ground face
			if (!up)
			{
				vertices1[0][0] = pos.x + (32 / m_Scale);	vertices1[0][1] = pos.y + (32 / m_Scale);	vertices1[0][2] = pos.z + (64 / m_Scale);
				vertices1[1][0] = pos.x + (0 / m_Scale);	vertices1[1][1] = pos.y + (32 / m_Scale);	vertices1[1][2] = pos.z + (-64 / m_Scale);
				vertices1[2][0] = pos.x + (0 / m_Scale);	vertices1[2][1] = pos.y + (32 / m_Scale);	vertices1[2][2] = pos.z + (64 / m_Scale);

				vertices2[0][0] = pos.x + (0 / m_Scale); 	vertices2[0][1] = pos.y + (32 / m_Scale);	vertices2[0][2] = pos.z + (-64 / m_Scale);
				vertices2[1][0] = pos.x + (32 / m_Scale);	vertices2[1][1] = pos.y + (32 / m_Scale);	vertices2[1][2] = pos.z + (64 / m_Scale);
				vertices2[2][0] = pos.x + (32 / m_Scale);	vertices2[2][1] = pos.y + (32 / m_Scale);	vertices2[2][2] = pos.z + (-64 / m_Scale);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices1[0][0], vertices1[0][1], vertices1[0][2],
					vertices1[1][0], vertices1[1][1], vertices1[1][2],
					vertices1[2][0], vertices1[2][1], vertices1[2][2]);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices2[0][0], vertices2[0][1], vertices2[0][2],
					vertices2[1][0], vertices2[1][1], vertices2[1][2],
					vertices2[2][0], vertices2[2][1], vertices2[2][2]);
			}

			// left (X-)
			if (!left)
			{
				vertices1[0][2] = pos.z + (-64 / m_Scale);	vertices1[0][1] = pos.y + (0 / m_Scale);	vertices1[0][0] = pos.x + (0 / m_Scale);
				vertices1[1][2] = pos.z + (64 / m_Scale);	vertices1[1][1] = pos.y + (32 / m_Scale);	vertices1[1][0] = pos.x + (0 / m_Scale);
				vertices1[2][2] = pos.z + (-64 / m_Scale);	vertices1[2][1] = pos.y + (32 / m_Scale);	vertices1[2][0] = pos.x + (0 / m_Scale);

				vertices2[0][2] = pos.z + (64 / m_Scale);	vertices2[0][1] = pos.y + (32 / m_Scale);	vertices2[0][0] = pos.x + (0 / m_Scale);
				vertices2[1][2] = pos.z + (-64 / m_Scale);	vertices2[1][1] = pos.y + (0 / m_Scale);	vertices2[1][0] = pos.x + (0 / m_Scale);
				vertices2[2][2] = pos.z + (64 / m_Scale);	vertices2[2][1] = pos.y + (0 / m_Scale);	vertices2[2][0] = pos.x + (0 / m_Scale);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices1[0][0], vertices1[0][1], vertices1[0][2],
					vertices1[1][0], vertices1[1][1], vertices1[1][2],
					vertices1[2][0], vertices1[2][1], vertices1[2][2]);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices2[0][0], vertices2[0][1], vertices2[0][2],
					vertices2[1][0], vertices2[1][1], vertices2[1][2],
					vertices2[2][0], vertices2[2][1], vertices2[2][2]);
			}

			// right (X+)
			if (!right)
			{
				vertices1[0][2] = pos.z + (64 / m_Scale);	vertices1[0][1] = pos.y + (0 / m_Scale);	vertices1[0][0] = pos.x + (32 / m_Scale);
				vertices1[1][2] = pos.z + (-64 / m_Scale);	vertices1[1][1] = pos.y + (32 / m_Scale);	vertices1[1][0] = pos.x + (32 / m_Scale);
				vertices1[2][2] = pos.z + (64 / m_Scale);	vertices1[2][1] = pos.y + (32 / m_Scale);	vertices1[2][0] = pos.x + (32 / m_Scale);

				vertices2[0][2] = pos.z + (-64 / m_Scale);	vertices2[0][1] = pos.y + (32 / m_Scale);	vertices2[0][0] = pos.x + (32 / m_Scale);
				vertices2[1][2] = pos.z + (64 / m_Scale);	vertices2[1][1] = pos.y + (0 / m_Scale);	vertices2[1][0] = pos.x + (32 / m_Scale);
				vertices2[2][2] = pos.z + (-64 / m_Scale);	vertices2[2][1] = pos.y + (0 / m_Scale);	vertices2[2][0] = pos.x + (32 / m_Scale);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices1[0][0], vertices1[0][1], vertices1[0][2],
					vertices1[1][0], vertices1[1][1], vertices1[1][2],
					vertices1[2][0], vertices1[2][1], vertices1[2][2]);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices2[0][0], vertices2[0][1], vertices2[0][2],
					vertices2[1][0], vertices2[1][1], vertices2[1][2],
					vertices2[2][0], vertices2[2][1], vertices2[2][2]);
			}

			// front
			{
				vertices1[0][0] = pos.x + (32 / m_Scale);	vertices1[0][1] = pos.y + (0 / m_Scale);	vertices1[0][2] = pos.z + (-64 / m_Scale);
				vertices1[1][0] = pos.x + (0 / m_Scale);	vertices1[1][1] = pos.y + (32 / m_Scale);	vertices1[1][2] = pos.z + (-64 / m_Scale);
				vertices1[2][0] = pos.x + (32 / m_Scale);	vertices1[2][1] = pos.y + (32 / m_Scale);	vertices1[2][2] = pos.z + (-64 / m_Scale);

				vertices2[0][0] = pos.x + (0 / m_Scale);	vertices2[0][1] = pos.y + (32 / m_Scale);	vertices2[0][2] = pos.z + (-64 / m_Scale);
				vertices2[1][0] = pos.x + (32 / m_Scale);	vertices2[1][1] = pos.y + (0 / m_Scale);	vertices2[1][2] = pos.z + (-64 / m_Scale);
				vertices2[2][0] = pos.x + (0 / m_Scale);	vertices2[2][1] = pos.y + (0 / m_Scale);	vertices2[2][2] = pos.z + (-64 / m_Scale);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices1[0][0], vertices1[0][1], vertices1[0][2],
					vertices1[1][0], vertices1[1][1], vertices1[1][2],
					vertices1[2][0], vertices1[2][1], vertices1[2][2]);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices2[0][0], vertices2[0][1], vertices2[0][2],
					vertices2[1][0], vertices2[1][1], vertices2[1][2],
					vertices2[2][0], vertices2[2][1], vertices2[2][2]);
			}

			// back
			{
				vertices1[0][0] = pos.x + (0 / m_Scale);	vertices1[0][1] = pos.y + (0 / m_Scale);	vertices1[0][2] = pos.z + (64 / m_Scale);
				vertices1[1][0] = pos.x + (32 / m_Scale);	vertices1[1][1] = pos.y + (32 / m_Scale);	vertices1[1][2] = pos.z + (64 / m_Scale);
				vertices1[2][0] = pos.x + (0 / m_Scale);	vertices1[2][1] = pos.y + (32 / m_Scale);	vertices1[2][2] = pos.z + (64 / m_Scale);

				vertices2[0][0] = pos.x + (32 / m_Scale);	vertices2[0][1] = pos.y + (32 / m_Scale);	vertices2[0][2] = pos.z + (64 / m_Scale);
				vertices2[1][0] = pos.x + (0 / m_Scale);	vertices2[1][1] = pos.y + (0 / m_Scale);	vertices2[1][2] = pos.z + (64 / m_Scale);
				vertices2[2][0] = pos.x + (32 / m_Scale);	vertices2[2][1] = pos.y + (0 / m_Scale);	vertices2[2][2] = pos.z + (64 / m_Scale);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices1[0][0], vertices1[0][1], vertices1[0][2],
					vertices1[1][0], vertices1[1][1], vertices1[1][2],
					vertices1[2][0], vertices1[2][1], vertices1[2][2]);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices2[0][0], vertices2[0][1], vertices2[0][2],
					vertices2[1][0], vertices2[1][1], vertices2[1][2],
					vertices2[2][0], vertices2[2][1], vertices2[2][2]);
			}

			// block bottom face
			if (!down)
			{
				vertices1[0][0] = pos.x + (0 / m_Scale);	vertices1[0][1] = pos.y + (0 / m_Scale);	vertices1[0][2] = pos.z + (64 / m_Scale);
				vertices1[1][0] = pos.x + (0 / m_Scale);	vertices1[1][1] = pos.y + (0 / m_Scale);	vertices1[1][2] = pos.z + (-64 / m_Scale);
				vertices1[2][0] = pos.x + (32 / m_Scale);	vertices1[2][1] = pos.y + (0 / m_Scale);	vertices1[2][2] = pos.z + (64 / m_Scale);

				vertices2[0][0] = pos.x + (32 / m_Scale);	vertices2[0][1] = pos.y + (0 / m_Scale);	vertices2[0][2] = pos.z + (-64 / m_Scale);
				vertices2[1][0] = pos.x + (32 / m_Scale);	vertices2[1][1] = pos.y + (0 / m_Scale);	vertices2[1][2] = pos.z + (64 / m_Scale);
				vertices2[2][0] = pos.x + (0 / m_Scale);	vertices2[2][1] = pos.y + (0 / m_Scale);	vertices2[2][2] = pos.z + (-64 / m_Scale);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices1[0][0], vertices1[0][1], vertices1[0][2],
					vertices1[1][0], vertices1[1][1], vertices1[1][2],
					vertices1[2][0], vertices1[2][1], vertices1[2][2]);

				fprintf(f, "{SURFACE_DEFAULT,0,TERRAIN_STONE,{{%d,%d,%d},{%d,%d,%d},{%d,%d,%d}}},\n",
					vertices2[0][0], vertices2[0][1], vertices2[0][2],
					vertices2[1][0], vertices2[1][1], vertices2[1][2],
					vertices2[2][0], vertices2[2][1], vertices2[2][2]);
			}
		}
	}

	fprintf(f, "};\nconst size_t surfaces_count = sizeof( surfaces ) / sizeof( surfaces[0] );\nconst int32_t spawn[3] = {%d, %d, 0};\n", spawnX, spawnY);
	fclose(f);
}
