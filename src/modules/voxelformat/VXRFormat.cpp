/**
 * @file
 */

#include "VXRFormat.h"
#include "core/Common.h"
#include "core/FourCC.h"
#include "core/GLM.h"
#include "core/Log.h"
#include "app/App.h"
#include "VXMFormat.h"
#include "io/FileStream.h"
#include "io/Filesystem.h"
#include "voxel/RawVolume.h"
#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>

namespace voxel {

#define wrap(read) \
	if ((read) != 0) { \
		Log::error("Could not load vmx file: Not enough data in stream " CORE_STRINGIFY(read) " - still %i bytes left (line %i)", (int)stream.remaining(), (int)__LINE__); \
		return false; \
	}

#define wrapBool(read) \
	if ((read) != true) { \
		Log::error("Could not load vmx file: Not enough data in stream " CORE_STRINGIFY(read) " - still %i bytes left (line %i)", (int)stream.remaining(), (int)__LINE__); \
		return false; \
	}

bool VXRFormat::saveGroups(const VoxelVolumes& volumes, const io::FilePtr& file) {
	return false;
}

bool VXRFormat::loadChildVXM(const core::String& vxrPath, VoxelVolumes& volumes) {
	const io::FilePtr& file = io::filesystem()->open(vxrPath);
	if (!file->validHandle()) {
		return false;
	}
	VXMFormat f;
	return f.loadGroups(file, volumes);
}

bool VXRFormat::importChildOld(io::FileStream& stream, uint32_t version) {
	if (version <= 2) {
		char id[1024];
		wrapBool(stream.readString(sizeof(id), id, true))
	}

	uint32_t dummy;
	wrap(stream.readInt(dummy))
	char buf[1024];
	wrapBool(stream.readString(sizeof(buf), buf, true))
	uint32_t frameCount;
	wrap(stream.readInt(frameCount))
	for (uint32_t i = 0u; i < frameCount; ++i) {
		wrap(stream.readInt(dummy)) // frame index
		wrap(stream.readInt(dummy)) // ???
		if (version > 1) {
			stream.readBool();
		}
		glm::vec3 pivot;
		wrap(stream.readFloat(pivot.x))
		wrap(stream.readFloat(pivot.y))
		wrap(stream.readFloat(pivot.z))
		if (version >= 3) {
			glm::vec3 localPivot;
			wrap(stream.readFloat(localPivot.x))
			wrap(stream.readFloat(localPivot.y))
			wrap(stream.readFloat(localPivot.z))
		}
		if (version == 1) {
			float unknown;
			wrap(stream.readFloat(unknown))
			wrap(stream.readFloat(unknown))
			wrap(stream.readFloat(unknown))
			wrap(stream.readFloat(unknown))
			wrap(stream.readFloat(unknown))
			wrap(stream.readFloat(unknown))
		} else {
			glm::quat rot;
			wrap(stream.readFloat(rot.x))
			wrap(stream.readFloat(rot.y))
			wrap(stream.readFloat(rot.z))
			wrap(stream.readFloat(rot.w))
			glm::quat localRot;
			wrap(stream.readFloat(localRot.x))
			wrap(stream.readFloat(localRot.y))
			wrap(stream.readFloat(localRot.z))
			wrap(stream.readFloat(localRot.w))
		}
		float scale;
		wrap(stream.readFloat(scale))
		if (version >= 3) {
			float localScale;
			wrap(stream.readFloat(localScale))
		}
	}
	uint32_t children;
	wrap(stream.readInt(children))
	for (uint32_t i = 0u; i < children; ++i) {
		wrapBool(importChildOld(stream, version))
	}
	return true;
}

bool VXRFormat::importChild(const core::String& vxrPath, io::FileStream& stream, VoxelVolumes& volumes, uint32_t version) {
	uint32_t dummy;
	char id[1024];
	wrapBool(stream.readString(sizeof(id), id, true))
	char filename[1024];
	wrapBool(stream.readString(sizeof(filename), filename, true))
	if (filename[0] != '\0') {
		core::String modelPath = vxrPath;
		if (!modelPath.empty()) {
			modelPath.append("/");
		}
		modelPath.append(filename);
		if (!loadChildVXM(modelPath, volumes)) {
			Log::warn("Failed to attach model for %s with filename %s", id, filename);
		}
	}
	if (version <= 3) {
		return true;
	}
	if (version == 4) {
		uint32_t children = 0;
		wrap(stream.readInt(children))
		for (uint32_t i = 0; i < children; ++i) {
			wrapBool(importChild(vxrPath, stream, volumes, version))
		}
		return true;
	}
	if (version >= 6) {
		wrap(stream.readInt(dummy)) // color
		stream.readBool(); // favorite
		stream.readBool(); // visible?
	}
	stream.readBool(); // mirror x axis
	stream.readBool(); // mirror y axis
	stream.readBool(); // mirror z axis
	stream.readBool(); // ???
	stream.readBool(); // ???
	stream.readBool(); // ???
	stream.readBool(); // ???
	stream.readBool(); // ???
	wrap(stream.readInt(dummy)) // ???
	wrap(stream.readInt(dummy)) // ???
	stream.readBool(); // ???
	stream.readBool(); // ???
	stream.readBool(); // ???
	stream.readBool(); // ???
	uint32_t children = 0;
	wrap(stream.readInt(children))
	for (uint32_t i = 0; i < children; ++i) {
		wrapBool(importChild(vxrPath, stream, volumes, version))
	}
	return true;
}

bool VXRFormat::loadGroups(const io::FilePtr& file, VoxelVolumes& volumes) {
	if (!(bool)file || !file->exists()) {
		Log::error("Could not load vmr file: File doesn't exist");
		return false;
	}
	io::FileStream stream(file.get());

	uint8_t magic[4];
	wrap(stream.readByte(magic[0]))
	wrap(stream.readByte(magic[1]))
	wrap(stream.readByte(magic[2]))
	wrap(stream.readByte(magic[3]))
	if (magic[0] != 'V' || magic[1] != 'X' || magic[2] != 'R') {
		Log::error("Could not load vxr file: Invalid magic found (%c%c%c%c)",
			magic[0], magic[1], magic[2], magic[3]);
		return false;
	}
	int version;
	if (magic[3] >= '0' && magic[3] <= '9') {
		version = magic[3] - '0';
	} else {
		Log::error("Invalid version found");
		return false;
	}

	if (version < 1 || version > 7) {
		Log::error("Could not load vxr file: Unsupported version found (%i)", version);
		return false;
	}

	if (version >= 7) {
		char buf[1024];
		wrapBool(stream.readString(sizeof(buf), buf, true))
	}
	if (version <= 3) {
		uint32_t dummy;
		wrap(stream.readInt(dummy))
		uint32_t children = 0;
		wrap(stream.readInt(children))
		for (uint32_t i = 0; i < children; ++i) {
			wrapBool(importChildOld(stream, version))
		}
		uint32_t modelCount;
		wrap(stream.readInt(modelCount))
		for (uint32_t i = 0; i < modelCount; ++i) {
			char id[1024];
			wrapBool(stream.readString(sizeof(id), id, true))
			char filename[1024];
			wrapBool(stream.readString(sizeof(filename), filename, true))
			if (filename[0] != '\0') {
				core::String modelPath = file->path();
				if (!modelPath.empty()) {
					modelPath.append("/");
				}
				modelPath.append(filename);
				if (!loadChildVXM(modelPath, volumes)) {
					Log::warn("Failed to attach model for %s with filename %s", id, filename);
				}
			}
		}
		return true;
	}

	uint32_t children = 0;
	wrap(stream.readInt(children))
	for (uint32_t i = 0; i < children; ++i) {
		wrapBool(importChild(file->path(), stream, volumes, version))
	}

	// some files since version 6 still have stuff here

	return true;
}

#undef wrap
#undef wrapBool

}
