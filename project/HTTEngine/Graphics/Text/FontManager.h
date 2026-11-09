#pragma once
#include <span>
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

#include "HTTEngine/Math/Vector2.h"


/// <summary>
/// 文字1つの形（フォントの画像（アトラス）のどこにあるか・どこに置くか）
/// <para>長さは「基準の大きさ（FontManager::kBaseSize）」のときのピクセル。描くときは「描く大きさ ÷ kBaseSize」を掛ける</para>
/// </summary>
struct Glyph {
	Vector2 uvMin = {0.0f, 0.0f};  // アトラスの中の、絵の左上（UV。0〜1）
	Vector2 uvMax = {0.0f, 0.0f};  // 絵の右下
	Vector2 size = {0.0f, 0.0f};   // 絵の幅・高さ（0 なら絵が無い＝空白）
	Vector2 offset = {0.0f, 0.0f}; // ペンの位置（ベースラインの上）から、絵の左上までのずれ（右が＋、下が＋）
	float advance = 0.0f;          // ペンを右へ進める幅（次の文字の位置）
	int index = 0;                 // フォントの中の番号（字詰め（カーニング）を調べるのに使う。0 はフォントに無い文字＝豆腐の四角）
};

/// <summary>
/// フォントの縦の寸法（基準の大きさのピクセル）
/// </summary>
struct FontMetrics {
	float ascent = 0.0f;  // ベースラインから、一番高い文字の上の端まで（＋）
	float descent = 0.0f; // ベースラインから、一番低い文字の下の端まで（−。g・y の下の出っぱり）
	float lineGap = 0.0f; // 行と行の間に足すすき間
};


/// <summary>
/// フォントを読み込んで、文字の形を SDF（縁までの距離）の画像にして、1枚のアトラスに並べる
/// <para>文字は使われたときに1つずつ作る（日本語の数千文字を最初に全部作ると重いので）。作った文字はアトラスに残り、次からはそのまま使う</para>
/// <para>書き足したアトラスは、UploadAtlases で GPU へ送る（1フレームに1回、描き始める前）</para>
/// <para>フォントのファイルを読むのは stb_truetype（ImGui に入っている imstb_truetype.h を、ImGui とは別に使う）</para>
/// </summary>
class FontManager {
public:
	static constexpr float kBaseSize = 48.0f; // SDF を作る大きさ（全角の1文字の幅がこのピクセル）。描くときは、これを拡大・縮小する
	static constexpr int kSdfPadding = 8;     // 文字の縁から、何ピクセル外側まで距離を入れるか（縁取りの太さの上限。基準の大きさで）

	// 初期化 / 解放（解放は DirectXCommon::Release より前。GPU の絵を持っているので）
	static void Initialize();
	static void Release();

	/// <summary>
	/// フォントのファイル（.ttf / .otf / .ttc）を読み込む。同じファイルなら、前に読んだ物の番号を返す
	/// </summary>
	/// <param name="filePath">フォントのファイル（UTF-8 の文字から作るときは ToPath を通す）</param>
	/// <returns>フォントの番号（1から）。読めなかったら 0</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	// フォントの番号からパスを引く（知らない番号なら空）
	static const std::string& GetPath(uint32_t handle);

	/// <summary>
	/// 既定のフォント（Text の Font が (none) のときに使う）。初めて呼ばれたときに、Windows のフォントから探して読む
	/// <para>メイリオ → 游ゴシック → ＭＳ ゴシック → Arial の順に、入っている物を使う。どれも無ければ 0</para>
	/// </summary>
	static uint32_t GetDefaultFont();

	/// <summary>
	/// 文字1つの形。初めての文字なら、SDF を作ってアトラスに足す（アトラスがいっぱいなら、絵の無い形を返す）
	/// <para>返した参照は FontManager::Release まで使える</para>
	/// </summary>
	static const Glyph& GetGlyph(uint32_t handle, char32_t codepoint);

	// 2つの文字の間を詰める量（基準の大きさのピクセル。詰めるならマイナス。「AV」「To」など）
	static float GetKerning(uint32_t handle, const Glyph& left, const Glyph& right);

	// フォントの縦の寸法（行の高さを決めるのに使う）
	static const FontMetrics& GetMetrics(uint32_t handle);

	// アトラスの SRV の番号（シェーダーの gTextures の何番目か）
	static uint32_t GetAtlasTextureIndex(uint32_t handle);

	/// <summary>
	/// 書き足したアトラスを GPU へ送る（描き始める前に、1フレームに1回。WindowManager::PreRenderAll の頭）
	/// <para>描画とは別のコマンドリスト（UploadContext）で送って、終わるまで待つ</para>
	/// </summary>
	static void UploadAtlases();

	// ===== フォントのファイル（エディタのフォントの見本も使う）=====
	// ファイルの中身を全部読む（読めなければ空）
	static std::vector<uint8_t> ReadFontFile(const std::filesystem::path& filePath);
	// stb_truetype で読めるフォントのデータか（.ttc は1つ目のフォントを見る）
	static bool IsFontData(std::span<const uint8_t> data);
	// Windows のフォントの置き場所（全員用と、自分だけに入れた物）。無いフォルダは入れない
	static std::vector<std::filesystem::path> GetSystemFontFolders();
};