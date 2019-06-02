#include "WorldExit.h"

#include <cstring>

#include <json.hpp>
#include <ImGui/imgui.h>
#include <spdlog/spdlog.h>

#include "Quiver/Application/WorldEditor/WorldEditor.h"
#include "Quiver/Application/Game/Game.h"
#include "Quiver/Application/MainMenu/MainMenu.h"
#include "Quiver/Entity/CustomComponent/CustomComponent.h"
#include "Quiver/Entity/Entity.h"
#include "Quiver/Misc/ImGuiHelpers.h"
#include "Quiver/World/World.h"
#include "Quiver/World/WorldContext.h"

using json = nlohmann::json;

using namespace qvr;

enum class ExitTarget
{
	World,           // Transitions the Game to a new World
	ApplicationState // Transitions the Application to a new ApplicationState
};

enum class ApplicationStateType
{
	Game,
	Editor,
	MainMenu
};

class WorldExit : public CustomComponent
{
public:
	WorldExit(Entity& entity);

	void OnBeginContact(
		Entity& other, 
		b2Fixture& myFixture, 
		b2Fixture& otherFixture) override;

	std::string GetTypeName() const override { return "WorldExit"; }

	json ToJson  () const override;
	bool FromJson(const json& j) override;

	std::unique_ptr<CustomComponentEditor> CreateEditor() override;

	friend class WorldExitEditor;

private:
	ExitTarget targetType = ExitTarget::World;
	ApplicationStateType targetApplicationState = ApplicationStateType::Game;
	std::string worldFilePath;
};

WorldExit::WorldExit(Entity& entity)
	: CustomComponent(entity)
{}

class WorldEditorCreator
{
	std::unique_ptr<World> world;
public:
	explicit WorldEditorCreator(std::unique_ptr<World> world) : world(std::move(world)) {}

	std::unique_ptr<ApplicationState> operator()(ApplicationStateContext& ctx) {
		return std::make_unique<WorldEditor>(ctx, std::move(world));
	}
};

void WorldExit::OnBeginContact(
	Entity& other, 
	b2Fixture& myFixture, 
	b2Fixture& otherFixture)
{
	if (!other.GetCustomComponent()) return;

	// TODO: Set the filter on the fixture so that it only collides with the player
	// so that we don't have to do this here. 
	// Also hurry up and add CustomComponentType IDs already!
	if (other.GetCustomComponent()->GetTypeName() != "Player") return;

	if (this->targetType == ExitTarget::World) {
		GetEntity().GetWorld().SetNextWorld(
			LoadWorld(this->worldFilePath, GetEntity().GetWorld().GetContext()));
	}
	else if (this->targetType == ExitTarget::ApplicationState) {
		switch (this->targetApplicationState) {
		case ApplicationStateType::Editor:
		{
			World::ApplicationStateCreator factory =
				WorldEditorCreator(
					LoadWorld(this->worldFilePath, GetEntity().GetWorld().GetContext()));

			GetEntity().GetWorld().SetNextApplicationState(std::move(factory));
		}
		break;
		case ApplicationStateType::Game:
		{
			World::ApplicationStateCreator factory = 
				[worldFilePath = this->worldFilePath](ApplicationStateContext& ctx) {
					return std::make_unique<qvr::Game>(
						ctx, 
						LoadWorld(worldFilePath, ctx.GetWorldContext()));
				};

			GetEntity().GetWorld().SetNextApplicationState(std::move(factory));
		}
		break;
		case ApplicationStateType::MainMenu:
		{
			GetEntity().GetWorld().SetNextApplicationState(
				[](ApplicationStateContext& ctx) {
					return std::make_unique<qvr::MainMenu>(ctx);
				});
		}
		break;
		}
	}
}

class WorldExitEditor : public CustomComponentEditorType<WorldExit>
{
public:
	WorldExitEditor(WorldExit& target) : CustomComponentEditorType(target) {}

	void GuiControls() override;
};

std::unique_ptr<CustomComponentEditor> WorldExit::CreateEditor() {
	return std::make_unique<WorldExitEditor>(*this);
}

void WorldExitEditor::GuiControls()
{
	int exitTargetIndex = (int)Target().targetType;
	
	{
		std::array<const char*, 2> ExitTargetStrings = {
			"To World",
			"To Application State"
		};

		ImGui::ListBox(
			"Target Type",
			&exitTargetIndex,
			ExitTargetStrings.data(),
			ExitTargetStrings.size());
	}

	Target().targetType = (ExitTarget)exitTargetIndex;

	if (Target().targetType == ExitTarget::ApplicationState)
	{
		int applicationStateIndex = (int)Target().targetApplicationState;
		
		{
			std::array<const char*, 3> ApplicationStateStrings = {
				"Game",
				"World Editor",
				"Main Menu"
			};

			ImGui::ListBox(
				"Target App State",
				&applicationStateIndex,
				ApplicationStateStrings.data(),
				ApplicationStateStrings.size());
		}

		Target().targetApplicationState = (ApplicationStateType)applicationStateIndex;
	}

	if (Target().targetType == ExitTarget::World ||
		Target().targetApplicationState != ApplicationStateType::MainMenu)
	{
		ImGui::InputText<64>("World File to Load", Target().worldFilePath);
	}
}

json WorldExit::ToJson() const
{
	json j;

	j["TargetType"] = (int)targetType;
	
	if (!worldFilePath.empty() &&
		(this->targetType == ExitTarget::World ||
		this->targetApplicationState != ApplicationStateType::MainMenu))
	{
		j["WorldFile"] = worldFilePath;
	}

	if (this->targetType == ExitTarget::ApplicationState) {
		j["TargetApplicationState"] = (int)targetApplicationState;
	}

	return j;
}

bool WorldExit::FromJson(const json& j)
{
	if (!j.is_object()) return false;

	targetType = (ExitTarget)j.value<int>("TargetType", 0);
	worldFilePath = j.value<std::string>("WorldFile", {});
	targetApplicationState = 
		(ApplicationStateType)j.value<int>("TargetApplicationState", 0);

	return true;
}

std::unique_ptr<CustomComponent> CreateWorldExit(Entity& entity)
{
	return std::make_unique<WorldExit>(entity);
}