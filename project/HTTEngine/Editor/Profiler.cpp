#include "HTTEngine/Editor/Profiler.h"

#include <algorithm>
#include <cfloat>

//=============================================================================
// カテゴリを取得（無ければ作る）
//=============================================================================
Profiler::CategoryProxy Profiler::Category(const std::string& name) {
	for (auto& c : categories_) {
		if (c.name == name) {
			return CategoryProxy(&c);
		}
	}
	categories_.push_back({name, {}});
	return CategoryProxy(&categories_.back());
}

//=============================================================================
// グループを取得（無ければ作る）
//=============================================================================
Profiler::GroupProxy Profiler::CategoryProxy::Group(const std::string& name) {
	for (auto& g : categoryData_->groups) {
		if (g.name == name) {
			return GroupProxy(&g);
		}
	}
	categoryData_->groups.push_back({name, {}});
	return GroupProxy(&categoryData_->groups.back());
}

//=============================================================================
// 項目を取得（無ければ作る）
//=============================================================================
Profiler::Entry& Profiler::GroupProxy::FindOrAdd(const std::string& label) {
	for (auto& e : groupData_->entries) {
		if (e.label == label) {
			return e;
		}
	}
	groupData_->entries.push_back(Entry{});
	groupData_->entries.back().label = label;
	return groupData_->entries.back();
}

//=============================================================================
// 文字列として登録
//=============================================================================
Profiler::GroupProxy& Profiler::GroupProxy::Text(const std::string& label, const std::string& value) {
	Entry& e = FindOrAdd(label);
	e.displayType = DisplayType::Value;
	e.text = value;
	return *this;
}

//=============================================================================
// バーとして登録
//=============================================================================
Profiler::GroupProxy& Profiler::GroupProxy::Bar(const std::string& label, float value, float maxValue, const std::string& unit) {
	Entry& e = FindOrAdd(label);
	e.displayType = DisplayType::Bar;
	e.value = value;
	e.maxValue = maxValue;
	e.unit = unit;
	return *this;
}

//=============================================================================
// グラフとして登録
//=============================================================================
Profiler::GroupProxy& Profiler::GroupProxy::Plot(const std::string& label, float value, float maxValue) {
	Entry& e = FindOrAdd(label);
	e.displayType = DisplayType::Plot;
	e.value = value;
	e.maxValue = maxValue;
	e.history.push_back(value);
	// 直近180フレームのみ表示
	if (e.history.size() > 180) {
		e.history.erase(e.history.begin());
	}
	return *this;
}

//=============================================================================
// 登録内容を全消去
//=============================================================================
void Profiler::Clear() { categories_.clear(); }

//=============================================================================
// 使用率から色を決める
//=============================================================================
ImVec4 Profiler::BarColor(float frac) {
	return frac < 0.8f   ? ImVec4(0.3f, 0.8f, 0.3f, 1.0f)  // 緑
	       : frac < 1.0f ? ImVec4(0.9f, 0.8f, 0.2f, 1.0f)  // 黄
	                     : ImVec4(0.9f, 0.3f, 0.3f, 1.0f); // 赤
}

//=============================================================================
// 1項目を描く
//=============================================================================
void Profiler::DrawEntry(const Entry& e) {
	switch (e.displayType) {
	// --- 数値 ---
	case DisplayType::Value:
		ImGui::Text("%-16s %s", e.label.c_str(), e.text.c_str());
		break;

	// --- バー ---
	case DisplayType::Bar: {
		float frac = (e.maxValue > 0.0f) ? e.value / e.maxValue : 0.0f;
		const std::string text = std::format("{:.2f} {}", e.value, e.unit);
		ImGui::Text("%-12s", e.label.c_str());
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, BarColor(frac));
		ImGui::ProgressBar(std::min(frac, 1.0f), ImVec2(-1, 0), text.c_str());
		ImGui::PopStyleColor();
		break;
	}

	// --- グラフ ---
	case DisplayType::Plot: {
		float frac = (e.maxValue > 0.0f) ? e.value / e.maxValue : 0.0f;
		ImGui::Text("%-16s %.2f", e.label.c_str(), e.value);
		ImGui::PushStyleColor(ImGuiCol_PlotLines, BarColor(frac));
		ImGui::PlotLines(("##" + e.label).c_str(), e.history.data(), static_cast<int>(e.history.size()), 0, nullptr, 0.0f, FLT_MAX, ImVec2(-1, 50));
		ImGui::PopStyleColor();
		break;
	}
	}
}

//=============================================================================
// 同名の子を探す（無ければ追加）
//=============================================================================
Profiler::LabelNode& Profiler::LabelNode::FindOrAddChild(const std::string& childName) {
	for (LabelNode& c : children) {
		if (c.name == childName) {
			return c;
		}
	}
	children.push_back(LabelNode{childName});
	return children.back();
}

//=============================================================================
// ラベルの '/' を辿って木を構築する
// 例: "Opaque/ModelPBR" → Opaque(枝) → ModelPBR(葉)
//=============================================================================
Profiler::LabelNode Profiler::BuildLabelTree(const std::vector<Entry>& entries) {
	LabelNode root;
	for (const Entry& e : entries) {
		LabelNode* current = &root;
		size_t start = 0;
		while (true) {
			size_t pos = e.label.find('/', start);
			bool isLast = (pos == std::string::npos);
			// この階層の名前を切り出す
			std::string part = isLast ? e.label.substr(start) : e.label.substr(start, pos - start);
			current = &current->FindOrAddChild(part);
			// 通過するノード全部に加算するので、枝は配下の合計になる
			current->sum += e.value;
			if (current->unit.empty()) {
				current->unit = e.unit;
			}
			if (isLast) {
				break;
			}
			start = pos + 1;
		}
		current->entry = &e; // 末端に実体を紐づける
	}
	return root;
}

//=============================================================================
// 木を再帰的に描く
//=============================================================================
void Profiler::DrawLabelNode(const LabelNode& node) {
	// --- 葉：従来どおりの項目表示 ---
	if (node.children.empty()) {
		if (node.entry) {
			// 表示名は末端だけにする（"Opaque/ModelPBR" ではなく "ModelPBR"）
			Entry shown = *node.entry;
			shown.label = node.name;
			DrawEntry(shown);
		}
		return;
	}
	// --- 枝：名前の右に配下の合計を出す ---
	// 第1引数をIDにして、合計値が毎フレーム変わっても開閉状態が保たれるようにする
	if (ImGui::TreeNodeEx(node.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen, "%-16s%.2f %s", node.name.c_str(), node.sum, node.unit.c_str())) {
		for (const LabelNode& child : node.children) {
			DrawLabelNode(child);
		}
		ImGui::TreePop();
	}
}

//=============================================================================
// グループ内の項目を描く
// ラベルに '/' があれば階層表示、無ければ従来どおり並べる
//=============================================================================
void Profiler::DrawEntries(const std::vector<Entry>& entries) {
	// 階層が必要かどうか判定
	bool hasPath = false;
	for (const Entry& e : entries) {
		if (e.label.find('/') != std::string::npos) {
			hasPath = true;
			break;
		}
	}
	// 従来どおり
	if (!hasPath) {
		for (const Entry& e : entries) {
			DrawEntry(e);
		}
		return;
	}
	// 階層表示
	LabelNode root = BuildLabelTree(entries);
	for (const LabelNode& child : root.children) {
		DrawLabelNode(child);
	}
}

//=============================================================================
// 描画
//=============================================================================
void Profiler::Draw(bool* open) {
	// --- 親ハブ + 内部専用ドックスペース ---
	ImGui::Begin("Profiler", open);
	ImGuiID dockId = ImGui::GetID("ProfilerDockSpace");
	ImGui::DockSpace(dockId, ImGui::GetContentRegionAvail(), ImGuiDockNodeFlags_None);
	ImGui::End();

	// ===== カテゴリごとに子ウィンドウ =====
	for (auto& cat : categories_) {
		ImGui::SetNextWindowDockID(dockId, ImGuiCond_FirstUseEver);
		ImGui::Begin(cat.name.c_str()); // "CPU" などの窓を開く

		// --- そのカテゴリ内のグループを回す ---
		for (auto& grp : cat.groups) {
			if (grp.name.empty()) {
				// 名前なしグループ = 見出し無しで直接並べる
				DrawEntries(grp.entries);
			} else if (ImGui::TreeNodeEx(grp.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
				// 名前ありグループ = Treeで折りたためる
				DrawEntries(grp.entries);
				ImGui::TreePop();
			}
		}

		ImGui::End(); // 子ウィンドウを閉じる
	}
}