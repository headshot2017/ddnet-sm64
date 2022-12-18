#include <string.h>
#include <inttypes.h>
#include <chrono>

#include "marios.h"

#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>
#include <base/hash_ctxt.h>

#include <engine/shared/config.h>

#include <game/mariocore.h>
#include <game/client/gameclient.h>

#define SEQUENCE_ARGS(priority, seqId) ((priority << 8) | seqId)
extern "C" {
	#include <decomp/include/sm64shared.h>
	#include <decomp/include/audio_defines.h>
	#include <decomp/include/surface_terrains.h>
	#include <decomp/include/seq_ids.h>
}

using namespace std::chrono_literals;

static const char *MARIO_SHADER =
"\n uniform mat4 view;"
"\n uniform mat4 projection;"
"\n uniform sampler2D marioTex;"
"\n uniform int wingCap;"
"\n uniform int metalCap;"
"\n "
"\n v2f vec3 v_color;"
"\n v2f vec3 v_normal;"
"\n v2f vec3 v_light;"
"\n v2f vec2 v_uv;"
"\n "
"\n #ifdef VERTEX"
"\n "
"\n     in vec3 position;"
"\n     in vec3 normal;"
"\n     in vec3 color;"
"\n     in vec2 uv;"
"\n "
"\n     void main()"
"\n     {"
"\n         v_color = color;"
"\n         v_normal = normal;"
"\n         v_light = transpose( mat3( view )) * normalize( vec3( 1 ));"
"\n         v_uv = uv;"
"\n "
"\n         gl_Position = projection * view * vec4( position, 1. );"
"\n     }"
"\n "
"\n #endif"
"\n #ifdef FRAGMENT"
"\n "
"\n     out vec4 color;"
"\n "
"\n     void main() "
"\n     {"
"\n         float light = .5 + .5 * clamp( dot( v_normal, v_light ), 0., 1. );"
"\n         vec4 texColor = vec4(0);"
"\n         if (wingCap == 0 && metalCap == 0) texColor = texture2D(marioTex, v_uv);"
"\n         else if (wingCap == 1)"
"\n         {"
"\n             texColor = texture2D(marioTex, v_uv);"
"\n             if (texColor.a != 1) discard;"
"\n         }"
"\n         else if (metalCap == 1) texColor = texture2D(marioTex, v_uv); // NEED A WAY TO MAKE REFLECTION"
"\n         vec3 mainColor = mix( v_color, texColor.rgb, texColor.a ); // v_uv.x >= 0. ? texColor.a : 0. );"
"\n         color = vec4( mainColor * light, 1 );"
"\n     }"
"\n "
"\n #endif";


void CMarios::OnConsoleInit()
{
	Console()->Register("mario", "", CFGFLAG_CLIENT, ConMario, this, "Toggle Mario");
	Console()->Register("mario_kill", "", CFGFLAG_CLIENT, ConMarioKill, this, "Kills Mario instantly");
	Console()->Register("mario_music", "i[ID]", CFGFLAG_CLIENT, ConMarioMusic, this, "Play SM64 music. Valid music IDs from 0 to 34. ID 0 stops music");
	Console()->Register("mario_cap", "s[cap]", CFGFLAG_CLIENT, ConMarioCap, this, "Switches Mario's cap: off, on, wing, metal");
}

void CMarios::OnInit()
{
	FILE *f = fopen("sm64.us.z64", "rb");

	if (!f)
	{
		m_pClient->m_Menus.PopupWarning("libsm64", "Super Mario 64 US ROM not found!\nPlease provide a ROM with the filename \"sm64.us.z64\"", "OK", 30s);
	}
	else
	{
		// load ROM into memory
		uint8_t *romBuffer;
		size_t romFileLength;

		fseek(f, 0, SEEK_END);
		romFileLength = (size_t)ftell(f);
		rewind(f);
		romBuffer = (uint8_t*)malloc(romFileLength + 1);
		fread(romBuffer, 1, romFileLength, f);
		romBuffer[romFileLength] = 0;
		fclose(f);

		// perform MD5 check to make sure it's the correct ROM
		MD5_CTX ctxt;
		md5_init(&ctxt);
		md5_update(&ctxt, romBuffer, romFileLength);
		MD5_DIGEST result = md5_finish(&ctxt);

		char hexResult[MD5_MAXSTRSIZE];
		md5_str(result, hexResult, sizeof(hexResult));

		const char *SM64_MD5 = "20b854b239203baf6c961b850a4a51a2";
		if (str_comp(hexResult, SM64_MD5)) // mismatch
		{
			char msg[256];
			sprintf(msg,
				"Super Mario 64 US ROM MD5 mismatch!\n"
				"Expected: %s\n"
				"Your copy: %s\n"
				"Please provide the correct ROM",
				SM64_MD5, hexResult);

			free(romBuffer);
			m_pClient->m_Menus.PopupWarning("libsm64", msg, "OK", 30s);
		}
		else
		{
			// Mario texture is 704x64 RGBA
			m_MarioTexture = (uint8_t*)malloc(4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT);

			// load libsm64
			sm64_global_init(romBuffer, m_MarioTexture, [](const char *msg) {dbg_msg("libsm64", "%s", msg);});
			dbg_msg("libsm64", "Super Mario 64 US ROM loaded!");
			sm64_play_sound_global(SOUND_MENU_STAR_SOUND);
			free(romBuffer);

			Graphics()->firstInitMario(&m_MarioShaderHandle, &m_MarioTexHandle, m_MarioTexture, MARIO_SHADER);

			for(int i=0; i<3*SM64_GEO_MAX_TRIANGLES; i++) m_MarioIndices[i] = i;
		}
	}
}

void CMarios::OnMapLoad()
{
	uint32_t surfaceCount = 2;
	SM64Surface surfaces[surfaceCount];

	for (uint32_t i=0; i<surfaceCount; i++)
	{
		surfaces[i].type = SURFACE_DEFAULT;
		surfaces[i].force = 0;
		surfaces[i].terrain = TERRAIN_STONE;
	}
	
	int width = Collision()->GetWidth()/2 * 32 / (g_Config.m_MarioScale/100.f);
	int spawnX = width;
	int spawnY = (Collision()->GetHeight()+205) * 32 / (-g_Config.m_MarioScale/100.f);
	
	surfaces[surfaceCount-2].vertices[0][0] = spawnX + width + (400*32);	surfaces[surfaceCount-2].vertices[0][1] = spawnY;	surfaces[surfaceCount-2].vertices[0][2] = +128;
	surfaces[surfaceCount-2].vertices[1][0] = spawnX - width - (400*32);	surfaces[surfaceCount-2].vertices[1][1] = spawnY;	surfaces[surfaceCount-2].vertices[1][2] = -128;
	surfaces[surfaceCount-2].vertices[2][0] = spawnX - width - (400*32);	surfaces[surfaceCount-2].vertices[2][1] = spawnY;	surfaces[surfaceCount-2].vertices[2][2] = +128;

	surfaces[surfaceCount-1].vertices[0][0] = spawnX - width - (400*32);	surfaces[surfaceCount-1].vertices[0][1] = spawnY;	surfaces[surfaceCount-1].vertices[0][2] = -128;
	surfaces[surfaceCount-1].vertices[1][0] = spawnX + width + (400*32);	surfaces[surfaceCount-1].vertices[1][1] = spawnY;	surfaces[surfaceCount-1].vertices[1][2] = +128;
	surfaces[surfaceCount-1].vertices[2][0] = spawnX + width + (400*32);	surfaces[surfaceCount-1].vertices[2][1] = spawnY;	surfaces[surfaceCount-1].vertices[2][2] = -128;

	sm64_static_surfaces_load(surfaces, surfaceCount);
}

void CMarios::OnStateChange(int NewState, int OldState)
{
	if (OldState == IClient::STATE_ONLINE)
	{
		// disconnected, destroy all marios
		for (int i=0; i<MAX_CLIENTS; i++)
		{
			if (m_pClient->m_GameWorld.m_Core.m_apMarios[i])
			{
				delete m_pClient->m_GameWorld.m_Core.m_apMarios[i];
				m_pClient->m_GameWorld.m_Core.m_apMarios[i] = 0;

				CMarioMesh *mesh = &m_MarioMeshes[i];
				Graphics()->destroyMario(mesh);
			}
		}
	}
}

void CMarios::TickAndRenderMario(int ID)
{
	CMarioCore *mario = m_pClient->m_GameWorld.m_Core.m_apMarios[ID];

	if (ID == m_pClient->m_Snap.m_LocalClientID)
	{
		//CNetObj_PlayerInput *input = (CNetObj_PlayerInput *)Client()->GetInput(Client()->GameTick(g_Config.m_ClDummy), g_Config.m_ClDummy);
		CNetObj_PlayerInput *input = &GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
		mario->input.stickX = GameClient()->m_Controls.m_aInputDirectionLeft[g_Config.m_ClDummy] ? 1 : GameClient()->m_Controls.m_aInputDirectionRight[g_Config.m_ClDummy] ? -1 : 0;
		mario->input.stickY = 0;
		mario->input.buttonA = input->m_Jump;
		mario->input.buttonB = input->m_Fire & 1;
		mario->input.buttonZ = input->m_Hook;
	}

	if (g_Config.m_MarioAttackTees)
	{
		for (int i=0; i<MAX_CLIENTS; i++)
		{
			CCharacterCore *Char = m_pClient->m_GameWorld.m_Core.m_apCharacters[i];
			if (!Char) continue;

			float dist = distance(Char->m_Pos, mario->m_Pos);
			if (dist < 48 && sm64_mario_attack(mario->ID(), Char->m_Pos.x/mario->Scale(), Char->m_Pos.y/-mario->Scale(), 0, 0))
			{
				// tee attacked
				if (mario->state.action == ACT_GROUND_POUND)
				{
					sm64_set_mario_action(mario->ID(), ACT_TRIPLE_JUMP);
					sm64_play_sound_global(SOUND_ACTION_HIT);

					for (int j=-2; j<=2; j++)
					{
						float angle = (90 + (20 * j)) / 180.f * pi;
						m_pClient->m_Effects.DamageIndicator(Char->m_Pos, vec2(cos(angle), -sin(angle)));
					}
				}
				else
					m_pClient->m_Effects.DamageIndicator(Char->m_Pos, vec2(0, -1));
			}
		}
	}

	mario->Tick(Client()->RenderFrameTime());

	if (mario->state.flags & MARIO_METAL_CAP)
	{
		for (int i=0; i<mario->geometry.numTrianglesUsed; i++)
		{
			/*
			mario->geometry.uv[i*6+0] = 0;
			mario->geometry.uv[i*6+1] = 0.09090909090909091f;
			mario->geometry.uv[i*6+2] = 0.09090909090909091f;
			mario->geometry.uv[i*6+3] = 0;
			*/
			for (int j=0; j<9; j++) mario->geometry.color[i*9+j] = 0; // needs fix
		}
	}
	else if (g_Config.m_MarioCustomColors)
	{
		ColorRGBA bodyColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClPlayerColorBody).UnclampLighting());
		ColorRGBA feetColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClPlayerColorFeet).UnclampLighting());
		for (int i=0; i<mario->geometry.numTrianglesUsed; i++)
		{
			uint8_t r = mario->geometry.color[i*9+0]*255;
			uint8_t g = mario->geometry.color[i*9+1]*255;
			uint8_t b = mario->geometry.color[i*9+2]*255;
			float r2 = 0, g2 = 0, b2 = 0;
			bool changed = false;

			if (r == 0 && g == 0 && b == 255) // overalls / pants
			{
				changed = true;
				r2 = bodyColor.r / 2;
				g2 = bodyColor.g / 2;
				b2 = bodyColor.b / 2;
			}
			else if (r == 255 && g == 0 && b == 0) // shirt / hat
			{
				changed = true;
				r2 = bodyColor.r;
				g2 = bodyColor.g;
				b2 = bodyColor.b;
			}
			else if (r == 114 && g == 28 && b == 14) // shoes
			{
				changed = true;
				r2 = feetColor.r;
				g2 = feetColor.g;
				b2 = feetColor.b;
			}

			if (changed)
			{
				for (int j=0; j<3; j++)
				{
					mario->geometry.color[i*9 + j*3 + 0] = r2;
					mario->geometry.color[i*9 + j*3 + 1] = g2;
					mario->geometry.color[i*9 + j*3 + 2] = b2;
				}
			}
		}
	}

	CMarioMesh *mesh = &m_MarioMeshes[ID];
	if (mario->geometry.numTrianglesUsed)
		Graphics()->updateAndRenderMario(mesh, &mario->geometry, mario->state.flags, &m_MarioShaderHandle, &m_MarioTexHandle, m_MarioIndices);
}

void CMarios::OnRender()
{
	for (int i=0; i<MAX_CLIENTS; i++)
	{
		CMarioCore *mario = m_pClient->m_GameWorld.m_Core.m_apMarios[i];
		if (!mario) continue;
		TickAndRenderMario(i);
	}
}

void CMarios::ConMario(IConsole::IResult *pResult, void *pUserData)
{
	CMarios *pSelf = (CMarios*)pUserData;
	int ID = pSelf->m_pClient->m_Snap.m_LocalClientID;
	if (!pSelf->m_pClient->m_aClients[ID].m_Active || pSelf->m_pClient->m_Snap.m_SpecInfo.m_Active)
	{
		dbg_msg("libsm64", "You must be in-game to spawn Mario");
		return;
	}

	if (!pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID])
	{
		CMarioCore *mario = new CMarioCore;
		mario->Init(&pSelf->m_pClient->m_GameWorld.m_Core, pSelf->Collision(), pSelf->m_pClient->m_LocalCharacterPos, g_Config.m_MarioScale/100.f);
		if (!mario->Spawned())
		{
			dbg_msg("libsm64", "Failed to spawn Mario at position %.2f %.2f", pSelf->m_pClient->m_LocalCharacterPos.x/32, pSelf->m_pClient->m_LocalCharacterPos.y/32);
			delete mario;
			return;
		}

		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "libsm64", "Spawned Mario");

		pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID] = mario;

		// create mario vertex
		CMarioMesh *mesh = &pSelf->m_MarioMeshes[ID];
		pSelf->Graphics()->initMario(mesh, &mario->geometry);
	}
	else
	{
		delete pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID];
		pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID] = 0;

		CMarioMesh *mesh = &pSelf->m_MarioMeshes[ID];
		pSelf->Graphics()->destroyMario(mesh);

		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "libsm64", "Deleted Mario");
	}
}

void CMarios::ConMarioKill(IConsole::IResult *pResult, void *pUserData)
{
	CMarios *pSelf = (CMarios*)pUserData;
	int ID = pSelf->m_pClient->m_Snap.m_LocalClientID;
	CMarioCore *mario = pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID];

	if (!mario)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "libsm64", "Mario is not spawned");
		return;
	}

	mario->state.health = 0xff;
	sm64_mario_kill(mario->ID());
}

void CMarios::ConMarioMusic(IConsole::IResult *pResult, void *pUserData)
{
	CMarios *pSelf = (CMarios*)pUserData;
	int ID = pResult->GetInteger(0);

	if (ID < 0 || ID >= SEQ_COUNT)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "libsm64", "Invalid music ID. Choose between ID 0 and 34");
		return;
	}

	for (int i=0; i<SEQ_COUNT; i++)
		sm64_stop_background_music(i);

	if (ID != 0)
		sm64_play_music(0, SEQUENCE_ARGS(0, ID), 0);
}

void CMarios::ConMarioCap(IConsole::IResult *pResult, void *pUserData)
{
	CMarios *pSelf = (CMarios*)pUserData;
	int ID = pSelf->m_pClient->m_Snap.m_LocalClientID;
	CMarioCore *mario = pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID];

	if (!mario)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "libsm64", "Mario is not spawned");
		return;
	}

	struct {
		const char *name;
		uint32_t capFlag;
	} caps[] = {
		{"off", 0},
		{"on", MARIO_NORMAL_CAP},
		{"wing", MARIO_WING_CAP},
		{"metal", MARIO_METAL_CAP}
	};

	for (int i=0; i<4; i++)
	{
		if (!str_comp_nocase(pResult->GetString(0), caps[i].name))
		{
			sm64_set_mario_state(mario->ID(), 0);
			sm64_mario_interact_cap(mario->ID(), caps[i].capFlag, 65535, 0);
			return;
		}
	}

	char buf[128];
	str_format(buf, sizeof(buf), "Invalid cap name \"%s\"", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "libsm64", buf);
}
