/**
 * @file
 */

#include "Bird.h"
#include "animation/Animation.h"
#include "BirdSkeletonAttribute.h"
#include "core/Common.h"
#include "core/GLM.h"
#include "anim/Idle.h"

namespace animation {

bool Bird::initSettings(const std::string& luaString) {
	AnimationSettings settings;
	BirdSkeletonAttribute attributes;
	if (loadAnimationSettings(luaString, settings, &attributes)) {
		if (!attributes.init()) {
			return false;
		}
		_settings = settings;
		_attributes = attributes;
		return true;
	}
	Log::warn("Failed to load the Bird settings");
	return false;
}

bool Bird::initMesh(const AnimationCachePtr& cache) {
	if (!cache->getBoneModel(_settings, _vertices, _indices)) {
		Log::warn("Failed to load the models");
		return false;
	}
	return true;
}

void Bird::update(uint64_t dt, const attrib::ShadowAttributes&) {
	const float animTimeSeconds = float(dt) / 1000.0f;

	const BirdSkeleton old = _skeleton;

	switch (_anim) {
	case Animation::Idle:
		animal::bird::idle::update(_globalTimeSeconds, _skeleton, _attributes);
		break;
	default:
		break;
	}

	if (_globalTimeSeconds > 0.0f) {
		const float ticksPerSecond = 15.0f;
		_skeleton.lerp(old, glm::min(1.0f, animTimeSeconds * ticksPerSecond));
	}

	_globalTimeSeconds += animTimeSeconds;
}

}