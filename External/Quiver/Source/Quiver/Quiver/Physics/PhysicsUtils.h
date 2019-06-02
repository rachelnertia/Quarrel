#pragma once

#include <array>
#include <memory>

class b2Body;

namespace qvr {

using FixtureFilterBitNames = std::array<const char*, 16>;

namespace Physics {

struct b2BodyDeleter {
	void operator()(b2Body* body) const;
};

using b2BodyUniquePtr = std::unique_ptr<b2Body, b2BodyDeleter>;

static_assert(sizeof(b2BodyUniquePtr) == sizeof(b2Body*), "Oh no!");

}

}