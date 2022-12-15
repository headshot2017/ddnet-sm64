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
}

using namespace std::chrono_literals;

typedef float VEC2[2];
typedef float VEC3[3];

static const char *MARIO_SHADER =
"\n uniform mat3 viewproj;"
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
"\n         v_light = /*transpose( mat3( view )) * */ normalize( vec3( 1 ));"
"\n         v_uv = uv;"
"\n "
"\n         gl_Position = viewproj * vec4( position, 1. );"
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

GLuint shader_compile( const char *shaderContents, size_t shaderContentsLength, GLenum shaderType )
{
    const GLchar *shaderDefine = shaderType == GL_VERTEX_SHADER 
        ? "\n#version 130\n#define VERTEX  \n#define v2f out\n" 
        : "\n#version 130\n#define FRAGMENT\n#define v2f in \n";

    const GLchar *shaderStrings[2] = { shaderDefine, shaderContents };
    GLint shaderStringLengths[2] = { (GLint)strlen( shaderDefine ), (GLint)shaderContentsLength };

    GLuint shader = glCreateShader( shaderType );
    glShaderSource( shader, 2, shaderStrings, shaderStringLengths );
    glCompileShader( shader );

    GLint isCompiled;
    glGetShaderiv( shader, GL_COMPILE_STATUS, &isCompiled );
    if( isCompiled == GL_FALSE ) 
    {
        GLint maxLength;
        glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &maxLength );
        char *log = (char*)malloc( maxLength );
        glGetShaderInfoLog( shader, maxLength, &maxLength, log );

        dbg_msg("libsm64", "%s shader compilation failure: %s", (shaderType == GL_VERTEX_SHADER) ? "Vertex" : "Fragment", log);
    }
	else
		dbg_msg("libsm64", "%s shader compiled", (shaderType == GL_VERTEX_SHADER) ? "Vertex" : "Fragment");

    return shader;
}



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

			// initialize shader
			GLuint vert = shader_compile(MARIO_SHADER, strlen(MARIO_SHADER), GL_VERTEX_SHADER);
			GLuint frag = shader_compile(MARIO_SHADER, strlen(MARIO_SHADER), GL_FRAGMENT_SHADER);

			m_MarioShaderHandle = glCreateProgram();
			glAttachShader(m_MarioShaderHandle, vert);
			glAttachShader(m_MarioShaderHandle, frag);

			const GLchar *attribs[] = {"position", "normal", "color", "uv"};
			for (int i=6; i<10; i++) glBindAttribLocation(m_MarioShaderHandle, i, attribs[i-6]);

			glLinkProgram(m_MarioShaderHandle);
			glDetachShader(m_MarioShaderHandle, vert);
			glDetachShader(m_MarioShaderHandle, frag);

			for(int i=0; i<3*SM64_GEO_MAX_TRIANGLES; i++) m_MarioIndices[i] = i;
			m_LoadedOnce = false;
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
	CMarioMesh *mesh = &m_MarioMeshes[ID];
	glBindBuffer( GL_ARRAY_BUFFER, mesh->position_buffer );
    glBufferData( GL_ARRAY_BUFFER, sizeof( VEC3 ) * 3 * SM64_GEO_MAX_TRIANGLES, mario->m_GeometryPos, GL_DYNAMIC_DRAW );
    glBindBuffer( GL_ARRAY_BUFFER, mesh->normal_buffer );
    glBufferData( GL_ARRAY_BUFFER, sizeof( VEC3 ) * 3 * SM64_GEO_MAX_TRIANGLES, mario->geometry.normal, GL_DYNAMIC_DRAW );
    glBindBuffer( GL_ARRAY_BUFFER, mesh->color_buffer );
    glBufferData( GL_ARRAY_BUFFER, sizeof( VEC3 ) * 3 * SM64_GEO_MAX_TRIANGLES, mario->geometry.color, GL_DYNAMIC_DRAW );
    glBindBuffer( GL_ARRAY_BUFFER, mesh->uv_buffer );
    glBufferData( GL_ARRAY_BUFFER, sizeof( VEC2 ) * 3 * SM64_GEO_MAX_TRIANGLES, mario->geometry.uv, GL_DYNAMIC_DRAW );

	const float sx = 2.0f / (float)Graphics()->ScreenWidth();
	const float sy = -2.0f / (float)Graphics()->ScreenHeight();

	const float tx = -1.0f;
	const float ty = 1.0f;

	const float mvp_matrix[] = {
		sx, 0, tx,
		0, sy, ty,
		0, 0, 1
	};

	glUseProgram(m_MarioShaderHandle);
	glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_MarioTexHandle);
    glBindVertexArray(mesh->vao);
	glUniformMatrix3fv(glGetUniformLocation(m_MarioShaderHandle, "viewproj"), 1, GL_FALSE, (GLfloat*)mvp_matrix);
	glUniform1i(glGetUniformLocation(m_MarioShaderHandle, "marioTex"), 2);
    glDrawElements(GL_TRIANGLES, mario->geometry.numTrianglesUsed*3, GL_UNSIGNED_SHORT, m_MarioIndices);
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

		if (!pSelf->m_LoadedOnce)
		{
			pSelf->m_LoadedOnce = true;

			// initialize texture
			//m_MarioTexHandle = Graphics()->LoadTextureRaw(SM64_TEXTURE_WIDTH, SM64_TEXTURE_HEIGHT, CImageInfo::FORMAT_RGBA, m_MarioTexture, CImageInfo::FORMAT_RGBA, 0);
			glGenTextures(1, &pSelf->m_MarioTexHandle);
			glBindTexture(GL_TEXTURE_2D, pSelf->m_MarioTexHandle);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SM64_TEXTURE_WIDTH, SM64_TEXTURE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, pSelf->m_MarioTexture);

			dbg_msg("libsm64", "%d %d", pSelf->m_MarioTexHandle, pSelf->m_MarioShaderHandle);
		}

		CMarioMesh *mesh = &pSelf->m_MarioMeshes[ID];
		glGenVertexArrays( 1, &mesh->vao );
		glBindVertexArray( mesh->vao );

		#define X( loc, buff, arr, type ) do { \
			glGenBuffers( 1, &buff ); \
			glBindBuffer( GL_ARRAY_BUFFER, buff ); \
			glBufferData( GL_ARRAY_BUFFER, sizeof( type ) * 3 * SM64_GEO_MAX_TRIANGLES, arr, GL_DYNAMIC_DRAW ); \
			glEnableVertexAttribArray( loc ); \
			glVertexAttribPointer( loc, sizeof( type ) / sizeof( float ), GL_FLOAT, GL_FALSE, sizeof( type ), NULL ); \
		} while( 0 )

			X( 6, mesh->position_buffer, mario->m_GeometryPos,     VEC3 );
			X( 7, mesh->normal_buffer,   mario->geometry.normal,   VEC3 );
			X( 8, mesh->color_buffer,    mario->geometry.color,    VEC3 );
			X( 9, mesh->uv_buffer,       mario->geometry.uv,       VEC2 );

		#undef X

		dbg_msg("libsm64", "%d %d %d %d %d", mesh->position_buffer, mesh->normal_buffer, mesh->color_buffer, mesh->uv_buffer, mesh->vao);

		dbg_msg("libsm64", "create mario");
	}
	else
	{
		delete pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID];
		pSelf->m_pClient->m_GameWorld.m_Core.m_apMarios[ID] = 0;

		CMarioMesh *mesh = &pSelf->m_MarioMeshes[ID];

		glDeleteVertexArrays(1, &mesh->vao);
		glDeleteBuffers(1, &mesh->position_buffer);
		glDeleteBuffers(1, &mesh->normal_buffer);
		glDeleteBuffers(1, &mesh->color_buffer);
		glDeleteBuffers(1, &mesh->uv_buffer);

		dbg_msg("libsm64", "delete mario");
	}
}
