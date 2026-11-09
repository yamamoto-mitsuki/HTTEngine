#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/Graphics/Model/ModelManager.h"

// 前方宣言
struct Entity;


/// <summary>
/// スキンのあるメッシュを CS で曲げて、曲げた頂点（SkinnedVertex）を持っておく所
/// <para>出力バッファは「Entity × スキンのあるメッシュ」ごとに1つ（同じモデルでも、Entity ごとにポーズが違うので）</para>
/// <para>使い方：DrawModel が Request で仕事を積む → 描き始める前に Dispatch で全部まとめて曲げる → 描くときは Request が返した VBV を使う</para>
/// </summary>
class SkinCache {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class SkinCache;
	};
	explicit SkinCache(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	// 初期化・解放
	static void Initialize();
	static void Release();

	/// <summary>
	/// このフレームに、Entity のスキンのあるメッシュを曲げる仕事を積み、曲げた頂点を読むための VBV を返す
	/// <para>出力バッファが無ければここで作る。同じフレームに同じ Entity・メッシュでもう一度呼ばれたら、仕事は積まずに同じ VBV を返す</para>
	/// </summary>
	/// <param name="entity">持ち主の Entity（出力バッファを見分けるキー）</param>
	/// <param name="meshIndex">ModelAsset::meshes の何番目か（キー）</param>
	/// <param name="mesh">曲げるメッシュ（頂点・骨と重みのバッファを読む）</param>
	/// <param name="paletteAddress">行列パレットのGPUアドレス（RenderContext::PushFrameArray で書いた物）</param>
	static D3D12_VERTEX_BUFFER_VIEW Request(Handle<Entity> entity, uint32_t meshIndex, const ModelManager::SubMesh& mesh,
		D3D12_GPU_VIRTUAL_ADDRESS paletteAddress);

	/// <summary>
	/// 積んだ仕事を全部 Dispatch して、出力を頂点バッファとして読める状態にする
	/// <para>どのウィンドウも描き始める前に、1フレームに1回だけ呼ぶ（WindowManager::PreRenderAll の最初）</para>
	/// <para>このフレームに Request されなかった出力バッファ（消えた・無効にした Entity の物）は、ここで捨てる</para>
	/// </summary>
	static void Dispatch(ID3D12GraphicsCommandList* cmdList);

	// 今持っている出力バッファの数（Profilerの表示用）
	static size_t GetBufferCount() { return instance_->outputs_.size(); }

private:
	// ===== 出力バッファを見分けるキー =====
	struct Key {
		Handle<Entity> entity;
		uint32_t meshIndex = 0;
		bool operator==(const Key&) const = default;
	};
	// unordered_map のキーにするための、ハッシュ（数を混ぜて1つの数にする）
	struct KeyHash {
		size_t operator()(const Key& key) const {
			size_t hash = key.entity.index;
			hash = hash * 31 + key.entity.generation;
			hash = hash * 31 + key.meshIndex;
			return hash;
		}
	};

	// ===== 1つの「Entity × メッシュ」の出力 =====
	struct Output {
		Microsoft::WRL::ComPtr<ID3D12Resource> buffer; // 曲げた頂点（UAV にも頂点バッファにもなる）
		D3D12_VERTEX_BUFFER_VIEW vbv{};                // 描くときに使う
		uint32_t vertexCount = 0;                      // 頂点の数（モデルが差し替わって数が変わったら作り直す）
		uint64_t lastRequestedFrame = 0;               // 最後に Request されたフレーム（されなくなったら捨てる）
	};

	// ===== 1回の Dispatch の材料 =====
	struct Job {
		D3D12_GPU_VIRTUAL_ADDRESS paletteAddress = 0;   // t0：行列パレット
		D3D12_GPU_VIRTUAL_ADDRESS inputAddress = 0;     // t1：元の頂点
		D3D12_GPU_VIRTUAL_ADDRESS influenceAddress = 0; // t2：骨と重み
		D3D12_GPU_VIRTUAL_ADDRESS infoAddress = 0;      // b0：頂点の数
		ID3D12Resource* output = nullptr;               // u0：書き込む先（Output::buffer。持ち主は outputs_）
		uint32_t vertexCount = 0;                       // スレッドの数
	};

	// 出力バッファを作る（頂点の数ぶん。UAV にできるように）
	static void CreateOutput(Output& output, uint32_t vertexCount);

	// シングルトン
	friend struct std::default_delete<SkinCache>;
	~SkinCache() = default;
	SkinCache(const SkinCache&) = delete;
	SkinCache& operator=(const SkinCache&) = delete;
	static std::unique_ptr<SkinCache> instance_;

	std::unordered_map<Key, Output, KeyHash> outputs_; // Entity × メッシュ → 出力
	std::vector<Job> jobs_;                            // このフレームに積んだ仕事（Dispatch で空にする）
	uint64_t frame_ = 1;                               // 今のフレームの番号（Dispatch のたびに1つ進む）
};