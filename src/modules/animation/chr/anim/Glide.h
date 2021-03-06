/**
 * @file
 */

#pragma once

#include "animation/chr/CharacterSkeleton.h"

extern "C" void animation_chr_glide_update(double animTime, animation::CharacterSkeleton* skeleton, const animation::CharacterSkeletonAttribute* skeletonAttr);
