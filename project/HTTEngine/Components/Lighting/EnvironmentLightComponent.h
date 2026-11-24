#pragma once
#include <type_traits>

// 環境光（空から来る光）の強さ。シーンに1つ、どの Entity に付けてもよい Component（カテゴリ：Lighting）
// 値は EnvironmentLightSystem が毎フレーム SceneRenderer へ渡す（光の強さはカメラの定数バッファ、空の絵の明るさは Skybox）
// 有効な物が2つ以上あれば、先に見つかった1つだけを使う。1つも無ければ全部 1（空の画像の明るさのまま）。停止中のエディタでも掛かる

/// <summary>
/// 環境光（Unity の Lighting の Environment・Unreal の Sky Light の小さい版）
/// <para>空の画像（IBL）から来る光を、平行光源とは別に強めたり弱めたりする</para>
/// </summary>
struct EnvironmentLightComponent {
	bool enabled = true;              // false の間は全部 1（付けていないのと同じ）
	float intensity = 1.0f;           // 空から来る、ぼんやりした光の強さ（PBR の物の全部の面に当たる。下げると、平行光源の当たる面と影の差が出る）
	float reflectionIntensity = 1.0f; // 空の映り込みの強さ（つるつるした物・金属に写る空）
	float skyIntensity = 1.0f;        // 空の絵の明るさ（Camera の Background が Skybox のとき。物を照らす光は変わらない）
};

static_assert(std::is_trivially_copyable_v<EnvironmentLightComponent>);
