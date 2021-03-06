/**
 * @file
 */

#include "TreeNodeParser.h"
#include "core/StringUtil.h"
#include "backend/entity/ai/condition/True.h"

namespace backend {

TreeNodeParser::TreeNodeParser(const IAIFactory& aiFactory, const core::String& taskString) :
		IParser(), _aiFactory(aiFactory) {
	_taskString = core::string::eraseAllChars(taskString, ' ');
}

void TreeNodeParser::splitTasks(const core::String& string, std::vector<core::String>& tokens) const {
	bool inParameter = false;
	bool inChildren = false;
	core::String token;
	for (auto i = string.begin(); i != string.end(); ++i) {
		if (*i == '{') {
			inParameter = true;
		} else if (*i == '}') {
			inParameter = false;
		} else if (*i == '(') {
			inChildren = true;
		} else if (*i == ')') {
			inChildren = false;
		}

		if (!inParameter && !inChildren) {
			if (*i == ',') {
				tokens.push_back(token);
				token.clear();
				continue;
			}
		}
		token += *i;
	}
	tokens.push_back(token);
}

SteeringPtr TreeNodeParser::getSteering (const core::String& nodeName) {
	core::String steerType;
	const core::String& parameters = getBetween(nodeName, "{", "}");
	size_t n = nodeName.find("{");
	if (n == core::String::npos)
		n = nodeName.find("(");
	if (n != core::String::npos) {
		steerType = nodeName.substr(0, n);
	} else {
		steerType = nodeName;
	}

	const SteeringFactoryContext ctx(parameters);
	return _aiFactory.createSteering(steerType, ctx);
}

TreeNodePtr TreeNodeParser::getTreeNode(const core::String& name) {
	resetError();
	core::String nodeType;
	core::String parameters;
	size_t n = _taskString.find("(");
	if (n == core::String::npos || _taskString.find("{") < n) {
		parameters = getBetween(_taskString, "{", "}");
		n = _taskString.find("{");
	}
	if (n != core::String::npos) {
		nodeType = _taskString.substr(0, n);
	} else {
		nodeType = _taskString;
	}
	const core::String& subTrees = getBetween(_taskString, "(", ")");
	if (!subTrees.empty()) {
		if (nodeType != "Steer") {
			return TreeNodePtr();
		}
		std::vector<core::String> tokens;
		splitTasks(subTrees, tokens);
		movement::Steerings steerings;
		for (const core::String& nodeName : tokens) {
			const SteeringPtr& steering = getSteering(nodeName);
			if (!steering) {
				return TreeNodePtr();
			}
			steerings.push_back(steering);
		}
		const SteerNodeFactoryContext steerFactoryCtx(name.empty() ? nodeType : name, parameters, True::get(), steerings);
		return _aiFactory.createSteerNode(nodeType, steerFactoryCtx);
	}

	const TreeNodeFactoryContext factoryCtx(name.empty() ? nodeType : name, parameters, True::get());
	return _aiFactory.createNode(nodeType, factoryCtx);
}

}
