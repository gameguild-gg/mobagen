#include "FlockingRule.h"

#include "imgui.h"
#include "../utils/ImGuiExtra.h"
#include "Polygon.h"
#include "../gameobjects/Boid.h"
#include "../gameobjects/World.h"

FlockingRule::FlockingRule(const FlockingRule& toCopy){
    weight = toCopy.weight;
    debugColor = toCopy.debugColor;
    force = toCopy.force;
    isEnabled = toCopy.isEnabled;
    world = toCopy.world;
}

Vector2 FlockingRule::computeWeightedForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
    //the computed force is cached in a var
    if (isEnabled) {
        force = getBaseWeightMultiplier() * weight * computeForce(neighborhood, boid);
    }
    else
    {
        //If the rule is not enabled, return vector zero.
        force = Vector2::zero();
    }

    return force;
}

bool FlockingRule::drawImguiRule() {
    ImGui::SetCurrentContext(world->engine->imGuiContext);
    bool valueHasChanged = false;

    ImGui::SetNextItemOpen(isEnabled, ImGuiCond_Once); //Opened by default if rule active
    if (ImGui::TreeNode(getRuleName())) {
        ImguiTooltip(getRuleExplanation());

        if (ImGui::Checkbox("Enabled", &isEnabled)){
            valueHasChanged = true;
        }

        if (isEnabled) {
            if (ImGui::DragFloat("Weight##", &weight, 0.025f)){
                valueHasChanged = true;
            }

            ImGui::SameLine(); HelpMarker("Drag to change the weight's value or CTRL+Click to input a new value.");

            //Additional settings rule-dependant
            if (drawImguiRuleExtra()) {
                valueHasChanged = true;
            }
        }

        ImGui::TreePop();
    }
    else
        ImguiTooltip(getRuleExplanation());

    return valueHasChanged;
}

void FlockingRule::draw(const Boid& boid, SDL_Renderer* renderer) const {
    Polygon::DrawLine(renderer,
                      boid.transform.position,
                      boid.transform.position+force*1.5f,
                      debugColor);
}



