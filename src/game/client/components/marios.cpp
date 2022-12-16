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

extern "C" {
	#include <decomp/include/audio_defines.h>
	#include <decomp/include/surface_terrains.h>
}

using namespace std::chrono_literals;

static const char *MARIO_SHADER =
"\n uniform mat4 view;"
"\n uniform mat4 projection;"
"\n uniform sampler2D marioTex;"
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
"\n         vec4 texColor = texture2D( marioTex, v_uv );"
"\n         vec3 mainColor = mix( v_color, texColor.rgb, texColor.a ); // v_uv.x >= 0. ? texColor.a : 0. );"
"\n         color = vec4( mainColor * light, 1 );"
"\n     }"
"\n "
"\n #endif";



// idea: move all these to their own engine/graphics functions

void CMarios::OnConsoleInit()
{
	Console()->Register("mario", "", CFGFLAG_CLIENT, ConMario, this, "Toggle Mario");
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

void CMarios::OnRender()
{
	int ID = m_pClient->m_Snap.m_LocalClientID;
	CMarioCore *mario = m_pClient->m_GameWorld.m_Core.m_apMarios[ID];

	if (!m_pClient->m_aClients[ID].m_Active || m_pClient->m_Snap.m_SpecInfo.m_Active || !mario)
	{
		return;
	}

	//CNetObj_PlayerInput *input = (CNetObj_PlayerInput *)Client()->GetInput(Client()->GameTick(g_Config.m_ClDummy), g_Config.m_ClDummy);
	CNetObj_PlayerInput *input = &GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
	mario->input.stickX = -input->m_Direction;
	mario->input.stickY = 0;
	mario->input.buttonA = input->m_Jump;
	mario->input.buttonB = input->m_Fire & 1;
	mario->input.buttonZ = input->m_Hook;

	mario->Tick(Client()->RenderFrameTime());

	CMarioMesh *mesh = &m_MarioMeshes[ID];
	Graphics()->updateAndRenderMario(mesh, &mario->geometry, &m_MarioShaderHandle, &m_MarioTexHandle, m_MarioIndices);
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

		dbg_msg("libsm64", "Created Mario");

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

		dbg_msg("libsm64", "Deleted Mario");
	}
}
