#include <chrono>
#include <iostream>

#include <Box2D/Common/b2Math.h>
#include <Box2D/Collision/Shapes/b2CircleShape.h>
#include <Box2D/Dynamics/b2Body.h>
#include <Box2D/Dynamics/b2Fixture.h>
#include <Box2D/Dynamics/b2World.h>
#include <Box2D/Dynamics/b2WorldCallbacks.h>
#include <Box2D/Dynamics/Contacts/b2Contact.h>
#include <ImGui/imgui.h>
#include <json.hpp>
#include <optional.hpp>
#include <spdlog/fmt/ostr.h>

#include "Quiver/Animation/AnimationLibrary.h"
#include "Quiver/Animation/AnimationLibraryGui.h"
#include "Quiver/Entity/AudioComponent/AudioComponent.h"
#include "Quiver/Entity/CustomComponent/CustomComponent.h"
#include "Quiver/Entity/PhysicsComponent/PhysicsComponent.h"
#include "Quiver/Entity/RenderComponent/RenderComponent.h"
#include "Quiver/Entity/Entity.h"
#include "Quiver/Misc/ImGuiHelpers.h"
#include "Quiver/Misc/Logging.h"
#include "Quiver/World/World.h"

#include "Damage.h"
#include "Effects.h"
#include "FirePropagation.h"
#include "Misc/GuiUtils.h"
#include "Misc/Utils.h"
#include "Player/CrossbowBolt.h"

using namespace qvr;

namespace
{

using json = nlohmann::json;

using namespace std::chrono;

class EnemyProjectile : public qvr::CustomComponent
{
public:
	EnemyProjectile(qvr::Entity& entity) : CustomComponent(entity) {}

	void OnBeginContact(
		qvr::Entity& other, 
		b2Fixture& myFixture, 
		b2Fixture& otherFixture) override
	{
		this->SetRemoveFlag(true);
	}

	std::string GetTypeName() const override { return "EnemyProjectile"; }
};

Entity* MakeProjectile(
	World& world,
	const b2Vec2& position,
	const b2Vec2& aimDir,
	const float speed,
	const b2Vec2& inheritedVelocity,
	const sf::Color& color)
{
	const float radius = 0.1f;

	const b2CircleShape shape = [=]()
	{
		b2CircleShape shape;
		shape.m_radius = radius;
		return shape;
	}();

	Entity* projectile = world.CreateEntity(shape,
		position + aimDir,
		b2Atan2(aimDir.y, aimDir.x) - (b2_pi * 0.5f));

	if (projectile == nullptr) {
		return nullptr;
	}

	// Set up body.
	{
		b2Body& projBody = projectile->GetPhysics()->GetBody();
		projBody.SetType(b2_dynamicBody);
		projBody.SetLinearVelocity((speed * aimDir) + inheritedVelocity);
		projBody.SetBullet(true);

		b2Fixture* fixture = projBody.GetFixtureList();
		b2Filter filterData = fixture->GetFilterData();
		filterData.categoryBits |= FixtureFilterCategories::Projectile;
		fixture->SetFilterData(filterData);
	}

	// Set up RenderComponent.
	{
		projectile->AddGraphics();
		RenderComponent* projRenderComp = projectile->GetGraphics();

		projRenderComp->SetDetached(true);

		projRenderComp->SetHeight(radius * 3);
		projRenderComp->SetSpriteRadius(radius);
		projRenderComp->SetGroundOffset(0.5f);

		projRenderComp->SetColor(color);
	}

	// Set up CustomComponent
	projectile->AddCustomComponent(std::make_unique<EnemyProjectile>(*projectile));

	return projectile;
}

class Enemy : public CustomComponent
{
public:
	Enemy(Entity& entity);

	std::string GetTypeName() const override { return "Enemy"; }

	void OnStep(const std::chrono::duration<float> timestep) override;

	void OnBeginContact(
		Entity& other, 
		b2Fixture& myFixture, 
		b2Fixture& otherFixture) override;
	void OnEndContact(
		Entity& other, 
		b2Fixture& myFixture, 
		b2Fixture& otherFixture) override;

	std::unique_ptr<CustomComponentEditor> CreateEditor() override;

	nlohmann::json ToJson() const override;

	bool FromJson(const nlohmann::json& j) override;

	friend class EnemyEditor;

private:
	bool CanShoot() const;

	void Shoot(const b2Vec2& target);

	void SetAnimation(
		const AnimationId animationId, 
		AnimatorRepeatSetting repeatSetting = AnimatorRepeatSetting::Forever);
	void SetAnimation(std::initializer_list<AnimatorStartSetting> animChain);

	DamageCount m_Damage = DamageCount(10);

	enum class Awakeness {
		None,
		Awakening,
		Awake
	};

	Awakeness m_Awakeness = Awakeness::None;

	bool IsAwake() const {
		return m_Awakeness == Awakeness::Awake;
	}

	World::TimePoint m_LastShootTime = 0s;

	AnimationId m_AwakeAnim = AnimationId::Invalid;
	AnimationId m_RunAnim = AnimationId::Invalid;
	AnimationId m_ShootAnim = AnimationId::Invalid;
	AnimationId m_StandAnim = AnimationId::Invalid;
	AnimationId m_DieAnim = AnimationId::Invalid;

	b2Fixture* m_Sensor;

	EntityRef m_Target;

	ActiveEffectSet m_ActiveEffects;

	FiresInContact m_FiresInContact;
};

Enemy::Enemy(Entity& entity)
	: CustomComponent(entity)
{
	SetCategoryBits(
		*GetEntity().GetPhysics()->GetBody().GetFixtureList(),
		FixtureFilterCategories::Enemy);

	// Create sensor fixture.
	b2FixtureDef fixtureDef;
	b2CircleShape circleShape = CreateCircleShape(5.0f);
	fixtureDef.shape = &circleShape;
	fixtureDef.isSensor = true;
	fixtureDef.filter.categoryBits = FixtureFilterCategories::Sensor;
	// Only collide with Players.
	fixtureDef.filter.maskBits = FixtureFilterCategories::Player;
	m_Sensor = GetEntity().GetPhysics()->GetBody().CreateFixture(&fixtureDef);
}

void Enemy::OnBeginContact(
	Entity& other, 
	b2Fixture& myFixture, 
	b2Fixture& otherFixture)
{
	auto log = qvr::GetConsoleLogger();
	const char* logCtx = "Enemy::OnBeginContact:";

	bool wakeUp = false;

	if (&myFixture == m_Sensor)
	{
		log->debug("{} Player entered sensor.", logCtx);

		m_Target = EntityRef(other);

		wakeUp = true;
	}
	else if (IsCrossbowBolt(otherFixture))
	{
		HandleContactWithCrossbowBolt(other, m_Damage);
		HandleContactWithCrossbowBolt(other, m_ActiveEffects);

		EntityRef shooter = GetCrossbowBoltFirer(other);

		if (shooter.Get()) {
			m_Target = shooter;
		}

		wakeUp = true;
	}
	else
	{
		::OnBeginContact(m_FiresInContact, otherFixture);
	}

	if (wakeUp) {
		if (m_Awakeness == Awakeness::None) {
			m_Awakeness = Awakeness::Awakening;
		}
	}
}

void Enemy::OnEndContact(
	Entity& other, 
	b2Fixture& myFixture,
	b2Fixture& otherFixture)
{
	auto log = qvr::GetConsoleLogger();
	const char* logCtx = "Enemy::OnEndContact:";

	if (&myFixture == m_Sensor)
	{
		log->debug("{} Player left sensor.", logCtx);
	}

	::OnEndContact(m_FiresInContact, otherFixture);
}

void Enemy::OnStep(const std::chrono::duration<float> timestep)
{
	auto log = GetConsoleLogger();

	const auto logCtx = "Enemy::OnStep:";

	ApplyFires(m_FiresInContact, m_ActiveEffects);

	for (auto& effect : m_ActiveEffects.container) {
		if (UpdateEffect(effect, timestep)) {
			ApplyEffect(effect, m_Damage);
		}
		ApplyEffect(effect, *GetEntity().GetGraphics());
	}

	RemoveExpiredEffects(m_ActiveEffects);

	if (HasExceededLimit(m_Damage))
	{
		log->debug("{} Dying - received {}/{} damage", logCtx, m_Damage.damage, m_Damage.max);
		SetAnimation(m_DieAnim, AnimatorRepeatSetting::Never);
		GetEntity().GetPhysics()->GetBody().DestroyFixture(m_Sensor);
		// Remove the CustomComponent, but not the Entity.
		GetEntity().AddCustomComponent(nullptr);
		// It's super important that this method immediately return after AddCustomComponent!
		return;
	}

	if (m_Awakeness == Awakeness::Awakening) {
		const AnimatorCollection& animSystem = GetEntity().GetWorld().GetAnimators();
		const AnimatorId animator = GetEntity().GetGraphics()->GetAnimatorId();
		if ((!animSystem.Exists(animator)) ||
			(animSystem.GetAnimation(animator) != m_AwakeAnim))
		{
			SetAnimation(
				{
					{ m_AwakeAnim, AnimatorRepeatSetting::Never },
					{ m_StandAnim, AnimatorRepeatSetting::Forever }
				});
		}

		m_Awakeness = Awakeness::Awake;
	}

	if (const Entity* player = m_Target.Get())
	{
		// Check that we have LOS.
		if (const auto visiblePlayerPosition =
			RayCastToFindPlayer(
				*GetEntity().GetWorld().GetPhysicsWorld(),
				GetEntity().GetPhysics()->GetPosition(),
				player->GetPhysics()->GetPosition()))
		{
			if (CanShoot())
			{
				log->debug("{} Firing at position ({}, {})!",
					logCtx,
					visiblePlayerPosition->x,
					visiblePlayerPosition->y);

				Shoot(*visiblePlayerPosition);
			}
		}
	}
}

bool Enemy::CanShoot() const
{
	{
		const AnimationId anim = GetCurrentAnimation(GetEntity());

		if (anim == m_AwakeAnim ||
			anim == m_ShootAnim)
		{
			return false;
		}
	}

	return GetEntity().GetWorld().GetTime() - m_LastShootTime > 1.0s;
}

void Enemy::Shoot(const b2Vec2& target)
{
	m_LastShootTime = GetEntity().GetWorld().GetTime();
	
	SetAnimation(
		{
			{ m_ShootAnim, AnimatorRepeatSetting::Never },
			{ m_StandAnim, AnimatorRepeatSetting::Forever }
		});
	
	if (!GetEntity().GetAudio())
	{
		GetEntity().AddAudio();
	}

	GetEntity().GetAudio()->SetSound("audio/crossbow_shoot.wav");

	const auto position = GetEntity().GetPhysics()->GetPosition();
	auto direction = target - position;
	direction.Normalize();

	MakeProjectile(
		GetEntity().GetWorld(),
		position,
		direction,
		20.0f,
		b2Vec2_zero,
		sf::Color::Red);
}

void Enemy::SetAnimation(std::initializer_list<AnimatorStartSetting> animChain)
{
	auto log = GetConsoleLogger();
	const char* logCtx = "Enemy::SetAnimation:";

	assert(animChain.size() > 0);

	const AnimatorStartSetting first = *animChain.begin();

	Enemy::SetAnimation(first.m_AnimationId, first.m_RepeatSetting);

	const AnimatorId animator = GetEntity().GetGraphics()->GetAnimatorId();

	if (animChain.size() > 1) {
		log->debug("{} Queuing {} Animations...", logCtx, animChain.size() - 1);
	}

	for (auto it = animChain.begin() + 1; it != animChain.end(); ++it)
	{
		if (!GetEntity().GetWorld().GetAnimators().QueueAnimation(animator, *it)) {
			log->warn("{} Couldn't queue Animation {}", logCtx, it->m_AnimationId);
			continue;
		}
		log->debug("{} Queued Animation {}", logCtx, it->m_AnimationId);
	}
}

void Enemy::SetAnimation(const AnimationId animationId, const AnimatorRepeatSetting repeatSetting)
{
	auto log = GetConsoleLogger();
	const char* logCtx = "Enemy::SetAnimation:";

	if (animationId != AnimationId::Invalid)
	{
		if (!GetEntity().GetGraphics()->SetAnimation(animationId, repeatSetting))
		{
			log->warn("{} Couldn't set the RenderComponent's Animation to {}", logCtx, animationId);
		}
	}
}

class EnemyEditor : public CustomComponentEditorType<Enemy>
{
public:
	EnemyEditor(Enemy& player) : CustomComponentEditorType(player) {}

	void GuiControls() override {
		AnimatorCollection& animSystem = Target().GetEntity().GetWorld().GetAnimators();

		if (const auto animationId = PickAnimationGui("Run Animation", Target().m_RunAnim, animSystem))
		{
			Target().m_RunAnim = animationId.value();
		}

		if (const auto animationId = PickAnimationGui("Idle Animation", Target().m_StandAnim, animSystem))
		{
			Target().m_StandAnim = animationId.value();
		}

		if (const auto animationId = PickAnimationGui("Shoot Animation", Target().m_ShootAnim, animSystem))
		{
			Target().m_ShootAnim = animationId.value();
		}

		if (const auto animationId = PickAnimationGui("Die Animation", Target().m_DieAnim, animSystem))
		{
			Target().m_DieAnim = animationId.value();
		}

		if (const auto animationId = PickAnimationGui("Awake Animation", Target().m_AwakeAnim, animSystem))
		{
			Target().m_AwakeAnim = animationId.value();
		}
	}
};

std::unique_ptr<CustomComponentEditor> Enemy::CreateEditor() {
	return std::make_unique<EnemyEditor>(*this);
}

json Enemy::ToJson() const
{
	const AnimatorCollection& animSystem = GetEntity().GetWorld().GetAnimators();

	json j;

	j["RunAnim"] = AnimationToJson(animSystem, m_RunAnim);
	j["ShootAnim"] = AnimationToJson(animSystem, m_ShootAnim);
	j["IdleAnim"] = AnimationToJson(animSystem, m_StandAnim);
	j["DieAnim"] = AnimationToJson(animSystem, m_DieAnim);
	j["AwakeAnim"] = AnimationToJson(animSystem, m_AwakeAnim);

	return j;
}

bool Enemy::FromJson(const nlohmann::json& j)
{
	AnimatorCollection& animSystem = GetEntity().GetWorld().GetAnimators();

	m_RunAnim = AnimationFromJson(animSystem, j.value<json>("RunAnim", {}));
	m_ShootAnim = AnimationFromJson(animSystem, j.value<json>("ShootAnim", {}));
	m_StandAnim = AnimationFromJson(animSystem, j.value<json>("IdleAnim", {}));
	m_DieAnim = AnimationFromJson(animSystem, j.value<json>("DieAnim", {}));
	m_AwakeAnim = AnimationFromJson(animSystem, j.value<json>("AwakeAnim", {}));

	return true;
}

}

std::unique_ptr<CustomComponent> CreateEnemy(Entity& entity)
{
	return std::make_unique<Enemy>(entity);
}
