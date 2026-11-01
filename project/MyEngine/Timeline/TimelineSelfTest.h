#pragma once

/// <summary>
/// Timeline の時間の進め方・イベントの呼ばれ方・ファイル・値の書き込みが合っているかを、起動時に1回だけ確かめる（CurveSelfTest と同じ形）
/// <para>合わなければログに出して assert で止める。Entity を一時的に作るので、EngineSystems::Initialize の後に呼ぶ</para>
/// </summary>
namespace TimelineSelfTest {
/// <summary>
/// 確かめる。Engine::Initialize の中で呼ぶ（Debug だけ）
/// </summary>
void Run();
} // namespace TimelineSelfTest