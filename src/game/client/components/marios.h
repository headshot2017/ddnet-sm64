#ifndef GAME_CLIENT_COMPONENTS_MARIOS_H
#define GAME_CLIENT_COMPONENTS_MARIOS_H
#include <game/client/component.h>

#include <engine/client.h>
#include <engine/console.h>

#include <game/client/render.h>
#include <game/generated/protocol.h>

class CMarios : public CComponent
{
public:
	virtual int Sizeof() const override { return sizeof(*this); }
	virtual void OnInit() override;
	virtual void OnConsoleInit() override;
	virtual void OnRender() override;

private:
	static void ConMario(IConsole::IResult *pResult, void *pUserData);

	int m_MarioBuffers[MAX_CLIENTS];
	uint8_t *m_MarioTexture;
	IGraphics::CTextureHandle m_MarioTexHandle;
};

#endif
