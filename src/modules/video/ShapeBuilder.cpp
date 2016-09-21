#include "ShapeBuilder.h"

namespace video {

// TODO: convert everything into triangles - if you wanna have lines, use glPolygonMode

void ShapeBuilder::aabb(const core::AABB<float>& aabb) {
	static const glm::vec3 vecs[8] = {
		glm::vec3(-1.0f,  1.0f,  1.0f), glm::vec3(-1.0f, -1.0f,  1.0f),
		glm::vec3( 1.0f,  1.0f,  1.0f), glm::vec3( 1.0f, -1.0f,  1.0f),
		glm::vec3(-1.0f,  1.0f, -1.0f), glm::vec3(-1.0f, -1.0f, -1.0f),
		glm::vec3( 1.0f,  1.0f, -1.0f), glm::vec3( 1.0f, -1.0f, -1.0f)
	};
	reserve(SDL_arraysize(vecs));
	const glm::vec3& width = aabb.getWidth() / 2.0f;
	const glm::vec3& center = aabb.getCenter();
	for (size_t i = 0; i < SDL_arraysize(vecs); ++i) {
		addVertex(vecs[i] * width + center, glm::zero<glm::vec2>(), glm::zero<glm::vec3>());
	}

	// front
	addIndex(0);
	addIndex(1);

	addIndex(1);
	addIndex(3);

	addIndex(3);
	addIndex(2);

	addIndex(2);
	addIndex(0);

	// back
	addIndex(4);
	addIndex(5);

	addIndex(5);
	addIndex(7);

	addIndex(7);
	addIndex(6);

	addIndex(6);
	addIndex(4);

	// connections
	addIndex(0);
	addIndex(4);

	addIndex(2);
	addIndex(6);

	addIndex(1);
	addIndex(5);

	addIndex(3);
	addIndex(7);
}

void ShapeBuilder::frustum(const Camera& camera, int splitFrustum) {
	glm::vec3 out[video::FRUSTUM_VERTICES_MAX];
	uint32_t indices[video::FRUSTUM_VERTICES_MAX * 3];
	camera.frustumCorners(out, indices);

	const int targetLineVertices = camera.rotationType() == CameraRotationType::Target ? 2 : 0;

	if (splitFrustum == -42) {
		int indexOffset = 0;
		float planes[splitFrustum * 2];

		camera.sliceFrustum(planes, SDL_arraysize(planes), splitFrustum);

		reserve(video::FRUSTUM_VERTICES_MAX * splitFrustum + targetLineVertices);

		for (int s = 0; s < splitFrustum; ++s) {
			const float near = planes[s * 2 + 0];
			const float far = planes[s * 2 + 1];
			Log::info("split at %f:%f (%i)", near, far, s);
			camera.splitFrustum(near, far, out);

			for (size_t i = 0; i < SDL_arraysize(out); ++i) {
				addVertex(out[i], glm::zero<glm::vec2>(), glm::zero<glm::vec3>());
			}

			for (size_t i = 0; i < SDL_arraysize(indices); ++i) {
				addIndex(indexOffset + indices[i]);
			}
			indexOffset += video::FRUSTUM_VERTICES_MAX;
		}
	} else {
		reserve(video::FRUSTUM_VERTICES_MAX + targetLineVertices);

		for (size_t i = 0; i < SDL_arraysize(out); ++i) {
			addVertex(out[i], glm::zero<glm::vec2>(), glm::zero<glm::vec3>());
		}

		for (size_t i = 0; i < SDL_arraysize(indices); ++i) {
			addIndex(indices[i]);
		}
	}

	if (camera.rotationType() == CameraRotationType::Target) {
		setColor(core::Color::Green);
		addVertex(camera.position(), glm::zero<glm::vec2>(), glm::zero<glm::vec3>());
		addVertex(camera.target(), glm::zero<glm::vec2>(), glm::zero<glm::vec3>());
		addIndex(video::FRUSTUM_VERTICES_MAX + 0);
		addIndex(video::FRUSTUM_VERTICES_MAX + 1);
	}
}

void ShapeBuilder::axis(float scale) {
	const glm::vec3 verticesAxis[] = {
			 glm::vec3( 0.0f,   0.0f,   0.0f),
			 glm::vec3(scale,   0.0f,   0.0f),
			 glm::vec3( 0.0f,   0.0f,   0.0f),
			 glm::vec3( 0.0f,  scale,   0.0f),
			 glm::vec3( 0.0f,   0.0f,   0.0f),
			 glm::vec3( 0.0f,   0.0f,  scale)};

	setColor(core::Color::Red);
	addVertex(verticesAxis[0], glm::zero<glm::vec2>(), glm::zero<glm::vec3>());
	addVertex(verticesAxis[1], glm::zero<glm::vec2>(), glm::zero<glm::vec3>());
	setColor(core::Color::Green);
	addVertex(verticesAxis[2], glm::zero<glm::vec2>(), glm::zero<glm::vec3>());
	addVertex(verticesAxis[3], glm::zero<glm::vec2>(), glm::zero<glm::vec3>());
	setColor(core::Color::Blue);
	addVertex(verticesAxis[4], glm::zero<glm::vec2>(), glm::zero<glm::vec3>());
	addVertex(verticesAxis[5], glm::zero<glm::vec2>(), glm::zero<glm::vec3>());

	for (size_t i = 0; i < SDL_arraysize(verticesAxis); ++i) {
		addIndex(i);
	}
}

void ShapeBuilder::plane(uint32_t tesselation, float scale) {
	static const glm::vec2 uv0(0.0f, 1.0f);
	static const glm::vec2 uv1(1.0f, 0.0f);
	static const glm::vec2 uv2(0.0f, 0.0f);
	static const glm::vec2 meshBounds(uv1.x - uv0.x, uv2.y - uv0.y);
	static const glm::vec2 uvBounds(uv1.x - uv0.y, uv0.y - uv2.y);
	static const glm::vec2 uvPos = uv2;
	static const glm::vec2 anchorOffset(meshBounds.x / 2, meshBounds.y / 2);

	const uint32_t strucWidth = tesselation + 2;
	const float segmentWidth = 1.0f / (tesselation + 1);
	const float scaleX = meshBounds.x / (tesselation + 1);
	const float scaleY = meshBounds.y / (tesselation + 1);

	reserve(strucWidth * strucWidth);

	for (float y = 0.0f; y < strucWidth; ++y) {
		for (float x = 0.0f; x < strucWidth; ++x) {
			const glm::vec2 uv((x * segmentWidth * uvBounds.x) + uvPos.x, uvBounds.y - (y * segmentWidth * uvBounds.y) + uvPos.y);
			const glm::vec3 v(x * scaleX - anchorOffset.x, 0.0f, y * scaleY - anchorOffset.y);
			addVertex(v * scale, uv, glm::zero<glm::vec3>());
		}
	}

	for (size_t y = 0; y < tesselation + 1; ++y) {
		for (size_t x = 0; x < tesselation + 1; ++x) {
			_indices.emplace_back((y * strucWidth) + x);
			_indices.emplace_back((y * strucWidth) + x + 1);
			_indices.emplace_back(((y + 1) * strucWidth) + x);
			_indices.emplace_back(((y + 1) * strucWidth) + x);
			_indices.emplace_back((y * strucWidth) + x + 1);
			_indices.emplace_back(((y + 1) * strucWidth) + x + 1);
		}
	}
}

void ShapeBuilder::sphere(int numSlices, int numStacks, float radius) {
	static constexpr float pi = glm::pi<float>();
	static constexpr float twoPi = glm::two_pi<float>();
	const float du = 1.0f / numSlices;
	const float dv = 1.0f / numStacks;

	for (int stack = 0; stack <= numStacks; stack++) {
		const float stackAngle = (pi * stack) / numStacks;
		const float sinStack = glm::sin(stackAngle);
		const float cosStack = glm::cos(stackAngle);
		for (int slice = 0; slice <= numSlices; slice++) {
			const float sliceAngle = (twoPi * slice) / numSlices;
			const float sinSlice = glm::sin(sliceAngle);
			const float cosSlice = glm::cos(sliceAngle);
			const glm::vec3 norm(sinSlice * sinStack, cosSlice * sinStack, cosStack);
			const glm::vec3 pos(norm * radius);
			addVertex(pos, glm::vec2(du * slice, dv * stack), norm);
		}
	}

	// north-pole triangles
	const int startVertexIndex = 0;
	int rowA = startVertexIndex;
	int rowB = rowA + numSlices + 1;
	for (int slice = 0; slice < numSlices; slice++) {
		addIndex(rowA + slice);
		addIndex(rowB + slice);
		addIndex(rowB + slice + 1);
	}

	// stack triangles
	for (int stack = 1; stack < numStacks - 1; stack++) {
		rowA = startVertexIndex + stack * (numSlices + 1);
		rowB = rowA + numSlices + 1;
		for (int slice = 0; slice < numSlices; slice++) {
			addIndex(rowA + slice);
			addIndex(rowB + slice + 1);
			addIndex(rowA + slice + 1);

			addIndex(rowA + slice);
			addIndex(rowB + slice);
			addIndex(rowB + slice + 1);
		}
	}

	// south-pole triangles
	rowA = startVertexIndex + (numStacks - 1) * (numSlices + 1);
	rowB = rowA + numSlices + 1;
	for (int slice = 0; slice < numSlices; slice++) {
		addIndex(rowA + slice);
		addIndex(rowB + slice + 1);
		addIndex(rowA + slice + 1);
	}
}

void ShapeBuilder::shutdown() {
	clear();
}

}
