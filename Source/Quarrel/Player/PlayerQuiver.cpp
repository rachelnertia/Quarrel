#include "PlayerQuiver.h"

#include "External/enum_json.h"

using namespace std::chrono_literals;

using json = nlohmann::json;

auto QuarrelSlot::TakeQuarrel()
	-> std::experimental::optional<QuarrelTypeInfo>
{
	if (!CanTakeQuarrel(*this)) {
		return {};
	}

	auto cooldown = type.cooldownTime;

	cooldownTime = cooldown;
	cooldownRemaining = cooldown;

	return type;
}

auto QuarrelSlot::TakeQuarrel(const std::chrono::duration<float> cooldown) 
	-> std::experimental::optional<QuarrelTypeInfo>
{
	if (!CanTakeQuarrel(*this)) {
		return {};
	}

	cooldownTime = cooldown;
	cooldownRemaining = cooldown;

	return type;
}

void to_json(json & j, const QuarrelTypeInfo & quarrelType) {
	j = json
	{
		{ "name", quarrelType.name },
		{ "cooldownTime", quarrelType.cooldownTime.count() },
		{ "colour", quarrelType.colour },
		{ "effect", quarrelType.effect }
	};
}

void from_json(const json & j, QuarrelTypeInfo & quarrelType) {
	quarrelType.name = j.at("name").get<std::string>();
	quarrelType.cooldownTime = std::chrono::duration<float>(j.value<float>("cooldownTime", 0.5f));
	quarrelType.colour = j.at("colour").get<sf::Color>();
	quarrelType.effect = j.at("effect").get<CrossbowBoltEffect>();
}

auto TakeQuarrel(PlayerQuiver& quiver, const int slotIndex) -> OptionalQuarrelType
{
	assert(slotIndex >= 0);
	assert(slotIndex < PlayerQuiver::MaxEquippedQuarrelTypes);

	if (!quiver.quarrelSlots[slotIndex].has_value()) {
		return {};
	}

	return quiver.quarrelSlots[slotIndex]->TakeQuarrel();
}

void PutQuarrelBack(PlayerQuiver& quiver, const QuarrelTypeInfo& quarrel)
{
	for (auto& slot : quiver.quarrelSlots)
	{
		if (!slot.has_value()) continue;
		if (slot->type.effect != quarrel.effect) continue;

		slot->ResetCooldown();

		return;
	}
}

void to_json(json& j, PlayerQuiver const& quiver) {
	for (auto& slot : quiver.quarrelSlots) {
		json slotJson;
		if (slot.has_value()) {
			slotJson["quarrelType"] = slot->type;
		}
		j.push_back(slotJson);
	}
}

void from_json(json const& j, PlayerQuiver & quiver) {
	if (!j.is_array()) return;

	int slotIndex = 0;
	for (auto& element : j) {
		if (!element.empty()) {
			quiver.quarrelSlots[slotIndex] = QuarrelSlot(QuarrelTypeInfo(element.at("quarrelType")));
		}
		slotIndex++;
	}
}