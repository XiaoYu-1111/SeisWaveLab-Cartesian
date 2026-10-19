// A revision broadcasts fold state to inactive tabs when they are next visited.
inline ImGuiID parameterCommandKey2D() {return ImHashStr("Seis2D.ParameterCommand",0,ImGui::GetCurrentWindow()->ID);}
inline void drawParameterGroupTools2D() {
 auto* storage=ImGui::GetStateStorage();auto command=parameterCommandKey2D();auto revision=command+1;
 if(AppUi::Widgets::SmallButton("Expand all")) {storage->SetInt(command,2);storage->SetInt(revision,storage->GetInt(revision)+1);}
 ImGui::SameLine();if(AppUi::Widgets::SmallButton("Collapse all")) {storage->SetInt(command,1);storage->SetInt(revision,storage->GetInt(revision)+1);}
 ImGui::Separator();
}
inline bool parameterGroup2D(const char* label,bool defaultOpen=true) {
 auto* storage=ImGui::GetStateStorage();auto command=parameterCommandKey2D();int revision=storage->GetInt(command+1);
 auto seen=ImHashStr(label,0,ImGui::GetID("Seis2D.GroupRevision"));
 if(storage->GetInt(seen)!=revision) {ImGui::SetNextItemOpen(storage->GetInt(command)==2,ImGuiCond_Always);storage->SetInt(seen,revision);}
 return AppUi::Widgets::CollapsingHeader(label,defaultOpen?ImGuiTreeNodeFlags_DefaultOpen:ImGuiTreeNodeFlags_None);
}
