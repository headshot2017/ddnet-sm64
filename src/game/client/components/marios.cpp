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
	#include <libsm64.h>
	#include <decomp/include/audio_defines.h>
}

using namespace std::chrono_literals;


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
			sm64_global_terminate();
			sm64_global_init(romBuffer, m_MarioTexture, [](const char *msg) {dbg_msg("libsm64", "%s", msg);});
			dbg_msg("libsm64", "Super Mario 64 US ROM loaded!");
			sm64_play_sound_global(SOUND_MENU_STAR_SOUND);
			free(romBuffer);

			//virtual CTextureHandle LoadTextureRaw(int Width, int Height, int Format, const void *pData, int StoreFormat, int Flags, const char *pTexName = nullptr) = 0;
			m_MarioTexHandle = Graphics()->LoadTextureRaw(SM64_TEXTURE_WIDTH, SM64_TEXTURE_HEIGHT, CImageInfo::FORMAT_RGBA, m_MarioTexture, CImageInfo::FORMAT_RGBA, 0);
			dbg_msg("libsm64", (m_MarioTexHandle.IsValid()) ? "Mario texture loaded" : "Failed to load Mario texture!");
		}
	}
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
	//Graphics()->RecreateBufferObject(m_MarioBuffers[ID], SM64_GEO_MAX_TRIANGLES, mario->geometry.position, 0);
}

void CMarios::ConMario(IConsole::IResult *pResult, void *pUserData)
{
	CMarios *pSelf = (CMarios*)pUserData;
	int ID = pSelf->m_pClient->m_Snap.m_LocalClientID;
	if (!pSelf->m_pClient->m_aClients[ID].m_Active || pSelf->m_pClient->m_Snap.m_SpecInfo.m_Active)
	{
		dbg_msg("libsm64", "not active %d", ID);
		return;
	}

	if (!pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID])
	{
		CMarioCore *mario = new CMarioCore;
		mario->Init(&pSelf->m_pClient->m_GameWorld.m_Core, pSelf->Collision(), pSelf->m_pClient->m_LocalCharacterPos, g_Config.m_MarioScale/100.f);
		pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID] = mario;

		// create mario vertex
		//virtual int CreateBufferObject(size_t UploadDataSize, void *pUploadData, int CreateFlags, bool IsMovedPointer = false) = 0;
		//pSelf->m_MarioBuffers[ID] = pSelf->Graphics()->CreateBufferObject(SM64_GEO_MAX_TRIANGLES, mario->geometry.position, 0);
		dbg_msg("libsm64", "create mario");
	}
	else
	{
		delete pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID];
		pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID] = 0;
		dbg_msg("libsm64", "delete mario");
	}
}
