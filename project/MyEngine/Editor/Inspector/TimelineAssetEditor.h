#pragma once

/// <summary>
/// .timeline のファイルを Inspector に出す係（AssetInspector に登録する）。エディタだけ
/// <para>中身は Timeline ウィンドウで直す。ここは様子と、開くボタンだけ</para>
/// </summary>
class TimelineAssetEditor {
public:
	// AssetInspector に登録する
	static void Initialize();
};