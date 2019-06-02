#pragma once

#include <Box2D/Common/b2Math.h>

#include <json.hpp>

#include "Quiver/Entity/Component.h"
#include "Quiver/Physics/PhysicsUtils.h"

class b2Body;
class b2Shape;
struct b2BodyDef;
struct b2FixtureDef;
struct b2Transform;

namespace qvr {

class World;
struct PhysicsComponentDef;

class PhysicsComponent final : public Component {
public:
	explicit PhysicsComponent(Entity&entity, const PhysicsComponentDef& def);

	~PhysicsComponent();

	PhysicsComponent(const PhysicsComponent&) = delete;
	PhysicsComponent(const PhysicsComponent&&) = delete;

	PhysicsComponent& operator=(const PhysicsComponent&) = delete;
	PhysicsComponent& operator=(const PhysicsComponent&&) = delete;

	nlohmann::json ToJson();

	b2Vec2 GetPosition() const;

	b2Body& GetBody() { return *mBody; }

private:
	Physics::b2BodyUniquePtr mBody;

};

}