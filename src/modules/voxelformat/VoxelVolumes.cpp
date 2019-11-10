/**
 * @file
 */

#include "VoxelVolumes.h"
#include "voxel/VolumeMerger.h"
#include <vector>
#include <utility>

namespace voxel {

VoxelVolume::VoxelVolume(voxel::RawVolume* _volume, const std::string& _name, bool _visible) :
        volume(_volume), name(_name), visible(_visible) {
    if (volume != nullptr) {
        pivot = volume->region().getCentre();
    } else {
        pivot = glm::zero<glm::ivec3>();
    }
}
VoxelVolume::VoxelVolume(voxel::RawVolume* _volume, const std::string& _name, bool _visible, const glm::ivec3& _pivot) :
        volume(_volume), name(_name), visible(_visible), pivot(_pivot) {
}

VoxelVolumes::~VoxelVolumes() {
    volumes.clear();
}

voxel::RawVolume* VoxelVolumes::merge() const {
    if (volumes.empty()) {
        return nullptr;
    }
    if (volumes.size() == 1) {
        return new RawVolume(volumes[0].volume);
    }
    std::vector<const RawVolume*> rawVolumes;
    rawVolumes.reserve(volumes.size());
    for (const auto& v : volumes) {
        if (v.volume == nullptr) {
            continue;
        }
        rawVolumes.push_back(v.volume);
    }
    if (rawVolumes.empty()) {
        return nullptr;
    }
    return ::voxel::merge(rawVolumes);
}

}