#pragma once
#include <array>
#include <string>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <string_view>


/// <summary>
/// 決まった長さまでの文字（UTF-8）。データの Component に文字を持たせるときに使う
/// <para>Component は memcpy で写せる型しか持てない（ARCHITECTURE.md 原則2）。std::string は中で別の場所のメモリを指しているので持てない。
/// これは文字を自分の中の配列に入れるので、写すだけで中身ごと写る（Undo・Prefab・Duplicate がそのまま使える）</para>
/// <para>入りきらない分は切る（日本語の1文字の途中では切らない）。N は最後の 0 も入れたバイト数（256 なら 255 バイト＝日本語で85文字くらい）</para>
/// </summary>
template<size_t N> 
class FixedString {
	static_assert(N >= 2, "FixedString は1文字以上入る大きさにしてください");

public:
	FixedString() = default;
	// "abc"・std::string・std::string_view から作れる・入れられる（text = "SCORE" や text = std::format(...) と書ける）
	// "abc" の分（const char*）も別に書くのは、"abc" → string_view → FixedString と2回変換する書き方を C++ が許さないため
	FixedString(const char* text) { Assign(text); }
	FixedString(std::string_view text) { Assign(text); }
	FixedString& operator=(const char* text) {
		Assign(text);
		return *this;
	}
	FixedString& operator=(std::string_view text) {
		Assign(text);
		return *this;
	}

	/// <summary>
	/// 中身を入れ替える。入りきらない分は切る
	/// </summary>
	void Assign(std::string_view text) { 
		size_t length = (std::min)(text.size(), Capacity());
		// UTF-8の続きのバイト（10xxxxxx）の所で切ると、1文字で途中で切れて化ける。その文字まで時を戻して、文字ごと切る
		while (length > 0 && length < text.size() && (static_cast<uint8_t>(text[length]) & 0xC0) == 0x80) {
			--length;
		}
		std::copy_n(text.begin(), length, data_.begin());
		// 残りは 0 で埋める（同じ文字なら、配列の中身も全部同じになる。Undo が「変わったか」をバイトで比べても正しく見える）
		std::fill(data_.begin() + length, data_.end(), '\0');
		size_ = static_cast<uint32_t>(length);
	}

	// 中身を見る（写さない。FixedString が消えたり書き換わったりしたら使えない）
	std::string_view View() const { return {data_.data(), size_}; }
	// std::string にして返す（写す）
	std::string ToString() const { return std::string(View()); }

	size_t Size() const { return size_; }
	bool Empty() const { return size_ == 0; }
	// 入るバイト数（最後の 0 の分を除く）
	static constexpr size_t Capacity() { return N - 1; }

	bool operator==(const FixedString& other) const { return View() == other.View(); }
	bool operator==(std::string_view other) const { return View() == other; }


private:
	std::array<char, N> data_ = {}; // 文字（UTF-8）。最後はいつも0
	uint32_t size_ = 0;             // 入っているバイト数
};




