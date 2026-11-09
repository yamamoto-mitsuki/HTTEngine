#pragma once
#include <span>
#include <memory>
#include <string>
#include <cstdint>
#include <iterator>

#include <d3d12.h>
#include <wrl.h>


/// <summary>
/// DefaultHeapへの転送をまとめて行う
/// <para>専用のCommandList,Fenceを持ち、フレーム描画の同期とは独立して動く</para>
/// </summary>
class UploadContext {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class UploadContext;
	};
	explicit UploadContext(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	/// <summary>
	/// 初期化。DirectXCommon::Initialize()の後に呼ぶ
	/// </summary>
	/// <param name="stagingBytes">使い回すステージングバッファのサイズ（デフォルト: 8MB）</param>
	static void Initialize(size_t stagingBytes = 8 * 1024 * 1024);

	/// <summary>
	/// 解放。DirectXCommon::Release()の前に呼ぶ
	/// </summary>
	static void Release();

	/// <summary>
	/// dest(DEFAULTヒープ)へdataを転送する予約。stagingへ書き、GPUコピーを記録する。
	/// </summary>
	/// <param name="finalState">転送後の状態（頂点バッファなら VERTEX_AND_CONSTANT_BUFFER）</param>
	static void QueueUpload(ID3D12Resource* dest, const void* data, size_t size, D3D12_RESOURCE_STATES finalState);

	/// <summary>
	/// dest(DEFAULTヒープの2Dテクスチャ)へ、ミップマップの段ごとの中身を転送する予約。
	/// <para>subResources[i] が i 段目（0 が一番大きい絵）。1行の幅は、中で GPU の決まり（256バイトの倍数）に詰め直す</para>
	/// <para>描画のコマンドリストとは別のリストで送るので、フレームの途中（ImGui を描いている間）でも使える</para>
	/// </summary>
	/// <param name="finalState">転送後の状態（シェーダーで読むなら PIXEL_SHADER_RESOURCE）</param>
	/// <param name="currentState">今の状態（作ったばかりなら COPY_DEST のまま。前に送った絵を書き直すなら、前の finalState）</param>
	static void QueueTextureUpload(ID3D12Resource* dest, std::span<const D3D12_SUBRESOURCE_DATA> subresources, 
		D3D12_RESOURCE_STATES finalState, D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COPY_DEST);

	/// <summary>
	/// 予約した全コピーをGPUに流して完了を待ち、stagingを再利用可能に戻す
	/// </summary>
	static void Flush();


	// ===== 中身付きの GPU常駐バッファを作る（作って、転送を予約する。GPUで使う前にFlushする）=====

	/// <summary>
	/// DEFAULTヒープのバッファを作り、data を送る予約をする
	/// </summary>
	/// <param name="finalState">転送後の状態（頂点バッファなら VERTEX_AND_CONSTANT_BUFFER）</param>
	/// <param name="name">PIX・エラーのときに出る名前</param>
	static Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferWithData(const void* data, size_t size,D3D12_RESOURCE_STATES finalState,
		const std::wstring& name);

	/// <summary>
	/// 頂点バッファを作り、ビューを返す。頂点の型は、入れ物の中身の方から決まる
	/// </summary>
	/// <param name="vertices">std::vector・std::arrayなど（中身がメモリに1列に並んでいる入れ物）</param>
	/// <param name="outBuffer">作ったバッファ（持ち主が持っておく。消えるとGPUから読めなくなる）</param>
	template<class TContainer>
	static D3D12_VERTEX_BUFFER_VIEW CreateVertexBuffer(const TContainer& vertices, 
		Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer, const std::wstring& name){
		using TVertex = typename TContainer::value_type; // 入れ物の中身の型
		const size_t size = sizeof(TVertex) * std::size(vertices);
		outBuffer = CreateBufferWithData(std::data(vertices), size, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, name);

		D3D12_VERTEX_BUFFER_VIEW vbv{};
		vbv.BufferLocation = outBuffer->GetGPUVirtualAddress();
		vbv.SizeInBytes = static_cast<UINT>(size);
		vbv.StrideInBytes = sizeof(TVertex); // 1頂点分の大きさ（頂点番号 × これ で次の頂点）
		return vbv;
    }

	/// <summary>
	/// インデックスバッファを作り、ビューを返す。uint16_t なら R16_UINT、uint32_t なら R32_UINT
	/// </summary>
	template<class TContainer>
	static D3D12_INDEX_BUFFER_VIEW CreateIndexBuffer(const TContainer& indices, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer, 
		const std::wstring& name) {
		using TIndex = typename TContainer::value_type;
		static_assert(sizeof(TIndex) == 2 || sizeof(TIndex) == 4, "インデックスは、uint16_t か uint32_tにしてください");
		const size_t size = sizeof(TIndex) * std::size(indices);
		outBuffer = CreateBufferWithData(std::data(indices), size, D3D12_RESOURCE_STATE_INDEX_BUFFER, name);

		D3D12_INDEX_BUFFER_VIEW ibv{};
		ibv.BufferLocation = outBuffer->GetGPUVirtualAddress();
		ibv.SizeInBytes = static_cast<UINT>(size);
		ibv.Format = (sizeof(TIndex) == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT; // 型の区別は Format で伝える
		return ibv;
	}


private:
	// シングルトン
	friend struct std::default_delete<UploadContext>;
	~UploadContext() = default;
	UploadContext(const UploadContext&) = delete;
	UploadContext& operator=(const UploadContext&) = delete;
	static std::unique_ptr<UploadContext> instance_;
	void Init(size_t stagingBytes);

	// 転送用
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;
	// 永続MapのUploadBuffer
	Microsoft::WRL::ComPtr<ID3D12Resource> staging_;
	uint8_t* stagingMapped_ = nullptr;
	size_t stagingCapacity_ = 0;
	size_t stagingOffset_ = 0;
	bool hasPending_ = false; // 未Flushの記録があるか
};