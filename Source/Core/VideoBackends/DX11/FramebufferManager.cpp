// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/HW/Memmap.h"
#include "VideoBackends/DX11/D3DBase.h"
#include "VideoBackends/DX11/D3DUtil.h"
#include "VideoBackends/DX11/FramebufferManager.h"
#include "VideoBackends/DX11/GeometryShaderCache.h"
#include "VideoBackends/DX11/PixelShaderCache.h"
#include "VideoBackends/DX11/PostProcessing.h"
#include "VideoBackends/DX11/Render.h"
#include "VideoBackends/DX11/VertexShaderCache.h"
#include "VideoBackends/DX11/XFBEncoder.h"
#include "VideoCommon/VideoConfig.h"

namespace DX11 {

static XFBEncoder s_xfbEncoder;

FramebufferManager::Efb FramebufferManager::m_efb;
u32 FramebufferManager::m_target_width;
u32 FramebufferManager::m_target_height;

D3DTexture2D* &FramebufferManager::GetEFBColorTexture() { return m_efb.color_tex; }
ID3D11Texture2D* &FramebufferManager::GetEFBColorStagingBuffer() { return m_efb.color_staging_buf; }

D3DTexture2D* &FramebufferManager::GetEFBDepthTexture() { return m_efb.depth_tex; }
D3DTexture2D* &FramebufferManager::GetEFBDepthReadTexture() { return m_efb.depth_read_texture; }
ID3D11Texture2D* &FramebufferManager::GetEFBDepthStagingBuffer() { return m_efb.depth_staging_buf; }

D3DTexture2D* &FramebufferManager::GetResolvedEFBColorTexture()
{
	if (g_ActiveConfig.iMultisampleMode)
	{
		for (int i = 0; i < m_efb.slices; i++)
			D3D::context->ResolveSubresource(m_efb.resolved_color_tex->GetTex(), D3D11CalcSubresource(0, i, 1), m_efb.color_tex->GetTex(), D3D11CalcSubresource(0, i, 1), DXGI_FORMAT_R8G8B8A8_UNORM);
		return m_efb.resolved_color_tex;
	}
	else
		return m_efb.color_tex;
}

D3DTexture2D* &FramebufferManager::GetResolvedEFBDepthTexture()
{
	if (g_ActiveConfig.iMultisampleMode)
	{
		for (int i = 0; i < m_efb.slices; i++)
			D3D::context->ResolveSubresource(m_efb.resolved_depth_tex->GetTex(), D3D11CalcSubresource(0, i, 1), m_efb.depth_tex->GetTex(), D3D11CalcSubresource(0, i, 1), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
		return m_efb.resolved_depth_tex;
	}
	else
		return m_efb.depth_tex;
}

FramebufferManager::FramebufferManager()
{
	m_target_width = Renderer::GetTargetWidth();
	m_target_height = Renderer::GetTargetHeight();
	if (m_target_height < 1)
	{
		m_target_height = 1;
	}
	if (m_target_width < 1)
	{
		m_target_width = 1;
	}
	DXGI_SAMPLE_DESC sample_desc = D3D::GetAAMode(g_ActiveConfig.iMultisampleMode);

	ID3D11Texture2D* buf;
	D3D11_TEXTURE2D_DESC texdesc;
	HRESULT hr;

	m_EFBLayers = m_efb.slices = (g_ActiveConfig.iStereoMode > 0) ? 2 : 1;

	// EFB color texture - primary render target
	texdesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UNORM, m_target_width, m_target_height, m_efb.slices, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, sample_desc.Count, sample_desc.Quality);
	hr = D3D::device->CreateTexture2D(&texdesc, nullptr, &buf);
	CHECK(hr == S_OK, "create EFB color texture (size: %dx%d; hr=%#x)", m_target_width, m_target_height, hr);
	m_efb.color_tex = new D3DTexture2D(buf, (D3D11_BIND_FLAG)(D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET), DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM, (sample_desc.Count > 1));
	CHECK(m_efb.color_tex != nullptr, "create EFB color texture (size: %dx%d)", m_target_width, m_target_height);
	SAFE_RELEASE(buf);
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.color_tex->GetTex(), "EFB color texture");
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.color_tex->GetSRV(), "EFB color texture shader resource view");
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.color_tex->GetRTV(), "EFB color texture render target view");

	// Temporary EFB color texture - used in ReinterpretPixelData
	texdesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UNORM, m_target_width, m_target_height, m_efb.slices, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, sample_desc.Count, sample_desc.Quality);
	hr = D3D::device->CreateTexture2D(&texdesc, nullptr, &buf);
	CHECK(hr == S_OK, "create EFB color temp texture (size: %dx%d; hr=%#x)", m_target_width, m_target_height, hr);
	m_efb.color_temp_tex = new D3DTexture2D(buf, (D3D11_BIND_FLAG)(D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET), DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM, (sample_desc.Count > 1));
	CHECK(m_efb.color_temp_tex != nullptr, "create EFB color temp texture (size: %dx%d)", m_target_width, m_target_height);
	SAFE_RELEASE(buf);
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.color_temp_tex->GetTex(), "EFB color temp texture");
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.color_temp_tex->GetSRV(), "EFB color temp texture shader resource view");
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.color_temp_tex->GetRTV(), "EFB color temp texture render target view");

	// AccessEFB - Sysmem buffer used to retrieve the pixel data from color_tex
	texdesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, m_efb.slices, 1, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
	hr = D3D::device->CreateTexture2D(&texdesc, nullptr, &m_efb.color_staging_buf);
	CHECK(hr == S_OK, "create EFB color staging buffer (hr=%#x)", hr);
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.color_staging_buf, "EFB color staging texture (used for Renderer::AccessEFB)");

	// EFB depth buffer - primary depth buffer
	texdesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R32_TYPELESS, m_target_width, m_target_height, m_efb.slices, 1, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, sample_desc.Count, sample_desc.Quality);
	hr = D3D::device->CreateTexture2D(&texdesc, nullptr, &buf);
	CHECK(hr == S_OK, "create EFB depth texture (size: %dx%d; hr=%#x)", m_target_width, m_target_height, hr);
	m_efb.depth_tex = new D3DTexture2D(buf, (D3D11_BIND_FLAG)(D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE), DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_UNKNOWN, (sample_desc.Count > 1));
	SAFE_RELEASE(buf);
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.depth_tex->GetTex(), "EFB depth texture");
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.depth_tex->GetDSV(), "EFB depth texture depth stencil view");
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.depth_tex->GetSRV(), "EFB depth texture shader resource view");

	// Render buffer for AccessEFB (depth data)
	texdesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R32_FLOAT, 1, 1, m_efb.slices, 1, D3D11_BIND_RENDER_TARGET);
	hr = D3D::device->CreateTexture2D(&texdesc, nullptr, &buf);
	CHECK(hr == S_OK, "create EFB depth read texture (hr=%#x)", hr);
	m_efb.depth_read_texture = new D3DTexture2D(buf, D3D11_BIND_RENDER_TARGET);
	SAFE_RELEASE(buf);
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.depth_read_texture->GetTex(), "EFB depth read texture (used in Renderer::AccessEFB)");
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.depth_read_texture->GetRTV(), "EFB depth read texture render target view (used in Renderer::AccessEFB)");

	// AccessEFB - Sysmem buffer used to retrieve the pixel data from depth_read_texture
	texdesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R32_FLOAT, 1, 1, m_efb.slices, 1, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
	hr = D3D::device->CreateTexture2D(&texdesc, nullptr, &m_efb.depth_staging_buf);
	CHECK(hr == S_OK, "create EFB depth staging buffer (hr=%#x)", hr);
	D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.depth_staging_buf, "EFB depth staging texture (used for Renderer::AccessEFB)");

	if (g_ActiveConfig.iMultisampleMode)
	{
		// Framebuffer resolve textures (color+depth)
		texdesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UNORM, m_target_width, m_target_height, m_efb.slices, 1, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 1);
		hr = D3D::device->CreateTexture2D(&texdesc, nullptr, &buf);
		m_efb.resolved_color_tex = new D3DTexture2D(buf, D3D11_BIND_SHADER_RESOURCE, DXGI_FORMAT_R8G8B8A8_UNORM);
		CHECK(m_efb.resolved_color_tex != nullptr, "create EFB color resolve texture (size: %dx%d)", m_target_width, m_target_height);
		SAFE_RELEASE(buf);
		D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.resolved_color_tex->GetTex(), "EFB color resolve texture");
		D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.resolved_color_tex->GetSRV(), "EFB color resolve texture shader resource view");

		texdesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R24G8_TYPELESS, m_target_width, m_target_height, m_efb.slices, 1, D3D11_BIND_SHADER_RESOURCE);
		hr = D3D::device->CreateTexture2D(&texdesc, nullptr, &buf);
		CHECK(hr == S_OK, "create EFB depth resolve texture (size: %dx%d; hr=%#x)", m_target_width, m_target_height, hr);
		m_efb.resolved_depth_tex = new D3DTexture2D(buf, D3D11_BIND_SHADER_RESOURCE, DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
		SAFE_RELEASE(buf);
		D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.resolved_depth_tex->GetTex(), "EFB depth resolve texture");
		D3D::SetDebugObjectName((ID3D11DeviceChild*)m_efb.resolved_depth_tex->GetSRV(), "EFB depth resolve texture shader resource view");
	}
	else
	{
		m_efb.resolved_color_tex = nullptr;
		m_efb.resolved_depth_tex = nullptr;
	}

	s_xfbEncoder.Init();
}

FramebufferManager::~FramebufferManager()
{
	s_xfbEncoder.Shutdown();

	SAFE_RELEASE(m_efb.color_tex);
	SAFE_RELEASE(m_efb.color_temp_tex);
	SAFE_RELEASE(m_efb.color_staging_buf);
	SAFE_RELEASE(m_efb.resolved_color_tex);
	SAFE_RELEASE(m_efb.depth_tex);
	SAFE_RELEASE(m_efb.depth_staging_buf);
	SAFE_RELEASE(m_efb.depth_read_texture);
	SAFE_RELEASE(m_efb.resolved_depth_tex);
}

void FramebufferManager::CopyToRealXFB(u32 xfbAddr, u32 fbStride, u32 fbHeight, const EFBRectangle& sourceRc, float Gamma)
{
	u8* dst = Memory::GetPointer(xfbAddr);
	s_xfbEncoder.Encode(dst, fbStride / 2, fbHeight, sourceRc, Gamma);
}

XFBSourceBase* FramebufferManager::CreateXFBSource(u32 target_width, u32 target_height, u32 layers)
{
	return new XFBSource(D3DTexture2D::Create(target_width, target_height,
		(D3D11_BIND_FLAG)(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE),
		D3D11_USAGE_DEFAULT, DXGI_FORMAT_R8G8B8A8_UNORM, 1, layers), layers);
}

void FramebufferManager::GetTargetSize(u32 *width, u32 *height)
{
	*width = m_target_width;
	*height = m_target_height;
}

void XFBSource::DecodeToTexture(u32 xfbAddr, u32 fbWidth, u32 fbHeight)
{
	// DX11's XFB decoder does not use this function.
	// YUYV data is decoded in Render::Swap.
}

void XFBSource::CopyEFB(float Gamma)
{
	bool depth_copy_required = g_renderer->GetPostProcessor()->GetConfig()->IsDepthInputRequired();
	if (depth_copy_required && !depthtex)
	{
		depthtex = D3DTexture2D::Create(texWidth, texHeight,
			(D3D11_BIND_FLAG)(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE),
			D3D11_USAGE_DEFAULT, DXGI_FORMAT_R32_FLOAT, 1, m_slices);
	}
	if (D3D::GetAAMode(g_ActiveConfig.iMultisampleMode).Count > 1)
	{
		for (UINT i = 0; i < (UINT)m_slices; i++)
		{
			UINT resource_idx = D3D11CalcSubresource(0, i, 1);
			D3D::context->ResolveSubresource(
				tex->GetTex(),
				resource_idx,
				FramebufferManager::GetEFBColorTexture()->GetTex(),
				resource_idx, DXGI_FORMAT_R8G8B8A8_UNORM);
		}
		if (depth_copy_required)
		{
			g_renderer->ResetAPIState();
			const D3D11_VIEWPORT vp = CD3D11_VIEWPORT(0.f, 0.f, (float)texWidth, (float)texHeight);
			D3D::context->OMSetRenderTargets(1, &depthtex->GetRTV(), nullptr);
			D3D::context->RSSetViewports(1, &vp);
			D3D::SetPointCopySampler();
			D3D::drawShadedTexQuad(FramebufferManager::GetEFBDepthTexture()->GetSRV(), nullptr,
			Renderer::GetTargetWidth(), Renderer::GetTargetHeight(),
			PixelShaderCache::GetColorCopyProgram(true), VertexShaderCache::GetSimpleVertexShader(),
			VertexShaderCache::GetSimpleInputLayout(), GeometryShaderCache::GetCopyGeometryShader(), 1.0, 0, texWidth, texHeight);
			D3D::context->OMSetRenderTargets(1,
				&FramebufferManager::GetEFBColorTexture()->GetRTV(),
				FramebufferManager::GetEFBDepthTexture()->GetDSV());
			g_renderer->RestoreAPIState();
		}
	}
	else
	{
		D3D::context->CopyResource(tex->GetTex(), FramebufferManager::GetEFBColorTexture()->GetTex());
		if (depth_copy_required)
		{
			D3D::context->CopyResource(depthtex->GetTex(), FramebufferManager::GetEFBDepthTexture()->GetTex());
		}
	}
}

}  // namespace DX11
