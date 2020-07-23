/**
 * @file
 */

#include "Limit.h"
#include "core/Log.h"

namespace backend {

Limit::Limit(const core::String& name, const core::String& parameters, const ConditionPtr& condition) :
	TreeNode(name, parameters, condition) {
	_type = "Limit";
	if (!parameters.empty()) {
		_amount = parameters.toInt();
	} else {
		_amount = 1;
	}
}

ai::TreeNodeStatus Limit::execute(const AIPtr& entity, int64_t deltaMillis) {
	if (_children.size() != 1) {
		Log::error("Limit must have exactly one node");
		return ai::EXCEPTION;
	}

	if (TreeNode::execute(entity, deltaMillis) == ai::CANNOTEXECUTE) {
		return ai::CANNOTEXECUTE;
	}

	const int alreadyExecuted = getLimitState(entity);
	if (alreadyExecuted >= _amount) {
		return state(entity, ai::FINISHED);
	}

	const TreeNodePtr& treeNode = *_children.begin();
	const ai::TreeNodeStatus status = treeNode->execute(entity, deltaMillis);
	setLimitState(entity, alreadyExecuted + 1);
	if (status == ai::RUNNING) {
		return state(entity, ai::RUNNING);
	}
	return state(entity, ai::FAILED);
}

}
