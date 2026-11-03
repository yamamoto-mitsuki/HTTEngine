#include "MyEngine/Graphics/Pipeline/RenderStates.h"

#include <algorithm>

//=============================================================================
// BlendMode → D3D12_BLEND_DESC
//=============================================================================
D3D12_BLEND_DESC RenderStates::MakeBlendDesc(BlendMode mode) {
	// ブレンド設定
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	// BlendModeによって分岐
	switch (mode) {
	// --- ブレンド無し ---
	case BlendMode::None:
		blendDesc.RenderTarget[0].BlendEnable = FALSE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		break;
	// --- 通常aブレンド（デフォルト） ---
	case BlendMode::Normal:
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		break;
	// --- 加算 ---
	case BlendMode::Add:
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		break;
	// --- 減算 ---
	case BlendMode::Subtract:
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_SUBTRACT;
		break;
	// --- 乗算 ---
	case BlendMode::Multiply:
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		break;
	// --- スクリーン合成 ---
	case BlendMode::Screen:
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		break;
	}

	return blendDesc;
}


//=============================================================================
// RasterizerType → D3D12_RASTERIZER_DESC
//=============================================================================
D3D12_RASTERIZER_DESC RenderStates::MakeRasterizerDesc(RasterizerType type) {
	D3D12_RASTERIZER_DESC desc{};
	// --- 全タイプ共通の既定値 ---
	desc.FillMode = D3D12_FILL_MODE_SOLID;
	desc.CullMode = D3D12_CULL_MODE_BACK;
	desc.FrontCounterClockwise = FALSE;
	desc.DepthClipEnable = TRUE;

	// --- タイプで変わる所だけ上書き ---
	switch (type) {
	case RasterizerType::SolidBack:
		// 既定のまま（背面カリング＋塗りつぶし）
		break;

	case RasterizerType::SolidNone:
		desc.CullMode = D3D12_CULL_MODE_NONE; // 両面描画
		break;

	case RasterizerType::Wireframe:
		desc.FillMode = D3D12_FILL_MODE_WIREFRAME;
		desc.CullMode = D3D12_CULL_MODE_NONE;
		break;

	case RasterizerType::SolidFront:
		// シャドウマップ生成パス用：前面カリング＋深度バイアスで影のアクネ/ピーターパンを抑制
		desc.CullMode = D3D12_CULL_MODE_FRONT;
		desc.DepthBias = 10000;           // 定数バイアス（深度フォーマット依存。要調整）
		desc.SlopeScaledDepthBias = 1.5f; // 傾斜バイアス（要調整）
		desc.DepthBiasClamp = 0.0f;
		break;
	}
	return desc;
}


//=============================================================================
// DepthMode・StencilMode → D3D12_DEPTH_STENCIL_DESC
//=============================================================================
D3D12_DEPTH_STENCIL_DESC RenderStates::MakeDepthStencilDesc(DepthMode mode, StencilMode stencil) {
	D3D12_DEPTH_STENCIL_DESC desc{};
	desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // 既定（手前を採用）

	switch (mode) {
	case DepthMode::TestWrite: // 不透明3D
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		break;

	case DepthMode::TestNoWrite: // 半透明3D（不透明の深度に対してテストはするが書かない）
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		break;

	case DepthMode::Disable: // 2D / UI / フルスクリーン
		desc.DepthEnable = FALSE;
		desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		break;
	}

	// --- ステンシル（UI のマスク）。値が参照値と同じ所だけ通し、通った所を増やす・減らす・そのまま ---
	if (stencil == StencilMode::None) {
		return desc;
	}
	D3D12_DEPTH_STENCILOP_DESC face{};
	face.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	face.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	face.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	switch (stencil) {
	case StencilMode::Increment:
		face.StencilPassOp = D3D12_STENCIL_OP_INCR_SAT; // 1 増やす（255 で止まる）。同じピクセルに2回描いても、2回目は値が違うので通らない
		break;
	case StencilMode::Decrement:
		face.StencilPassOp = D3D12_STENCIL_OP_DECR_SAT; // 1 減らす（0 で止まる）
		break;
	default:
		face.StencilPassOp = D3D12_STENCIL_OP_KEEP; // 調べるだけ
		break;
	}
	desc.StencilEnable = TRUE;
	desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	desc.StencilWriteMask = (stencil == StencilMode::Test) ? 0 : D3D12_DEFAULT_STENCIL_WRITE_MASK;
	desc.FrontFace = face; // 2D は両面を描くので、裏も同じにする
	desc.BackFace = face;
	return desc;
}


//=============================================================================
// 2D を重ねる順
//=============================================================================
bool UIDrawKey::operator<(const UIDrawKey& other) const {
	// --- 外の段から順に比べる（Order → 種類 → 番号）---
	const uint32_t common = (std::min)(depth, other.depth);
	for (uint32_t i = 0; i < common; ++i) {
		if (parts[i] != other.parts[i]) {
			return parts[i] < other.parts[i];
		}
	}
	// --- 段の数が同じ：同じ物の、マスクの始まり → 終わり、その中の手順の順 ---
	if (depth == other.depth) {
		return (phase != other.phase) ? phase < other.phase : step < other.step;
	}
	// --- 片方の段が、もう片方の頭と同じ：段の少ない方がマスクの図形で、多い方はその中身 ---
	// マスクの始まり（phase が −）なら中身より先、終わりなら後
	return (depth < other.depth) ? (phase < 0) : (other.phase > 0);
}