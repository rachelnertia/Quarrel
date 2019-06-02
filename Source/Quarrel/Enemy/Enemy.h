#pragma once

#include <memory>

namespace qvr {
class Entity;
class CustomComponent;
}

std::unique_ptr<qvr::CustomComponent> CreateEnemy(qvr::Entity& entity);