//
// Created by Alexandre Tolstenko Nogueira on 2023.04.04.
//

#include "TopBar.h"
#include "imgui.h"

void TopBar::OnGui(ImGuiContext* context) {
  ImGui::SetCurrentContext(context);
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New", "Ctrl+N")) {
      }
      if (ImGui::MenuItem("Open", "Ctrl+O")) {
      }
      if (ImGui::MenuItem("Save", "Ctrl+S")) {
      }
      if (ImGui::MenuItem("Save As", "Ctrl+Shift+S")) {
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
      }
      if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {
      }  // Disabled item
      ImGui::Separator();
      if (ImGui::MenuItem("Cut", "Ctrl+X")) {
      }
      if (ImGui::MenuItem("Copy", "Ctrl+C")) {
      }
      if (ImGui::MenuItem("Paste", "Ctrl+V")) {
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Show/Hide Top Bar", "Ctrl+T")) {
      }
      if (ImGui::MenuItem("Show/Hide Left Bar", "Ctrl+L")) {
      }
      if (ImGui::MenuItem("Show/Hide Right Bar", "Ctrl+R")) {
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About")) {
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();

    ImGui::ShowDemoWindow(&enabled);
  }
}
