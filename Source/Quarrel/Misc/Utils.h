#pragma once

#include <functional>

#include <Box2D/Collision/Shapes/b2CircleShape.h>
#include <Box2D/Collision/Shapes/b2PolygonShape.h>
#include <Box2D/Common/b2Math.h>
#include <json.hpp>
#include <optional.hpp>

#include <Quiver/Animation/AnimationId.h>
#include <Quiver/Entity/EntityId.h>
#include <Quiver/Misc/Verify.h>
#include <Quiver/Physics/PhysicsUtils.h>

class b2Fixture;
class b2World;
struct b2AABB;

namespace qvr {
class AnimatorCollection;
class CustomComponent;
class Entity;
class World;
}

struct DamageCount;
struct ActiveEffectSet;

template <typename T>
using optional = std::experimental::optional<T>;

namespace FixtureFilterCategories
{
const uint16 Default = 1 << 0;
const uint16 Player = 1 << 1;
const uint16 Sensor = 1 << 2;
const uint16 Projectile = 1 << 3;
const uint16 CrossbowBolt = 1 << 4;
const uint16 Enemy = 1 << 5;
const uint16 Fire = 1 << 6;
const uint16 EnemyAttack = 1 << 7;
// RenderOnly is set by the engine. We shouldn't touch it.
const uint16 RenderOnly = 1 << 15;
}

inline qvr::FixtureFilterBitNames CreateFilterBitNames()
{
	qvr::FixtureFilterBitNames bitNames{};
	bitNames[0] = "Default";
	bitNames[1] = "Player";
	bitNames[2] = "Sensor";
	bitNames[3] = "Projectile";
	bitNames[4] = "CrossbowBolt";
	bitNames[5] = "Enemy";
	bitNames[6] = "Fire";
	bitNames[7] = "EnemyAttack";
	return bitNames;
}

void SetCategoryBits(b2Fixture& fixture, const uint16 categoryBits);
void SetMaskBits(b2Fixture& fixture, const uint16 maskBits);

uint16 GetCategoryBits(const b2Fixture& fixture);
uint16 GetMaskBits(const b2Fixture& fixture);

qvr::CustomComponent* GetCustomComponent(const b2Fixture* fixture);

qvr::Entity*          GetPlayerFromFixture(const b2Fixture* fixture);

std::experimental::optional<b2Vec2> QueryAABBToFindPlayer(
	const b2World& world,
	const b2AABB& aabb);

std::experimental::optional<b2Vec2> RayCastToFindPlayer(
	const b2World& world,
	const b2Vec2& rayStart,
	const b2Vec2& rayEnd);

std::experimental::optional<b2Vec2> RayCastToFindPlayer(
	const b2World& world,
	const b2Vec2& rayPos,
	const float angle,
	const float range);

inline b2Vec2 Normalize(const b2Vec2& v) {
	b2Vec2 n = v;
	n.Normalize();
	return n;
}

inline float Lerp(const float a, const float b, const float t) {
	return a + t * (b - a);
}

inline float NoEase(const float t) { return t; }

template <class T>
class TimeLerper {
	float t = 0.0f;
	float secondsToReachTarget = 0.0f;
	T startVal;
	T targetVal;
public:
	TimeLerper() = default;

	TimeLerper(const T& start, const T& target, const float seconds)
	{
		SetTarget(start, target, seconds);
	}

	void SetTarget(const T& start, const T& target, const float seconds) {
		secondsToReachTarget = std::max(seconds, 0.001f);
		targetVal = target;
		startVal = start;
		t = 0.0f;
	}

	T Update(
		const float seconds, 
		std::function<float(const float)> EasingFunc = NoEase)
	{
		if (t < 1.0f) {
			t += seconds / secondsToReachTarget;
			return startVal + EasingFunc(t) * (targetVal - startVal);
		}
		return targetVal;
	}
};

class DeltaRadians
{
	float last = 0.0f;
	float delta = 0.0f;
public:
	DeltaRadians(const float startRadians)
		: last(startRadians)
	{}

	void Update(const float currentRadians);

	float Get() const {
		return delta;
	}
};

class EntityRef
{
	qvr::World * world = nullptr;
public:
	qvr::EntityId id = qvr::EntityId(0);

	EntityRef() = default;

	EntityRef(qvr::World& world, const qvr::EntityId id) : world(&world), id(id) {}

	EntityRef(const qvr::Entity& entity);

	qvr::Entity* Get();
};

template <typename T>
bool FlagsAreSet(const T flags, const T bitfield)
{
	return (flags & bitfield) == flags;
}

inline b2CircleShape CreateCircleShape(const float radius)
{
	b2CircleShape circle;
	circle.m_radius = radius;
	return circle;
}

inline b2PolygonShape CreateRegularPolygonShape(
	const int vertexCount,
	const float radius)
{
	b2PolygonShape shape;

	if (!qvrVerify(vertexCount >= 3)) return shape;
	if (!qvrVerify(vertexCount <= 8)) return shape;

	std::array<b2Vec2, 8> points;

	points[0].x = 0.0f;
	points[1].y = radius;

	const float stepRadians = (b2_pi * 2.0f) / vertexCount;
	for (int pointIndex = 1; pointIndex < vertexCount; pointIndex++) {
		const float radians = stepRadians * pointIndex;
		points[pointIndex].x = sin(radians) * radius;
		points[pointIndex].y = cos(radians) * radius;
	}

	shape.Set(points.data(), vertexCount);

	return shape;
}

bool IsCrossbowBolt(const b2Fixture& fixture);

void HandleContactWithCrossbowBolt(
	const qvr::Entity& crossbowBoltEntity,
	DamageCount& damageCounter);
void HandleContactWithCrossbowBolt(
	const qvr::Entity& crossbowBoltEntity,
	ActiveEffectSet& activeEffects);

EntityRef GetCrossbowBoltFirer(const qvr::Entity& crossbowBoltEntity);

qvr::AnimationId GetCurrentAnimation(const qvr::Entity& entity);

nlohmann::json AnimationToJson(
	const qvr::AnimatorCollection& animators, 
	const qvr::AnimationId animationId);

qvr::AnimationId AnimationFromJson(
	const qvr::AnimatorCollection& animators, 
	const nlohmann::json& j);

template <typename T, std::size_t N>
constexpr std::size_t countof(T const (&)[N]) noexcept
{
	return N;
}

template<class T, class UnaryPredicate>
void AddOrUpdate(std::vector<T>& vec, const T& t, UnaryPredicate predicate) {
	auto foundIt = std::find_if(
		begin(vec),
		end(vec),
		predicate);

	if (foundIt != end(vec)) {
		*foundIt = t;
	}
	else {
		vec.push_back(t);
	}
}