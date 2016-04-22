#include "PagedVolume.h"

namespace voxel {

////////////////////////////////////////////////////////////////////////////////
/// This constructor creates a volume with a fixed size which is specified as a parameter. By default this constructor will not enable paging but you can override this if desired. If you do wish to enable paging then you are required to provide the call back function (see the other PagedVolume constructor).
/// @param pPager Called by PolyVox to load and unload data on demand.
/// @param uTargetMemoryUsageInBytes The upper limit to how much memory this PagedVolume should aim to use.
/// @param uChunkSideLength The size of the chunks making up the volume. Small chunks will compress/decompress faster, but there will also be more of them meaning voxel access could be slower.
////////////////////////////////////////////////////////////////////////////////
PagedVolume::PagedVolume(Pager* pPager, uint32_t uTargetMemoryUsageInBytes, uint16_t uChunkSideLength) :
		BaseVolume<Voxel>(), m_uChunkSideLength(uChunkSideLength), m_pPager(pPager) {
	// Validation of parameters
	core_assert_msg(pPager, "You must provide a valid pager when constructing a PagedVolume");
	core_assert_msg(uTargetMemoryUsageInBytes >= 1 * 1024 * 1024, "Target memory usage is too small to be practical");
	core_assert_msg(m_uChunkSideLength != 0, "Chunk side length cannot be zero.");
	core_assert_msg(m_uChunkSideLength <= 256, "Chunk size is too large to be practical.");
	core_assert_msg(isPowerOf2(m_uChunkSideLength), "Chunk side length must be a power of two.");

	// Used to perform multiplications and divisions by bit shifting.
	m_uChunkSideLengthPower = logBase2(m_uChunkSideLength);
	// Use to perform modulo by bit operations
	m_iChunkMask = m_uChunkSideLength - 1;

	// Calculate the number of chunks based on the memory limit and the size of each chunk.
	uint32_t uChunkSizeInBytes = PagedVolume::Chunk::calculateSizeInBytes(m_uChunkSideLength);
	m_uChunkCountLimit = uTargetMemoryUsageInBytes / uChunkSizeInBytes;

	// Enforce sensible limits on the number of chunks.
	const uint32_t uMinPracticalNoOfChunks = 32; // Enough to make sure a chunks and it's neighbours can be loaded, with a few to spare.
	const uint32_t uMaxPracticalNoOfChunks = uChunkArraySize / 2; // A hash table should only become half-full to avoid too many clashes.
	if (m_uChunkCountLimit < uMinPracticalNoOfChunks) {
		::Log::warn("Requested memory usage limit of %uiMb is too low and cannot be adhered to.",
				uTargetMemoryUsageInBytes / (1024 * 1024));
	}
	m_uChunkCountLimit = std::max(m_uChunkCountLimit, uMinPracticalNoOfChunks);
	m_uChunkCountLimit = std::min(m_uChunkCountLimit, uMaxPracticalNoOfChunks);

	// Inform the user about the chosen memory configuration.
	::Log::debug("Memory usage limit for volume now set to %uiMb (%ui chunks of %uiKb each).",
			(m_uChunkCountLimit * uChunkSizeInBytes) / (1024 * 1024), m_uChunkCountLimit, uChunkSizeInBytes / 1024);
}

////////////////////////////////////////////////////////////////////////////////
/// Destroys the volume The destructor will call flushAll() to ensure that a paging volume has the chance to save it's data via the dataOverflowHandler() if desired.
////////////////////////////////////////////////////////////////////////////////
PagedVolume::~PagedVolume() {
	flushAll();
}

////////////////////////////////////////////////////////////////////////////////
/// This version of the function is provided so that the wrap mode does not need
/// to be specified as a template parameter, as it may be confusing to some users.
/// @param uXPos The @c x position of the voxel
/// @param uYPos The @c y position of the voxel
/// @param uZPos The @c z position of the voxel
/// @return The voxel value
////////////////////////////////////////////////////////////////////////////////
Voxel PagedVolume::getVoxel(int32_t uXPos, int32_t uYPos, int32_t uZPos) const {
	const int32_t chunkX = uXPos >> m_uChunkSideLengthPower;
	const int32_t chunkY = uYPos >> m_uChunkSideLengthPower;
	const int32_t chunkZ = uZPos >> m_uChunkSideLengthPower;

	const uint16_t xOffset = static_cast<uint16_t>(uXPos & m_iChunkMask);
	const uint16_t yOffset = static_cast<uint16_t>(uYPos & m_iChunkMask);
	const uint16_t zOffset = static_cast<uint16_t>(uZPos & m_iChunkMask);

	auto pChunk = canReuseLastAccessedChunk(chunkX, chunkY, chunkZ) ? m_pLastAccessedChunk : getChunk(chunkX, chunkY, chunkZ);

	return pChunk->getVoxel(xOffset, yOffset, zOffset);
}

////////////////////////////////////////////////////////////////////////////////
/// This version of the function is provided so that the wrap mode does not need
/// to be specified as a template parameter, as it may be confusing to some users.
/// @param v3dPos The 3D position of the voxel
/// @return The voxel value
////////////////////////////////////////////////////////////////////////////////
Voxel PagedVolume::getVoxel(const glm::ivec3& v3dPos) const {
	return getVoxel(v3dPos.x, v3dPos.y, v3dPos.z);
}

////////////////////////////////////////////////////////////////////////////////
/// @param uXPos the @c x position of the voxel
/// @param uYPos the @c y position of the voxel
/// @param uZPos the @c z position of the voxel
////////////////////////////////////////////////////////////////////////////////
void PagedVolume::setVoxel(int32_t uXPos, int32_t uYPos, int32_t uZPos, Voxel tValue) {
	const int32_t chunkX = uXPos >> m_uChunkSideLengthPower;
	const int32_t chunkY = uYPos >> m_uChunkSideLengthPower;
	const int32_t chunkZ = uZPos >> m_uChunkSideLengthPower;

	const uint16_t xOffset = static_cast<uint16_t>(uXPos - (chunkX << m_uChunkSideLengthPower));
	const uint16_t yOffset = static_cast<uint16_t>(uYPos - (chunkY << m_uChunkSideLengthPower));
	const uint16_t zOffset = static_cast<uint16_t>(uZPos - (chunkZ << m_uChunkSideLengthPower));

	auto pChunk = canReuseLastAccessedChunk(chunkX, chunkY, chunkZ) ? m_pLastAccessedChunk : getChunk(chunkX, chunkY, chunkZ);

	pChunk->setVoxel(xOffset, yOffset, zOffset, tValue);
}

////////////////////////////////////////////////////////////////////////////////
/// @param v3dPos the 3D position of the voxel
/// @param tValue the value to which the voxel will be set
////////////////////////////////////////////////////////////////////////////////
void PagedVolume::setVoxel(const glm::ivec3& v3dPos, Voxel tValue) {
	setVoxel(v3dPos.x, v3dPos.y, v3dPos.z, tValue);
}

////////////////////////////////////////////////////////////////////////////////
/// Note that if the memory usage limit is not large enough to support the region this function will only load part of the region. In this case it is undefined which parts will actually be loaded. If all the voxels in the given region are already loaded, this function will not do anything. Other voxels might be unloaded to make space for the new voxels.
/// @param regPrefetch The Region of voxels to prefetch into memory.
////////////////////////////////////////////////////////////////////////////////
void PagedVolume::prefetch(Region regPrefetch) {
	// Convert the start and end positions into chunk space coordinates
	const glm::ivec3& lower = regPrefetch.getLowerCorner();
	const glm::ivec3 v3dStart {lower.x >> m_uChunkSideLengthPower, lower.y >> m_uChunkSideLengthPower, lower.z >> m_uChunkSideLengthPower};

	const glm::ivec3& upper = regPrefetch.getUpperCorner();
	const glm::ivec3 v3dEnd {upper.x >> m_uChunkSideLengthPower, upper.y >> m_uChunkSideLengthPower, upper.z >> m_uChunkSideLengthPower};

	// Ensure we don't page in more chunks than the volume can hold.
	Region region(v3dStart, v3dEnd);
	uint32_t uNoOfChunks = static_cast<uint32_t>(region.getWidthInVoxels() * region.getHeightInVoxels() * region.getDepthInVoxels());
	if (uNoOfChunks > m_uChunkCountLimit) {
		::Log::warn("Attempting to prefetch more than the maximum number of chunks (this will cause thrashing).");
	}
	uNoOfChunks = std::min(uNoOfChunks, m_uChunkCountLimit);

	// Loops over the specified positions and touch the corresponding chunks.
	for (int32_t x = v3dStart.x; x <= v3dEnd.x; x++) {
		for (int32_t y = v3dStart.y; y <= v3dEnd.y; y++) {
			for (int32_t z = v3dStart.z; z <= v3dEnd.z; z++) {
				getChunk(x, y, z);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
/// Removes all voxels from memory, and calls dataOverflowHandler() to ensure the application has a chance to store the data.
////////////////////////////////////////////////////////////////////////////////
void PagedVolume::flushAll() {
	// Clear this pointer as all chunks are about to be removed.
	m_pLastAccessedChunk = nullptr;

	// Erase all the most recently used chunks.
	for (uint32_t uIndex = 0; uIndex < uChunkArraySize; uIndex++) {
		m_arrayChunks[uIndex] = nullptr;
	}
}

bool PagedVolume::canReuseLastAccessedChunk(int32_t iChunkX, int32_t iChunkY, int32_t iChunkZ) const {
	return iChunkX == m_v3dLastAccessedChunkX && iChunkY == m_v3dLastAccessedChunkY && iChunkZ == m_v3dLastAccessedChunkZ && m_pLastAccessedChunk;
}

typename PagedVolume::Chunk* PagedVolume::getChunk(int32_t uChunkX, int32_t uChunkY, int32_t uChunkZ) const {
	Chunk* pChunk = nullptr;

	// We generate a 16-bit hash here and assume this matches the range available in the chunk
	// array. The assert here is just to make sure we take care if change this in the future.
	static_assert(uChunkArraySize == 65536, "Chunk array size has changed, check if the hash calculation needs updating.");
	// Extract the lower five bits from each position component.
	const uint32_t uChunkXLowerBits = static_cast<uint32_t>(uChunkX & 0x1F);
	const uint32_t uChunkYLowerBits = static_cast<uint32_t>(uChunkY & 0x1F);
	const uint32_t uChunkZLowerBits = static_cast<uint32_t>(uChunkZ & 0x1F);
	// Combine then to form a 15-bit hash of the position. Also shift by one to spread the values out in the whole 16-bit space.
	const uint32_t iPosisionHash = (((uChunkXLowerBits)) | ((uChunkYLowerBits) << 5) | ((uChunkZLowerBits) << 10) << 1);

	// Starting at the position indicated by the hash, and then search through the whole array looking for a chunk with the correct
	// position. In most cases we expect to find it in the first place we look. Note that this algorithm is slow in the case that
	// the chunk is not found because the whole array has to be searched, but in this case we are going to have to page the data in
	// from an external source which is likely to be slow anyway.
	uint32_t iIndex = iPosisionHash;
	do {
		if (m_arrayChunks[iIndex]) {
			glm::ivec3& entryPos = m_arrayChunks[iIndex]->m_v3dChunkSpacePosition;
			if (entryPos.x == uChunkX && entryPos.y == uChunkY && entryPos.z == uChunkZ) {
				pChunk = m_arrayChunks[iIndex].get();
				pChunk->m_uChunkLastAccessed = ++m_uTimestamper;
				break;
			}
		}

		iIndex++;
		iIndex %= uChunkArraySize;
	} while (iIndex != iPosisionHash); // Keep searching until we get back to our start position.

	// If we still haven't found the chunk then it's time to create a new one and page it in from disk.
	if (!pChunk) {
		// The chunk was not found so we will create a new one.
		glm::ivec3 v3dChunkPos(uChunkX, uChunkY, uChunkZ);
		pChunk = new PagedVolume::Chunk(v3dChunkPos, m_uChunkSideLength, m_pPager);
		pChunk->m_uChunkLastAccessed = ++m_uTimestamper; // Important, as we may soon delete the oldest chunk

		// Store the chunk at the appropriate place in out chunk array. Ideally this place is
		// given by the hash, otherwise we do a linear search for the next available location
		// We always expect to find a free place because we aim to keep the array only half full.
		uint32_t iIndex = iPosisionHash;
		bool bInsertedSucessfully = false;
		do {
			if (m_arrayChunks[iIndex] == nullptr) {
				m_arrayChunks[iIndex] = std::move(std::unique_ptr<Chunk>(pChunk));
				bInsertedSucessfully = true;
				break;
			}

			iIndex++;
			iIndex %= uChunkArraySize;
		} while (iIndex != iPosisionHash); // Keep searching until we get back to our start position.

		// This should never really happen unless we are failing to keep our number of active chunks
		// significantly under the target amount. Perhaps if chunks are 'pinned' for threading purposes?
		core_assert_msg(bInsertedSucessfully, "No space in chunk array for new chunk.");

		// As we have added a chunk we may have exceeded our target chunk limit. Search through the array to
		// determine how many chunks we have, as well as finding the oldest timestamp. Note that this is potentially
		// wasteful and we may instead wish to track how many chunks we have and/or delete a chunk at random (or
		// just check e.g. 10 and delete the oldest of those) but we'll see if this is a bottleneck first. Paging
		// the data in is probably more expensive.
		uint32_t uChunkCount = 0;
		uint32_t uOldestChunkIndex = 0;
		uint32_t uOldestChunkTimestamp = std::numeric_limits<uint32_t>::max();
		for (uint32_t uIndex = 0; uIndex < uChunkArraySize; uIndex++) {
			if (m_arrayChunks[uIndex]) {
				uChunkCount++;
				if (m_arrayChunks[uIndex]->m_uChunkLastAccessed < uOldestChunkTimestamp) {
					uOldestChunkTimestamp = m_arrayChunks[uIndex]->m_uChunkLastAccessed;
					uOldestChunkIndex = uIndex;
				}
			}
		}

		// Check if we have too many chunks, and delete the oldest if so.
		if (uChunkCount > m_uChunkCountLimit) {
			m_arrayChunks[uOldestChunkIndex] = nullptr;
		}
	}

	m_pLastAccessedChunk = pChunk;
	m_v3dLastAccessedChunkX = uChunkX;
	m_v3dLastAccessedChunkY = uChunkY;
	m_v3dLastAccessedChunkZ = uChunkZ;

	return pChunk;
}

////////////////////////////////////////////////////////////////////////////////
/// Calculate the memory usage of the volume.
////////////////////////////////////////////////////////////////////////////////
uint32_t PagedVolume::calculateSizeInBytes() {
	uint32_t uChunkCount = 0;
	for (uint32_t uIndex = 0; uIndex < uChunkArraySize; uIndex++) {
		if (m_arrayChunks[uIndex]) {
			uChunkCount++;
		}
	}

	// Note: We disregard the size of the other class members as they are likely to be very small compared to the size of the
	// allocated voxel data. This also keeps the reported size as a power of two, which makes other memory calculations easier.
	return PagedVolume::Chunk::calculateSizeInBytes(m_uChunkSideLength) * uChunkCount;
}

PagedVolume::Chunk::Chunk(glm::ivec3 v3dPosition, uint16_t uSideLength, Pager* pPager) :
		m_uChunkLastAccessed(0), m_bDataModified(true), m_tData(0), m_uSideLength(0), m_uSideLengthPower(0), m_pPager(pPager), m_v3dChunkSpacePosition(v3dPosition) {
	core_assert_msg(m_pPager, "No valid pager supplied to chunk constructor.");
	core_assert_msg(uSideLength <= 256, "Chunk side length cannot be greater than 256.");

	// Compute the side length
	m_uSideLength = uSideLength;
	m_uSideLengthPower = logBase2(uSideLength);

	// Allocate the data
	const uint32_t uNoOfVoxels = m_uSideLength * m_uSideLength * m_uSideLength;
	m_tData = new Voxel[uNoOfVoxels];

	// Pass the chunk to the Pager to give it a chance to initialise it with any data
	// From the coordinates of the chunk we deduce the coordinates of the contained voxels.
	const glm::ivec3 v3dLower = m_v3dChunkSpacePosition * static_cast<int32_t>(m_uSideLength);
	const glm::ivec3 v3dUpper = v3dLower + glm::ivec3(m_uSideLength - 1, m_uSideLength - 1, m_uSideLength - 1);
	const Region reg(v3dLower, v3dUpper);

	// A valid pager is normally present - this check is mostly to ease unit testing.
	if (m_pPager) {
		// Page the data in
		m_pPager->pageIn(reg, this);
	}

	// We'll use this later to decide if data needs to be paged out again.
	m_bDataModified = false;
}

PagedVolume::Chunk::~Chunk() {
	if (m_bDataModified && m_pPager) {
		// From the coordinates of the chunk we deduce the coordinates of the contained voxels.
		const glm::ivec3 v3dLower = m_v3dChunkSpacePosition * static_cast<int32_t>(m_uSideLength);
		const glm::ivec3 v3dUpper = v3dLower + glm::ivec3(m_uSideLength - 1, m_uSideLength - 1, m_uSideLength - 1);

		// Page the data out
		m_pPager->pageOut(Region(v3dLower, v3dUpper), this);
	}

	delete[] m_tData;
	m_tData = 0;
}

Voxel* PagedVolume::Chunk::getData() const {
	return m_tData;
}

uint32_t PagedVolume::Chunk::getDataSizeInBytes() const {
	return m_uSideLength * m_uSideLength * m_uSideLength * sizeof(Voxel);
}

Voxel PagedVolume::Chunk::getVoxel(uint32_t uXPos, uint32_t uYPos, uint32_t uZPos) const {
	// This code is not usually expected to be called by the user, with the exception of when implementing paging
	// of uncompressed data. It's a performance critical code path so we use asserts rather than exceptions.
	core_assert_msg(uXPos < m_uSideLength, "Supplied position is outside of the chunk");
	core_assert_msg(uYPos < m_uSideLength, "Supplied position is outside of the chunk");
	core_assert_msg(uZPos < m_uSideLength, "Supplied position is outside of the chunk");
	core_assert_msg(m_tData, "No uncompressed data - chunk must be decompressed before accessing voxels.");

	const uint32_t index = morton256_x[uXPos] | morton256_y[uYPos] | morton256_z[uZPos];
	return m_tData[index];
}

Voxel PagedVolume::Chunk::getVoxel(const glm::i16vec3& v3dPos) const {
	return getVoxel(v3dPos.x, v3dPos.y, v3dPos.z);
}

void PagedVolume::Chunk::setVoxel(uint32_t uXPos, uint32_t uYPos, uint32_t uZPos, Voxel tValue) {
	// This code is not usually expected to be called by the user, with the exception of when implementing paging
	// of uncompressed data. It's a performance critical code path so we use asserts rather than exceptions.
	core_assert_msg(uXPos < m_uSideLength, "Supplied position is outside of the chunk");
	core_assert_msg(uYPos < m_uSideLength, "Supplied position is outside of the chunk");
	core_assert_msg(uZPos < m_uSideLength, "Supplied position is outside of the chunk");
	core_assert_msg(m_tData, "No uncompressed data - chunk must be decompressed before accessing voxels.");

	const uint32_t index = morton256_x[uXPos] | morton256_y[uYPos] | morton256_z[uZPos];
	m_tData[index] = tValue;
	this->m_bDataModified = true;
}

void PagedVolume::Chunk::setVoxel(const glm::i16vec3& v3dPos, Voxel tValue) {
	setVoxel(v3dPos.x, v3dPos.y, v3dPos.z, tValue);
}

uint32_t PagedVolume::Chunk::calculateSizeInBytes() {
	// Call through to the static version
	return calculateSizeInBytes(m_uSideLength);
}

uint32_t PagedVolume::Chunk::calculateSizeInBytes(uint32_t uSideLength) {
	// Note: We disregard the size of the other class members as they are likely to be very small compared to the size of the
	// allocated voxel data. This also keeps the reported size as a power of two, which makes other memory calculations easier.
	uint32_t uSizeInBytes = uSideLength * uSideLength * uSideLength * sizeof(Voxel);
	return uSizeInBytes;
}

// This convienience function exists for historical reasons. Chunks used to store their data in 'linear' order but now we
// use Morton encoding. Users who still have data in linear order (on disk, in databases, etc) will need to call this function
// if they load the data in by memcpy()ing it via the raw pointer. On the other hand, if they set the data using setVoxel()
// then the ordering is automatically handled correctly.
void PagedVolume::Chunk::changeLinearOrderingToMorton() {
	Voxel* pTempBuffer = new Voxel[m_uSideLength * m_uSideLength * m_uSideLength];

	// We should prehaps restructure this loop. From: https://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/
	//
	// "There's two basic ways to structure the actual swizzling: either you go through the (linear) source image in linear order,
	// writing in (somewhat) random order, or you iterate over the output data, picking the right source pixel for each target
	// location. The former is more natural, especially when updating subrects of the destination texture (the source pixels still
	// consist of one linear sequence of bytes per line; the pattern of destination addresses written is considerably more
	// complicated), but the latter is usually much faster, especially if the source image data is in cached memory while the output
	// data resides in non-cached write-combined memory where non-sequential writes are expensive."
	//
	// This is something to consider if profiling identifies it as a hotspot.
	for (uint16_t z = 0; z < m_uSideLength; z++) {
		for (uint16_t y = 0; y < m_uSideLength; y++) {
			for (uint16_t x = 0; x < m_uSideLength; x++) {
				uint32_t uLinearIndex = x + y * m_uSideLength + z * m_uSideLength * m_uSideLength;
				uint32_t uMortonIndex = morton256_x[x] | morton256_y[y] | morton256_z[z];
				pTempBuffer[uMortonIndex] = m_tData[uLinearIndex];
			}
		}
	}

	std::memcpy(m_tData, pTempBuffer, getDataSizeInBytes());

	delete[] pTempBuffer;
}

// Like the above function, this is provided fot easing backwards compatibility. In Cubiquity we have some
// old databases which use linear ordering, and we need to continue to save such data in linear order.
void PagedVolume::Chunk::changeMortonOrderingToLinear() {
	Voxel* pTempBuffer = new Voxel[m_uSideLength * m_uSideLength * m_uSideLength];
	for (uint16_t z = 0; z < m_uSideLength; z++) {
		for (uint16_t y = 0; y < m_uSideLength; y++) {
			for (uint16_t x = 0; x < m_uSideLength; x++) {
				uint32_t uLinearIndex = x + y * m_uSideLength + z * m_uSideLength * m_uSideLength;
				uint32_t uMortonIndex = morton256_x[x] | morton256_y[y] | morton256_z[z];
				pTempBuffer[uLinearIndex] = m_tData[uMortonIndex];
			}
		}
	}

	std::memcpy(m_tData, pTempBuffer, getDataSizeInBytes());

	delete[] pTempBuffer;
}

#define CAN_GO_NEG_X(val) (val > 0)
#define CAN_GO_POS_X(val)  (val < this->m_uChunkSideLengthMinusOne)
#define CAN_GO_NEG_Y(val) (val > 0)
#define CAN_GO_POS_Y(val)  (val < this->m_uChunkSideLengthMinusOne)
#define CAN_GO_NEG_Z(val) (val > 0)
#define CAN_GO_POS_Z(val)  (val < this->m_uChunkSideLengthMinusOne)

#define NEG_X_DELTA (-(deltaX[this->m_uXPosInChunk-1]))
#define POS_X_DELTA (deltaX[this->m_uXPosInChunk])
#define NEG_Y_DELTA (-(deltaY[this->m_uYPosInChunk-1]))
#define POS_Y_DELTA (deltaY[this->m_uYPosInChunk])
#define NEG_Z_DELTA (-(deltaZ[this->m_uZPosInChunk-1]))
#define POS_Z_DELTA (deltaZ[this->m_uZPosInChunk])

PagedVolume::Sampler::Sampler(PagedVolume* volume) :
		BaseVolume<Voxel>::template Sampler<PagedVolume >(volume), m_uChunkSideLengthMinusOne(volume->m_uChunkSideLength - 1) {
}

PagedVolume::Sampler::~Sampler() {
}

void PagedVolume::Sampler::setPosition(int32_t xPos, int32_t yPos, int32_t zPos) {
	// Base version updates position and validity flags.
	BaseVolume<Voxel>::template Sampler<PagedVolume >::setPosition(xPos, yPos, zPos);

	// Then we update the voxel pointer
	const int32_t uXChunk = this->mXPosInVolume >> this->mVolume->m_uChunkSideLengthPower;
	const int32_t uYChunk = this->mYPosInVolume >> this->mVolume->m_uChunkSideLengthPower;
	const int32_t uZChunk = this->mZPosInVolume >> this->mVolume->m_uChunkSideLengthPower;

	m_uXPosInChunk = static_cast<uint16_t>(this->mXPosInVolume - (uXChunk << this->mVolume->m_uChunkSideLengthPower));
	m_uYPosInChunk = static_cast<uint16_t>(this->mYPosInVolume - (uYChunk << this->mVolume->m_uChunkSideLengthPower));
	m_uZPosInChunk = static_cast<uint16_t>(this->mZPosInVolume - (uZChunk << this->mVolume->m_uChunkSideLengthPower));

	uint32_t uVoxelIndexInChunk = morton256_x[m_uXPosInChunk] | morton256_y[m_uYPosInChunk] | morton256_z[m_uZPosInChunk];

	auto pCurrentChunk =
			this->mVolume->canReuseLastAccessedChunk(uXChunk, uYChunk, uZChunk) ? this->mVolume->m_pLastAccessedChunk : this->mVolume->getChunk(uXChunk, uYChunk, uZChunk);

	mCurrentVoxel = pCurrentChunk->m_tData + uVoxelIndexInChunk;
}

bool PagedVolume::Sampler::setVoxel(const Voxel& tValue) {
	//Need to think what effect this has on any existing iterators.
	core_assert_msg(false, "This function cannot be used on PagedVolume samplers.");
	return false;
}

void PagedVolume::Sampler::movePositiveX() {
	// Base version updates position and validity flags.
	BaseVolume<Voxel>::template Sampler<PagedVolume >::movePositiveX();

	// Then we update the voxel pointer
	if (CAN_GO_POS_X(this->m_uXPosInChunk)) {
		//No need to compute new chunk.
		mCurrentVoxel += POS_X_DELTA;
		this->m_uXPosInChunk++;
	} else {
		//We've hit the chunk boundary. Just calling setPosition() is the easiest way to resolve this.
		setPosition(this->mXPosInVolume, this->mYPosInVolume, this->mZPosInVolume);
	}
}

void PagedVolume::Sampler::movePositiveY() {
	// Base version updates position and validity flags.
	BaseVolume<Voxel>::template Sampler<PagedVolume >::movePositiveY();

	// Then we update the voxel pointer
	if (CAN_GO_POS_Y(this->m_uYPosInChunk)) {
		//No need to compute new chunk.
		mCurrentVoxel += POS_Y_DELTA;
		this->m_uYPosInChunk++;
	} else {
		//We've hit the chunk boundary. Just calling setPosition() is the easiest way to resolve this.
		setPosition(this->mXPosInVolume, this->mYPosInVolume, this->mZPosInVolume);
	}
}

void PagedVolume::Sampler::movePositiveZ() {
	// Base version updates position and validity flags.
	BaseVolume<Voxel>::template Sampler<PagedVolume >::movePositiveZ();

	// Then we update the voxel pointer
	if (CAN_GO_POS_Z(this->m_uZPosInChunk)) {
		//No need to compute new chunk.
		mCurrentVoxel += POS_Z_DELTA;
		this->m_uZPosInChunk++;
	} else {
		//We've hit the chunk boundary. Just calling setPosition() is the easiest way to resolve this.
		setPosition(this->mXPosInVolume, this->mYPosInVolume, this->mZPosInVolume);
	}
}

void PagedVolume::Sampler::moveNegativeX() {
	// Base version updates position and validity flags.
	BaseVolume<Voxel>::template Sampler<PagedVolume >::moveNegativeX();

	// Then we update the voxel pointer
	if (CAN_GO_NEG_X(this->m_uXPosInChunk)) {
		//No need to compute new chunk.
		mCurrentVoxel += NEG_X_DELTA;
		this->m_uXPosInChunk--;
	} else {
		//We've hit the chunk boundary. Just calling setPosition() is the easiest way to resolve this.
		setPosition(this->mXPosInVolume, this->mYPosInVolume, this->mZPosInVolume);
	}
}

void PagedVolume::Sampler::moveNegativeY() {
	// Base version updates position and validity flags.
	BaseVolume<Voxel>::template Sampler<PagedVolume >::moveNegativeY();

	// Then we update the voxel pointer
	if (CAN_GO_NEG_Y(this->m_uYPosInChunk)) {
		//No need to compute new chunk.
		mCurrentVoxel += NEG_Y_DELTA;
		this->m_uYPosInChunk--;
	} else {
		//We've hit the chunk boundary. Just calling setPosition() is the easiest way to resolve this.
		setPosition(this->mXPosInVolume, this->mYPosInVolume, this->mZPosInVolume);
	}
}

void PagedVolume::Sampler::moveNegativeZ() {
	// Base version updates position and validity flags.
	BaseVolume<Voxel>::template Sampler<PagedVolume >::moveNegativeZ();

	// Then we update the voxel pointer
	if (CAN_GO_NEG_Z(this->m_uZPosInChunk)) {
		//No need to compute new chunk.
		mCurrentVoxel += NEG_Z_DELTA;
		this->m_uZPosInChunk--;
	} else {
		//We've hit the chunk boundary. Just calling setPosition() is the easiest way to resolve this.
		setPosition(this->mXPosInVolume, this->mYPosInVolume, this->mZPosInVolume);
	}
}

}
