#pragma once

/// <summary>
/// カーブの計算が合っているかを、起動時に1回だけ確かめる（ComputeSelfTest と同じ形）
/// <para>代表的な値・カーブの端・キーが0個と1個・同じ時間のキー・壊れた入力を確かめる。合わなければログに出して assert で止める</para>
/// </summary>
namespace CurveSelfTest {
/// <summary>
/// 確かめる。Engine::Initialize の中で呼ぶ（Debug だけ）
/// </summary>
void Run();
} // namespace CurveSelfTest