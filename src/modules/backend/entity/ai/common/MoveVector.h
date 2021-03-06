/**
 * @file
 */
#pragma once

#include <glm/vec3.hpp>

namespace backend {

enum class MoveVectorState {
	Valid, Invalid, TargetReached
};

class MoveVector {
protected:
	const glm::vec3 _vec3;
	const float _rotation;
	MoveVectorState _state;
public:
	static const MoveVector Invalid;
	static const MoveVector TargetReached;

	MoveVector(const glm::vec3& vec3, float rotation, MoveVectorState state = MoveVectorState::Valid);

	float getOrientation(float duration) const ;

	inline MoveVectorState state() const {
		return _state;
	}

	inline bool isValid() const {
		return _state == MoveVectorState::Valid;
	}

	inline bool isTargetReached() const {
		return _state == MoveVectorState::TargetReached;
	}

	inline const glm::vec3& getVector() const {
		return _vec3;
	}

	inline glm::vec3 getVector() {
		return _vec3;
	}

	inline float getRotation() const {
		return _rotation;
	}

	inline operator glm::vec3() const {
		return _vec3;
	}

	inline operator const glm::vec3&() const {
		return _vec3;
	}

	inline operator float() const {
		return _rotation;
	}
};

}
