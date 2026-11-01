#pragma once

/// <summary>
/// .curve のファイルを Inspector で直す係（AssetInspector に登録する）。エディタだけ
/// <para>絵と操作は CurveEditor に任せて、ここはファイルへの書き戻しと Undo への記録だけを行う</para>
/// </summary>
class CurveAssetEditor {
public:
	// AssetInspector に登録する
	static void Initialize();
};