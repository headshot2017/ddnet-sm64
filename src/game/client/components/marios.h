#ifndef GAME_CLIENT_COMPONENTS_MARIOS_H
#define GAME_CLIENT_COMPONENTS_MARIOS_H
#include <game/client/component.h>

#include <engine/client.h>
#include <engine/console.h>

#include <game/client/render.h>
#include <game/generated/protocol.h>

extern "C" {
	#include <libsm64.h>
}

class CMarios : public CComponent
{
public:
	virtual int Sizeof() const override { return sizeof(*this); }
	virtual void OnInit() override;
	virtual void OnConsoleInit() override;
	virtual void OnMapLoad() override;
	virtual void OnRender() override;

private:
	static void ConMario(IConsole::IResult *pResult, void *pUserData);

	CMarioMesh m_MarioMeshes[MAX_CLIENTS];
	uint8_t *m_MarioTexture;
	uint16_t m_MarioIndices[SM64_GEO_MAX_TRIANGLES * 3];
	uint32_t m_MarioTexHandle;
	uint32_t m_MarioShaderHandle;
};

#endif
