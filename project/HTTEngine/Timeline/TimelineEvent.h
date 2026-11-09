#pragma once
#include <string>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

/// <summary>
/// Timeline のイベント1回分（Script::OnTimelineEvent・TimelineSystem::AddEventListener に渡す）
/// </summary>
struct TimelineEvent {
	Handle<Entity> owner;  // 再生している Entity
	Handle<Entity> target; // トラックの対象の Entity（Target が空なら owner と同じ）
	std::string name;      // イベントの名前
	std::string value;     // イベントに付けた文字（使い方は受け取る側が決める）
	float time = 0.0f;     // イベントの時間（秒）
};