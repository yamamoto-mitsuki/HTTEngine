#include "TextRenderSystem.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string_view>
#include <vector>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Graphics/Text/FontManager.h"
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "HTTEngine/Systems/UI/UIMaskSystem.h"
#include "HTTEngine/Time/EngineTime.h"
#include "HTTEngine/Math/MathUtility.h"

namespace {
constexpr char32_t kReplacementCharacter = U'�';           // 壊れた UTF-8 の代わりに出す文字（？の形）
constexpr float kMaxOutlineWidth = 0.45f;                  // 縁取りの太さの上限（SDF の値。0.5 にすると、距離を入れた範囲の外（値が 0 の所）まで塗って、四角く見える）。光（グロー）も合わせてここまで
constexpr float kShineSlant = 0.3f;                        // 斜めの光の傾き（Text2d.PS の kShineSlant と同じ値にする。帯を端から端まで通すのに使う）
constexpr float kShakeFrequencyX = 3.7f;                   // 震えるときの横の速さの倍率（縦と違う数にして、同じ動きの繰り返しに見えないように）
constexpr float kShakeFrequencyY = 5.3f;                   // 震えるときの縦の速さの倍率

/// <summary>
/// 並べた文字1つ（ピクセル。Entity の位置が原点、Y は上が＋）
/// </summary>
struct PlacedGlyph {
	Vector2 min = {0.0f, 0.0f};   // 左下
	Vector2 max = {0.0f, 0.0f};   // 右上
	Vector2 uvMin = {0.0f, 0.0f}; // アトラスの中の左上
	Vector2 uvMax = {0.0f, 0.0f}; // アトラスの中の右下
};

/// <summary>
/// 1行分（glyphs の first 番目から count 個）
/// </summary>
struct TextLine {
	size_t first = 0;   // その行の最初の文字が、glyphs の何番目か
	size_t count = 0;   // その行の、絵のある文字の数（空白は数えない）
	float width = 0.0f; // 行の幅（ピクセル）
};

/// <summary>
/// 文字を並べた結果（毎回作り直す。配列のメモリは使い回す）
/// </summary>
struct TextLayout {
	std::vector<PlacedGlyph> glyphs; // 絵のある文字
	std::vector<TextLine> lines;     // 行（1行でも1つある）
	Vector2 size = {0.0f, 0.0f};     // 文字のかたまりの大きさ（ピクセル）
	Vector2 min = {0.0f, 0.0f};      // 文字のかたまりの左下（ピクセル。Entity の位置が原点）
	Vector2 max = {0.0f, 0.0f};      // 文字のかたまりの右上
};

/// <summary>
/// TextRenderSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct TextRenderState {
	std::vector<char32_t> codepoints; // 1文字ずつに分けた文字（作業用）
	TextLayout layout;                // 並べた結果（作業用）
	float time = 0.0f;                // 演出の時計（秒。一時停止・停止中も進む＝エディタでも動いて見える）
};
std::unique_ptr<TextRenderState> state;

//=============================================================================
// 文字を分ける・並べる
//=============================================================================
// UTF-8 の文字を、1文字ずつの番号（Unicode のコードポイント）に分ける
// UTF-8 は1文字が1〜4バイト。1バイト目の上のビットで長さが分かる（0xxxxxxx=1、110xxxxx=2、1110xxxx=3、11110xxx=4。続きのバイトは 10xxxxxx）
// 1バイト目の残りのビットと、続きのバイトの下6ビットずつをつなげると、文字の番号になる（「あ」= E3 81 82 → U+3042）
void DecodeUtf8(std::string_view text, std::vector<char32_t>& out) {
	out.clear();
	size_t i = 0;
	while (i < text.size()) {
		const uint8_t lead = static_cast<uint8_t>(text[i]);
		size_t length = 0;
		char32_t codepoint = 0;
		if (lead < 0x80) {
			length = 1;
			codepoint = lead;
		} else if ((lead & 0xE0) == 0xC0) {
			length = 2;
			codepoint = lead & 0x1F;
		} else if ((lead & 0xF0) == 0xE0) {
			length = 3;
			codepoint = lead & 0x0F;
		} else if ((lead & 0xF8) == 0xF0) {
			length = 4;
			codepoint = lead & 0x07;
		} else {
			out.push_back(kReplacementCharacter); // 続きのバイトが頭に来た（壊れている）
			++i;
			continue;
		}
		// 続きのバイトをつなげる（足りない・形が違うなら壊れている）
		bool isValid = (i + length <= text.size());
		for (size_t k = 1; isValid && k < length; ++k) {
			const uint8_t next = static_cast<uint8_t>(text[i + k]);
			isValid = ((next & 0xC0) == 0x80);
			codepoint = (codepoint << 6) | (next & 0x3F);
		}
		if (!isValid) {
			out.push_back(kReplacementCharacter);
			++i;
			continue;
		}
		out.push_back(codepoint);
		i += length;
	}
}

// 行を横にずらす量（space は「かたまりの幅 − その行の幅」＝その行の余り）
float AlignOffset(TextAlign align, float space) {
	switch (align) {
	case TextAlign::Left:
		return 0.0f;
	case TextAlign::Center:
		return space * 0.5f;
	case TextAlign::Right:
		return space;
	}
	return 0.0f;
}

// 文字を並べる（ピクセル。Entity の位置が原点、Y は上が＋。Pivot とそろえ方も入れる）
void LayoutText(const TextComponent& text, uint32_t font, TextLayout& layout) {
	std::vector<PlacedGlyph>& glyphs = layout.glyphs;
	std::vector<TextLine>& lines = layout.lines;
	glyphs.clear();
	lines.clear();
	DecodeUtf8(text.text.View(), state->codepoints);

	const float scale = text.fontSize / FontManager::kBaseSize; // 基準の大きさ → 描く大きさ
	const FontMetrics& metrics = FontManager::GetMetrics(font);
	const float ascent = metrics.ascent * scale;                                                              // ベースラインから上の端まで
	const float lineHeight = (metrics.ascent - metrics.descent + metrics.lineGap) * scale * text.lineSpacing; // 行の頭から次の行の頭まで

	// --- 1. 行ごとに、文字を左から並べる（x は行の頭から、y はベースラインから）---
	lines.push_back({});
	float penX = 0.0f;               // ペンの位置（次の文字を置く所）
	const Glyph* previous = nullptr; // 1つ前の文字（字詰めに使う。FontManager が持っている物を指すだけ）
	for (const char32_t codepoint : state->codepoints) {
		if (codepoint == U'\r') {
			continue; // Windows の改行（\r\n）の \r は飛ばす
		}
		if (codepoint == U'\n') {
			lines.back().width = penX;
			lines.push_back({glyphs.size(), 0, 0.0f});
			penX = 0.0f;
			previous = nullptr;
			continue;
		}
		const Glyph& glyph = FontManager::GetGlyph(font, codepoint);
		if (previous != nullptr) {
			penX += FontManager::GetKerning(font, *previous, glyph) * scale; // 「AV」などは少し詰める
		}
		if (glyph.size.x > 0.0f) {
			PlacedGlyph placed;
			placed.min.x = penX + glyph.offset.x * scale;
			placed.max.x = placed.min.x + glyph.size.x * scale;
			// offset.y は下が＋（stb_truetype の決まり）なので、上が＋に直す。絵の上の端がベースラインから -offset.y の高さ
			placed.max.y = -glyph.offset.y * scale;
			placed.min.y = placed.max.y - glyph.size.y * scale;
			placed.uvMin = glyph.uvMin;
			placed.uvMax = glyph.uvMax;
			glyphs.push_back(placed);
			++lines.back().count;
		}
		penX += glyph.advance * scale + text.letterSpacing;
		previous = &glyph;
	}
	lines.back().width = penX;

	// --- 2. かたまりの大きさ（幅は一番長い行、高さは行の数から）---
	float width = 0.0f;
	for (const TextLine& line : lines) {
		width = (std::max)(width, line.width);
	}
	const float height = static_cast<float>(lines.size() - 1) * lineHeight + (metrics.ascent - metrics.descent) * scale;
	layout.size = {width, height};

	// --- 3. 行ごとに、そろえ方で横にずらし、ベースラインの高さに置く（Pivot の所が Entity の位置になるように）---
	const float left = -text.pivot.x * width;
	const float top = (1.0f - text.pivot.y) * height;
	layout.min = {left, top - height};
	layout.max = {left + width, top};
	for (size_t row = 0; row < lines.size(); ++row) {
		const TextLine& line = lines[row];
		const Vector2 shift = {left + AlignOffset(text.align, width - line.width), top - ascent - static_cast<float>(row) * lineHeight};
		for (size_t i = line.first; i < line.first + line.count; ++i) {
			glyphs[i].min += shift;
			glyphs[i].max += shift;
		}
	}
}

//=============================================================================
// 描く
//=============================================================================
// 使うフォント（選んでいなければ既定のフォント）
uint32_t ResolveFont(uint32_t fontHandle) { return (fontHandle != 0) ? fontHandle : FontManager::GetDefaultFont(); }

// 縁取りの太さ（描く大きさのピクセル）→ SDF の値（シェーダーの outlineWidth）
// SDF は、縁から kSdfPadding ピクセル（基準の大きさで）離れると値が 0.5 変わる。描く大きさの1ピクセルは、基準の大きさでは kBaseSize ÷ fontSize ピクセル
float ToSdfWidth(float pixels, float fontSize) {
	if (pixels <= 0.0f || fontSize <= 0.0f) {
		return 0.0f;
	}
	const float basePixels = pixels * FontManager::kBaseSize / fontSize;
	return (std::min)(basePixels * 0.5f / static_cast<float>(FontManager::kSdfPadding), kMaxOutlineWidth);
}

// index 番目の文字を、今どれだけ動かすか（ピクセル）。文字ごとに motionSpread だけ遅らせるので、波のように伝わって見える
Vector2 CalcMotionOffset(const TextComponent& text, size_t index, float time) {
	const float cycle = time * text.motionSpeed - static_cast<float>(index) * text.motionSpread; // 何回目の動きの、どこにいるか
	switch (text.motion) {
	case TextMotion::None:
		return {0.0f, 0.0f};
	case TextMotion::Wave:
		// 波：上下に sin でゆらゆら（1回で、上 → 下 → 元）
		return {0.0f, std::sin(cycle * MathUtility::kTwoPi) * text.motionAmount};
	case TextMotion::Bounce:
		// 跳ねる：|sin| は「0 から上がって 0 に戻る」山が続く形。0 の所が着地（下に尖る）、上で一瞬止まって見える
		return {0.0f, std::abs(std::sin(cycle * std::numbers::pi_v<float>)) * text.motionAmount};
	case TextMotion::Shake:
		// 震える：横と縦を、違う速さの sin で動かす（同じ形の繰り返しに見えない）。文字ごとにずれるのでバラバラに震える
		return {std::sin(cycle * MathUtility::kTwoPi * kShakeFrequencyX) * text.motionAmount, std::sin((cycle + 0.25f) * MathUtility::kTwoPi * kShakeFrequencyY) * text.motionAmount};
	}
	return {0.0f, 0.0f};
}

// 斜めの光の帯の真ん中（かたまりの左が 0、右が 1）。通っていない間は、見えない所（-10）
// 1回 ＝ 通る時間（shineDuration）＋ 待つ時間（shineInterval）。帯の幅と傾きの分だけ外から始めて、外まで通す
float CalcShinePosition(const TextComponent& text, float halfWidth, float time) {
	const float duration = (std::max)(text.shineDuration, 0.01f);
	const float progress = std::fmod(time, duration + (std::max)(text.shineInterval, 0.0f)) / duration; // 0〜1 の間だけ通っている
	if (progress > 1.0f) {
		return -10.0f;
	}
	// 帯は「/」の向き（下へ行くほど左）。上の端は shinePosition、下の端は shinePosition − kShineSlant にある
	const float start = -halfWidth;                   // 上の端も左の外にある所から
	const float end = 1.0f + halfWidth + kShineSlant; // 下の端も右の外に出る所まで
	return start + (end - start) * progress;
}

// 並べた文字を、画面の正規化座標の頂点にする（1文字4つ。左下・左上・右下・右上の順）。offset はずらす量（影。ピクセル）
// position の z・w には「文字のかたまりの中の場所（左上 0,0 〜 右下 1,1）」を入れる（Text2d.VS がシェーダーへ運ぶ。斜めの光に使う）
std::vector<Vertex2dData> MakeVertices(const TextLayout& layout, const TextComponent& text, const Matrix4x4& worldMatrix, const Vector2& offset, float time) {
	std::vector<Vertex2dData> vertices;
	vertices.reserve(layout.glyphs.size() * 4);
	const float width = (std::max)(layout.size.x, 1.0f);
	const float height = (std::max)(layout.size.y, 1.0f);
	for (size_t i = 0; i < layout.glyphs.size(); ++i) {
		const PlacedGlyph& glyph = layout.glyphs[i];
		const Vector2 move = offset + CalcMotionOffset(text, i, time); // 影のずれ ＋ 文字1つずつの動き
		const auto toVertex = [&](float x, float y, float u, float v) {
			const Vector2 point = SpriteRenderSystem::CalcScreenPoint(worldMatrix, text.anchor, {x + move.x, y + move.y});
			const Vector2 local = {(x - layout.min.x) / width, (layout.max.y - y) / height}; // 動かす前の場所（光は文字と一緒に動かない）
			return Vertex2dData{
			    {point.x, point.y, local.x, local.y},
                {u, v}
            };
		};
		// アトラスは上が v=0（画像の上）、画面は上が＋なので、上の端（max.y）に uvMin.y を合わせる
		vertices.push_back(toVertex(glyph.min.x, glyph.min.y, glyph.uvMin.x, glyph.uvMax.y)); // 左下
		vertices.push_back(toVertex(glyph.min.x, glyph.max.y, glyph.uvMin.x, glyph.uvMin.y)); // 左上
		vertices.push_back(toVertex(glyph.max.x, glyph.min.y, glyph.uvMax.x, glyph.uvMax.y)); // 右下
		vertices.push_back(toVertex(glyph.max.x, glyph.max.y, glyph.uvMax.x, glyph.uvMin.y)); // 右上
	}
	return vertices;
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// Text の見せ方。Inspector とシーンファイルの両方がこの並びを使う（Image と同じ書き方）
void DescribeText(ComponentUI& ui, TextComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.Field("Text###text", value.text, Multiline().Tip("出す文字（Enter で改行。255 バイト＝日本語で85文字くらいまで）。Script からは text = \"...\" で書き換える"));
	ui.AssetField("Font###font", value.fontHandle, AssetType::Font, Tip("(none) なら既定のフォント（メイリオ）。resources の下に置いたフォントと、Windows に入っているフォントから選べる"));
	ui.Field("Font Size###fontSize", value.fontSize, AtLeast(1.0f).Tip("文字の大きさ（ピクセル。ゲームの画面のつもりで。全角の1文字がだいたいこの幅）"));
	ui.ColorField("Color###color", value.color);
	ui.Field("Align###align", value.align, Tip("何行かあるとき、どちらの端にそろえるか"));
	ui.Field("Anchor###anchor", value.anchor, Tip("画面のどこから位置を数えるか（位置は Transform の Translation の x・y。ピクセルで、上が＋）"));
	ui.Field("Pivot###pivot", value.pivot, Tip("文字のかたまりのどこを位置に合わせるか（0,0=左下 0,1=左上 1,1=右上）。画面の左上に置くなら、Anchor を TopLeft・Pivot を 0,1"));
	ui.Field("Line Spacing###lineSpacing", value.lineSpacing, AtLeast(0.0f).Tip("行の間の倍率（1 でフォントの決めた間。小さくすると詰まる）"));
	ui.Field("Letter Spacing###letterSpacing", value.letterSpacing, Tip("文字と文字の間に足す（ピクセル。マイナスで詰める）"));
	ui.Field("Order###order", value.order, Tip("重ねる順（大きいほど手前。Image の Order とも比べる）"));
	ui.Field("Blend###blend", value.blend, Tip("Normal：透明な所が透ける / Add：足して光る"));
	ui.Separator("Outline");
	ui.Field("Outline Width###outlineWidth", value.outlineWidth, AtLeast(0.0f).Tip("縁取りの太さ（ピクセル。0 なら無し。文字の大きさの 1/6 くらいで止まる）"));
	ui.ColorField("Outline Color###outlineColor", value.outlineColor);
	ui.Separator("Shadow");
	ui.Field("Shadow###shadow", value.shadow, Tip("影を付ける（同じ文字を、ずらして影の色で先に描く）"));
	ui.Field("Shadow Offset###shadowOffset", value.shadowOffset, Tip("影のずれ（ピクセル。右下なら x が＋、y が−）"));
	ui.ColorField("Shadow Color###shadowColor", value.shadowColor);
	ui.Separator("Glow");
	ui.Field("Glow Width###glowWidth", value.glowWidth, AtLeast(0.0f).Tip("光（グロー）の広さ（ピクセル。縁取りの外へ、離れるほど薄く。0 なら無し）"));
	ui.ColorField("Glow Color###glowColor", value.glowColor);
	ui.Separator("Shine");
	ui.Field("Shine###shine", value.shine, Tip("斜めの光が、文字の上を左から右へ通る（キラッと光る）"));
	ui.ColorField("Shine Color###shineColor", value.shineColor);
	ui.Field("Shine Width###shineWidth", value.shineWidth, AtLeast(0.0f).Tip("光の帯の半分の幅（ピクセル）"));
	ui.Field("Shine Duration###shineDuration", value.shineDuration, AtLeast(0.01f).Tip("光が端から端まで通る秒数"));
	ui.Field("Shine Interval###shineInterval", value.shineInterval, AtLeast(0.0f).Tip("光が通ってから、次に通るまでの秒数"));
	ui.Separator("Motion");
	ui.Field("Motion###motion", value.motion, Tip("文字1つずつの動き（Wave：波 / Bounce：跳ねる / Shake：震える）"));
	ui.Field("Amount###motionAmount", value.motionAmount, Tip("動く大きさ（ピクセル）"));
	ui.Field("Speed###motionSpeed", value.motionSpeed, AtLeast(0.0f).Tip("動く速さ（1秒に何回）"));
	ui.Field("Spread###motionSpread", value.motionSpread, Tip("隣の文字との、動きのずれ（1回の動きの割合。0.1 なら隣は 1/10 回分遅れる。0 なら全部一緒）"));
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void TextRenderSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<TextRenderState>();

	// Text の置き場所・保存・Inspector を登録する。見せ方は DescribeText 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<TextComponent>();
	ComponentSerializer::Register<TextComponent>("Text", &DescribeText);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<TextComponent>>("Text", "UI", &DescribeText));
}

void TextRenderSystem::Release() { state.reset(); }

//=============================================================================
// 描く（停止中も）
//=============================================================================
void TextRenderSystem::Draw() {
	HTT_ASSERT_MSG(state != nullptr, "TextRenderSystem::Initialize()を先に呼んでください");
	// 演出の時計を進める（一時停止・ヒットストップでも止めない時間。停止中のエディタでも動いて見える）
	state->time += Time::GetUnscaleDeltaTime();
	const float time = state->time;
	EntityManager::ForEach<TextComponent>([time](Handle<Entity> entity, const TextComponent& text) {
		if (!text.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		const uint32_t font = ResolveFont(text.fontHandle);
		if (transform == nullptr || font == 0) {
			return;
		}
		// 親までの Alpha Group を掛ける（文字・縁取り・影・光の色の a に。0 なら描かない）
		const float groupAlpha = SpriteRenderSystem::CalcGroupAlpha(entity);
		if (groupAlpha <= 0.0f) {
			return;
		}
		TextLayout& layout = state->layout;
		LayoutText(text, font, layout);
		if (layout.glyphs.empty()) {
			return; // 空・空白だけ
		}

		Renderer::Text2dConfig config;
		config.fontTextureIndex = FontManager::GetAtlasTextureIndex(font);
		config.outlineWidth = ToSdfWidth(text.outlineWidth, text.fontSize);
		config.blendMode = text.blend;
		UIMaskSystem::MakeStep(entity, text.order, UIDrawKind::Text).ApplyTo(config); // Image・図形とも Order の順に重なる。マスクの中なら切り抜く
		config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない（影も一緒）
		// --- 影：同じ文字を、ずらして影の色で先に描く（縁取りの所も影にする。同じ Order なので、積んだ順＝影が奥）---
		if (text.shadow) {
			config.color = MultiplyAlpha(text.shadowColor, groupAlpha);
			config.outlineColor = config.color;
			Renderer::DrawText2d(config, MakeVertices(layout, text, transform->worldMatrix, text.shadowOffset, time));
		}
		// --- 文字（光・斜めの光は、影には付けない）---
		config.color = MultiplyAlpha(text.color, groupAlpha);
		config.outlineColor = MultiplyAlpha(text.outlineColor, groupAlpha);
		// 光は縁取りの外へ広げるので、縁取りと合わせて上限まで
		config.glowWidth = (std::min)(ToSdfWidth(text.glowWidth, text.fontSize), kMaxOutlineWidth - config.outlineWidth);
		config.glowColor = MultiplyAlpha(text.glowColor, groupAlpha);
		if (text.shine) {
			const float halfWidth = text.shineWidth / (std::max)(layout.size.x, 1.0f); // ピクセル → かたまりの幅に対する割合
			config.shineWidth = halfWidth;
			config.shinePosition = CalcShinePosition(text, halfWidth, time);
			config.shineColor = MultiplyAlpha(text.shineColor, groupAlpha);
		}
		Renderer::DrawText2d(config, MakeVertices(layout, text, transform->worldMatrix, {0.0f, 0.0f}, time));
	});
}

//=============================================================================
// ゲームから使う
//=============================================================================
Vector2 TextRenderSystem::CalcTextSize(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "TextRenderSystem::Initialize()を先に呼んでください");
	const TextComponent* text = EntityManager::Get<TextComponent>(entity);
	if (text == nullptr) {
		return {0.0f, 0.0f};
	}
	const uint32_t font = ResolveFont(text->fontHandle);
	if (font == 0) {
		return {0.0f, 0.0f};
	}
	LayoutText(*text, font, state->layout);
	return state->layout.size;
}