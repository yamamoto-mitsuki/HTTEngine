#pragma once

/// <summary>
/// .material のファイルを Inspector で直す係（AssetInspector に登録する）。エディタだけ
/// <para>欄は MaterialManager::Describe（Shader の #PROPERTIES から作る）。ここはファイルへの書き戻しと Undo への記録だけを行う</para>
/// </summary>
class MaterialAssetEditor {
public:
	// AssetInspector に登録する
	static void Initialize();
};
