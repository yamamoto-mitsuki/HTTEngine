#include "VertexFormat.h"

//=============================================================================
// シェーダーリフレクション情報からInputLayoutを生成
//=============================================================================
std::vector<D3D12_INPUT_ELEMENT_DESC> VertexFormat::MakeInputLayout(const std::vector<ShaderInputParameter>& inputs) {
	std::vector<D3D12_INPUT_ELEMENT_DESC> elems;
	for (const auto& in : inputs) {
		if (in.systemValueType != D3D_NAME_UNDEFINED) {
			continue; // SV_InstanceID/VertexID は頂点バッファに無い→除外
		}

		D3D12_INPUT_ELEMENT_DESC e{};
		e.SemanticName = in.semanticName.c_str(); // reflectionはキャッシュ常駐なのでポインタ有効
		e.SemanticIndex = in.semanticIndex;
		e.Format = FormatOf(in.componentType, in.mask);
		e.InputSlot = 0; // 何本目の頂点バッファから読むか
		e.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT; // 同じスロットの1つ前の要素のすぐ後ろ（スロットが変わると0から数え直す）
		e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		e.InstanceDataStepRate = 0;
		elems.push_back(e);
	}
	return elems;
}


//=============================================================================
// シェーダー入力パラメータから対応するDXGI_FORMATを取得
//=============================================================================
DXGI_FORMAT VertexFormat::FormatOf(D3D_REGISTER_COMPONENT_TYPE type, BYTE mask) {
	int n = 0;
	for (BYTE m = mask; m; m >>= 1) {
		n += (m & 1); // 立ってるビット数=成分数
	}
	switch (type) {
	case D3D_REGISTER_COMPONENT_FLOAT32:
		return n == 1 ? DXGI_FORMAT_R32_FLOAT : n == 2 ? DXGI_FORMAT_R32G32_FLOAT : n == 3 ? DXGI_FORMAT_R32G32B32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
	case D3D_REGISTER_COMPONENT_UINT32:
		return n == 1 ? DXGI_FORMAT_R32_UINT : n == 2 ? DXGI_FORMAT_R32G32_UINT : n == 3 ? DXGI_FORMAT_R32G32B32_UINT : DXGI_FORMAT_R32G32B32A32_UINT;
	case D3D_REGISTER_COMPONENT_SINT32:
		return n == 1 ? DXGI_FORMAT_R32_SINT : n == 2 ? DXGI_FORMAT_R32G32_SINT : n == 3 ? DXGI_FORMAT_R32G32B32_SINT : DXGI_FORMAT_R32G32B32A32_SINT;
	}
	return DXGI_FORMAT_UNKNOWN;
}