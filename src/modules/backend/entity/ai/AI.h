/**
 * @file
 */
#pragma once

#include "backend/entity/ai/filter/FilteredEntities.h"
#include "group/GroupId.h"
#include "aggro/AggroMgr.h"
#include "ICharacter.h"
#include "core/Trace.h"
#include "core/concurrent/Lock.h"
#include "core/concurrent/Atomic.h"
#include "core/NonCopyable.h"
#include "AIMessages_generated.h"

#include <memory>
#include <glm/vec3.hpp>

namespace backend {

#ifndef AI_NOTHING_SELECTED
#define AI_NOTHING_SELECTED (-1)
#endif

class TreeNode;
typedef std::shared_ptr<TreeNode> TreeNodePtr;

class ICharacter;
typedef core::SharedPtr<ICharacter> ICharacterPtr;
class Zone;

/**
 * @brief This is the type the library works with. It interacts with it's real world entity by
 * the @ai{ICharacter} interface.
 *
 * Each ai entity has a @ai[AggroMgr} assigned that is updated with each tick (update()).
 *
 * A behaviour can be replaced at runtime with setBehaviour()
 *
 * You can set single @c AI instances to no longer update their state by calling setPause()
 */
class AI : public core::NonCopyable {
	friend class TreeNode;
	friend class LUAAIRegistry;
	friend class IFilter;
	friend class Filter;
	friend class Server;
protected:
	/**
	 * This map is only filled if we are in debugging mode for this entity
	 */
	typedef core::Map<int, ai::TreeNodeStatus> NodeStates;
	NodeStates _lastStatus;
	/**
	 * This map is only filled if we are in debugging mode for this entity
	 */
	typedef core::Map<int, uint64_t> LastExecMap;
	LastExecMap _lastExecMillis;

	/**
	 * @note The filtered entities are kept even over several ticks. The caller should decide
	 * whether he still needs an old/previous filtered selection
	 * @sa @ai{IFilter}
	 */
	mutable FilteredEntities _filteredEntities;

	/**
	 * Often @ai{Selector} states must be stored to continue in the next step at a particular
	 * position in the behaviour tree. This map is doing exactly this.
	 */
	typedef core::Map<int, int> SelectorStates;
	SelectorStates _selectorStates;

	/**
	 * This map stores the amount of execution for the @ai{Limit} node. The key is the node id
	 */
	typedef core::Map<int, int> LimitStates;
	LimitStates _limitStates;

	TreeNodePtr _behaviour;
	AggroMgr _aggroMgr;

	ICharacterPtr _character;

	bool _pause;
	bool _debuggingActive;

	int64_t _time;

	Zone* _zone;

	core::AtomicBool _reset;
public:
	/**
	 * @param behaviour The behaviour tree node that is applied to this ai entity
	 */
	explicit AI(const TreeNodePtr& behaviour) :
			_behaviour(behaviour), _pause(false), _debuggingActive(false), _time(0L), _zone(nullptr), _reset(false) {
	}
	virtual ~AI() {
	}

	void setFilteredEntities(const FilteredEntities& filteredEntities);

	void addFilteredEntity(ai::CharacterId id);

	/**
	 * @brief Update the behaviour and the aggro values if the entity is not on hold.
	 * @param[in] dt The current milliseconds to update the aggro entries and
	 * time based tasks or conditions.
	 */
	virtual void update(int64_t dt, bool debuggingActive);

	/**
	 * @brief Set the new @ai{Zone} this entity is in
	 */
	void setZone(Zone* zone);
	/**
	 * Returns the @ai{Zone} this entity is in.
	 */
	Zone* getZone() const;
	/**
	 * @brief Returns @c true if the entity is already in a @ai{Zone}. This must not be managed manually,
	 * the @c Zone is doing that already.
	 */
	bool hasZone() const;

	/**
	 * @brief don't update the entity as long as it is paused
	 * @sa isPause()
	 */
	void setPause(bool pause);

	/**
	 * @brief don't update the entity as long as it is paused
	 * @sa setPause()
	 */
	bool isPause() const;

	/**
	 * @return @c true if the owning entity is currently under debugging, @c false otherwise
	 */
	bool isDebuggingActive() const;

	/**
	 * @brief Get the current behaviour for this ai
	 */
	TreeNodePtr getBehaviour() const;
	/**
	 * @brief Set a new behaviour
	 * @return the old one if there was any
	 */
	TreeNodePtr setBehaviour(const TreeNodePtr& newBehaviour);
	/**
	 * @return The real world entity reference
	 */
	ICharacterPtr getCharacter() const;
	/**
	 * You might not set a character twice to an @c AI instance.
	 */
	void setCharacter(const ICharacterPtr& character);

	int64_t getTime() const;

	ai::CharacterId getId() const;

	/**
	 * @return the @c AggroMgr for this @c AI instance. Each @c AI instance has its own @c AggroMgr instance.
	 */
	AggroMgr& getAggroMgr();
	/**
	 * @return the @c AggroMgr for this @c AI instance. Each @c AI instance has its own @c AggroMgr instance.
	 */
	const AggroMgr& getAggroMgr() const;

	/**
	 * @brief @c FilteredEntities is holding a list of @c CharacterIds that were selected by the @c Select condition.
	 * @sa @c IFilter interface.
	 * @sa @c Filter condition that executes assigned @c IFilter implementations.
	 * @return A reference to the internal data structure. This should only be used from within @c TreeNode implementations
	 * to access those entities that were filtered by the @c Filter condition.
	 *
	 * @note If you call this from outside of the behaviour tree tick, you will run into race conditions.
	 */
	const FilteredEntities& getFilteredEntities() const;
};

inline TreeNodePtr AI::getBehaviour() const {
	return _behaviour;
}

inline void AI::setPause(bool pause) {
	_pause = pause;
}

inline bool AI::isPause() const {
	return _pause;
}

inline ICharacterPtr AI::getCharacter() const {
	return _character;
}

inline void AI::setCharacter(const ICharacterPtr& character) {
	_character = character;
}

inline AggroMgr& AI::getAggroMgr() {
	return _aggroMgr;
}

inline const AggroMgr& AI::getAggroMgr() const {
	return _aggroMgr;
}

inline const FilteredEntities& AI::getFilteredEntities() const {
	return _filteredEntities;
}

inline void AI::setFilteredEntities(const FilteredEntities& filteredEntities) {
	_filteredEntities = filteredEntities;
}

inline void AI::addFilteredEntity(ai::CharacterId id) {
	_filteredEntities.push_back(id);
}

inline bool AI::isDebuggingActive() const {
	return _debuggingActive;
}

inline void AI::setZone(Zone* zone) {
	if (_zone != zone || zone == nullptr) {
		_filteredEntities.clear();
	}
	_zone = zone;
}

inline Zone* AI::getZone() const {
	return _zone;
}

inline bool AI::hasZone() const {
	return _zone != nullptr;
}

inline int64_t AI::getTime() const {
	return _time;
}

typedef std::shared_ptr<AI> AIPtr;

}
