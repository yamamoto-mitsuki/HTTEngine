#include "AccelerationField.h"
#include "HTTEngine/Time/EngineTime.h"
#include "HTTEngine/Collision/Collision.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"


void AccelerationField::Initialize() { 
	// デフォルトの宣言
	acceleration = {15.0f, 0.0f, 0.0f}; 
	aabb.max = {2.0f, 2.0f, 2.0f};
	// 描画用宣言
	drawConfig_.shadingType = ShadingType::Unlit;
	drawConfig_.rasterizerType = RasterizerType::Wireframe;
}

void AccelerationField::Update(Particle* particle) {
	if (Collision::IsPointInAABB((*particle).transform.translation,aabb)) {
		(*particle).velocity += acceleration * Time::GetDeltaTime();
	}
}

void AccelerationField::Draw() {
	drawConfig_.aabb = aabb;
	Renderer::DrawAABB(drawConfig_);
}