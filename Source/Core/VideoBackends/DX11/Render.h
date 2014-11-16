
#ifndef _RENDER_H_
#define _RENDER_H_

#include "VideoCommon/RenderBase.h"

namespace DX11
{

class Renderer : public ::Renderer
{
public:
	Renderer(void *&window_handle);
	~Renderer();

	void SetColorMask();
	void SetBlendMode(bool forceUpdate);
	void SetScissorRect(const TargetRectangle& rc);
	void SetGenerationMode();
	void SetDepthMode();
	void SetLogicOpMode();
	void SetDitherMode();
	void SetLineWidth();
	void SetSamplerState(int stage,int texindex);
	void SetInterlacingMode();

	// TODO: Fix confusing names (see ResetAPIState and RestoreAPIState)
	void ApplyState(bool bUseDstAlpha);
	void RestoreState();

	void ApplyCullDisable();
	void RestoreCull();

	void RenderText(const std::string& str, int left, int top, u32 color);

	u32 AccessEFB(EFBAccessType type, u32 x, u32 y, u32 poke_data);

	void ResetAPIState();
	void RestoreAPIState();

	TargetRectangle ConvertEFBRectangle(const EFBRectangle& rc);

	void SwapImpl(u32 xfbAddr, u32 fbWidth, u32 fbStride, u32 fbHeight, const EFBRectangle& rc, float Gamma = 1.0f);

	void ClearScreen(const EFBRectangle& rc, bool colorEnable, bool alphaEnable, bool zEnable, u32 color, u32 z);

	void ReinterpretPixelData(unsigned int convtype);

	void UpdateViewport(Matrix44& vpCorrection);

	bool SaveScreenshot(const std::string &filename, const TargetRectangle &rc);

	static bool CheckForResize();

	int GetMaxTextureSize() override;
};

}

#endif
