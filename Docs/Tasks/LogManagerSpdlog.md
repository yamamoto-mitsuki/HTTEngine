参考：
- spdlog のヘッダーだけ版と、ライブラリにして使う版（SPDLOG_COMPILED_LIB）：https://github.com/gabime/spdlog#install
- 前方宣言と不完全型（`std::shared_ptr` は不完全型のまま持てる）：https://en.cppreference.com/w/cpp/memory/shared_ptr ・ https://en.cppreference.com/w/cpp/language/type#Incomplete_type
- `/GL`（プログラム全体の最適化。obj に中間コードが入る）：https://learn.microsoft.com/ja-jp/cpp/build/reference/gl-whole-program-optimization
- `/LTCG`（リンク時のコード生成・`:incremental`）：https://learn.microsoft.com/ja-jp/cpp/build/reference/ltcg-link-time-code-generation
- `PreferredToolArchitecture`（64 ビット版のツールを使う）：https://learn.microsoft.com/ja-jp/cpp/build/reference/msbuild-visual-cpp-overview
- `Directory.Build.props`（MSBuild が自動で読むファイル）：https://learn.microsoft.com/ja-jp/visualstudio/msbuild/customize-by-directory

# LogManager.h から spdlog を外す

Release の `MyEngine_v1.lib` が **1.5GB** あった。中身を調べると、エンジンの .cpp 197 個のうち **134 個が spdlog の実装を丸ごと持っていた**。
原因は `LogManager.h` が spdlog を include していたこと。spdlog は「ヘッダーだけで使う形」なので、include した .cpp には spdlog（と同梱の fmt）の実装が全部入る。

spdlog を使っているのは `LogManager.cpp` だけ。`LogManager.h` を読む側は `LogManager::Log(...)` などを呼ぶだけで、spdlog を知らなくてよい。
なので、**spdlog は LogManager.cpp の中だけに閉じ込める**（ヘッダーは「型の名前」だけ知らせる）。

パートは **A → B → C の順に写して、C まで写してから1回ビルドする**（A だけだと GameNotification.cpp がビルドできない。理由はパートC）。

ブランチは `feature/LogManagerSpdlog`（develop から）。

## 0章 全体の形

### 0.1 何が起きていたか

```
LogManager.h ──include──→ spdlog/spdlog.h・async.h（実装も全部ヘッダー）
     ↑                         └→ fmt（同梱・これも実装ごと）・<Windows.h>・<thread>・<chrono> …
     │
  134 個の .cpp（MyAssert.h・Engine.h などを通して読む）
     → 1 つ 1 つの obj に spdlog の実装が入る
     → Release（/GL）は obj に「中間コード」を全部持つので、1 つで約 6MB 増える
```

測った値（Release・CameraSystem.cpp を1つだけコンパイル）：

| 中身 | obj の大きさ |
|---|---|
| 今の CameraSystem.cpp | 13.1MB |
| LogManager.h を spdlog 無しにした CameraSystem.cpp | 9.5MB |
| `#include <externals/spdlog/spdlog.h>` だけの .cpp | 5.9MB |
| `#include <string>` だけの .cpp | 0.5MB |

全部写した後の `MyEngine_v1.lib`（写しで確認した値）：

| 構成 | 前 | 後 |
|---|---|---|
| Release | 1,495MB | **950MB**（-36%） |
| Development | 484MB | 460MB |
| Debug | 626MB | 600MB |

Release だけ大きく減るのは、/GL の obj が「使っていない inline 関数」の中間コードまで持つから。Debug・Development（/GL 無し）は、使った関数しか obj に入らないので、少ししか減らない。

### 0.2 直す物

| パート | ファイル | 何をするか |
|---|---|---|
| A | `Diagnostics/LogManager.h` | spdlog の include を消して前方宣言にする。`Flush` の本体を .cpp へ。ImGui 用の重さを自分の enum に |
| B | `Diagnostics/LogManager.cpp` | spdlog の include をここに足す。`Flush` の本体。重さの enum に合わせる |
| C | `UI/GameNotification.cpp` | `<shellapi.h>` の前に `<Windows.h>` を読む（今まで spdlog が読んでいたので、たまたま通っていた） |

### 0.3 考えたこと

- **前方宣言にした理由**：`std::shared_ptr<T>` は、`T` の中身が分からない（不完全型）ままでも、メンバーに持てる・コピーできる・消せる。中身が要るのは `logger_->flush()` のように **T の関数を呼ぶ所だけ**で、それは全部 LogManager.cpp にある。
  - `std::unique_ptr<T>` だと、**消す所（デストラクタ）で T の中身が要る**。LogManager のデストラクタはヘッダーで `= default` なので、unique_ptr だとこの形ではビルドできない。今の shared_ptr のままでよい（spdlog の関数も shared_ptr で渡す作り）。
- **Pimpl（中身を全部 .cpp の構造体に入れる形）にしなかった理由**：ヘッダーに出ている spdlog の名前は `async_logger` 1つだけで、前方宣言1行で隠せる。Pimpl は、ヘッダーに出したくない型が増えたときに考える。
- **ImGui 用の重さを自分の enum（`LogLevel`）にした理由**：spdlog の `level_enum` も前方宣言はできる（`enum level_enum : int;`）が、元の宣言と型（`: int`）が少しでも違うとビルドが止まり、spdlog を入れ替えたときにヘッダーまで直すことになる。色分けに要るのは3つだけなので、エンジンの型にした。
- **MyAssert.h が `LogManager::Flush()` を呼んでいる**：Flush を .cpp へ移しても、呼ぶ側は「関数を呼ぶ」だけなので何も変えなくてよい。

## パートA LogManager.h

### A-1 include を前方宣言にする

```cpp
// 消す
#include <externals/spdlog/common.h>
#include <externals/spdlog/spdlog.h>
#include <externals/spdlog/async.h>
```

```cpp
// <source_location> の下に足す
#include <mutex>

// ===== spdlog の前方宣言 =====
// spdlog はヘッダーだけで使う形（実装も全部ヘッダーにある）。ここで include すると、
// LogManager.h を読む全部の .cpp が spdlog の実装を持ってしまう（Release の /GL では 1 つで約 6MB 増える）
// ここでは型の名前だけ知らせて、spdlog の include は LogManager.cpp だけに置く
// （std::shared_ptr は、中身の分からない型（不完全型）のままでも持てる）
namespace spdlog {
class async_logger;
}
```

`<mutex>` は ImGui 用の `bufferMutex_`（`std::mutex`）のため。今までは spdlog が読んでいたので書かなくても通っていた。

### A-2 Flush の本体を .cpp へ

```cpp
// 前
	static void Flush() {
		if (instance_ && instance_->logger_) {
			instance_->logger_->flush();
		}
	}
```

```cpp
// 後
	static void Flush(); // logger_ の中身（spdlog の型）を使うので、本体は LogManager.cpp
```

上の `/// <summary>` のコメントはそのまま残す。

### A-3 ImGui 用の重さを自分の enum に

```cpp
#ifdef USE_IMGUI
	// ImGui表示用の重さ（色分けに使う。spdlog の level_enum をヘッダーに出さないため、自分の型にする）
	enum class LogLevel {
		Info,    // 白
		Warning, // 黄
		Error,   // 赤
	};

	// ImGui表示用バッファ
	struct LogEntry {
		std::string message;
		LogLevel level; // 前は spdlog::level::level_enum
	};
```

## パートB LogManager.cpp

### B-1 spdlog の include を足す

```cpp
#include <filesystem>

// spdlog の include はこの .cpp だけに置く（LogManager.h は前方宣言だけ）
#include <externals/spdlog/spdlog.h>      // ← 足す（init_thread_pool・register_logger・shutdown）
#include <externals/spdlog/async.h>
#include <externals/spdlog/sinks/msvc_sink.h>
#include <externals/spdlog/sinks/basic_file_sink.h>
```

### B-2 Flush の本体（Shutdown の下に足す）

```cpp
//=============================================================================
// 即時フラッシュ
//=============================================================================
void LogManager::Flush() {
	if (instance_ && instance_->logger_) {
		instance_->logger_->flush();
	}
}
```

### B-3 重さを LogLevel に

`Log`・`Warning`・`Error` の `logBuffer_.push_back` の最後の値を変える。

| 関数 | 前 | 後 |
|---|---|---|
| `Log` | `spdlog::level::info` | `LogLevel::Info` |
| `Warning` | `spdlog::level::warn` | `LogLevel::Warning` |
| `Error` | `spdlog::level::err` | `LogLevel::Error` |

`DrawImGuiWindow` の switch：

```cpp
		switch (entry.level) {
		case LogLevel::Warning:
			color = ImVec4(1.0f, 0.9f, 0.2f, 1.0f);
			break; // 黄
		case LogLevel::Error:
			color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
			break; // 赤
		case LogLevel::Info:
			hasColor = false;
			break; // 白（デフォルト）
		}
```

`default` を消して3つとも書く（自分の enum になったので、どの値が何色かを全部並べて見せる）。

`logger_->set_level(spdlog::level::trace)` などの spdlog の値は、.cpp の中なのでそのままでよい。

## パートC GameNotification.cpp

`<shellapi.h>` は `HWND` などの Windows.h の型を使うのに、自分では Windows.h を読まない。今までは、その上の `LogManager.h` → spdlog が Windows.h を読んでいたので、たまたま通っていた。
include の並び（CLAUDE.md の決まり）にもそろえる。

```cpp
#include "MyEngine/UI/GameNotification.h"

#include <string_view>
#include <format>

// shellapi.h は Windows.h の型（HWND など）を使うので、先に Windows.h を読む
// （前は LogManager.h の中の spdlog が Windows.h を読んでいたので、たまたま通っていた）
#include <Windows.h>
#include <shellapi.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#pragma comment(lib, "windowsapp.lib")

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/String/ConvertString.h"
```

## 確かめること

1. 3構成（Debug・Development・Release）でビルドが通る。警告が増えていない
2. `generated/outputs/Release/MyEngine_v1.lib` が 1GB を切る（写しでは 950MB）
3. Sandbox を起動して、`project/Sandbox/logs/` の新しい .log に `[LogManager::Initialize] Initialized` が出る（Release・Debug とも）
4. Debug で Console ウィンドウの色：warning が黄・error が赤・ほかは白（写しでは見ていない）
5. AL4_01：サブモジュールを進めてビルドする。ゲームの .cpp が spdlog 経由の include（`<Windows.h>`・`<mutex>`・`<thread>`・`<chrono>`・`<format>` など）に頼っていたら、そこで「定義されていない識別子」が出る。出たら、その .cpp に要るヘッダーを自分で include する（パートC と同じ直し方）

## この先

- 次に大きいのは `<filesystem>`（136 個の .cpp）・`<chrono>`・`<format>`・nlohmann/json（34 個）。どれも 1 つで 2〜4MB。/GL では普通の大きさなので今は直さない。「どのヘッダーが何を読んでいるか」を整理するときに、同じ考え方（使う .cpp だけで include する）で見直す。

## 同じ時に入れた設定（本体に直接入れた・コミット ae5e586）

Release のリンクで `LNK4020（型レコードの破損）` が出続けていた件の直し。写経は無し。

- `MyEngine.props` の Release：`LinkTimeCodeGeneration` を `UseLinkTimeCodeGeneration`（増分なしの `/LTCG`）、`LinkTimeCodeGenerationObjectFile` を空（`.iobj` を書かない）
- `project/MyEngine.Toolset.props`（新しい）：`PreferredToolArchitecture=x64`。`project/Directory.Build.props` から読む
- AL4_01 は、`project/Directory.Build.props` を作って `..\MyEngine_v1\project\MyEngine.Toolset.props` を Import する（サブモジュールを ae5e586 より後に進めてから）
