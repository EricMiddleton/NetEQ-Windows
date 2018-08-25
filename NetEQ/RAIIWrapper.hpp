#pragma once

#include <functional>

class RAIIWrapper {
public:
	using Action = std::function<void(void)>;

	RAIIWrapper(const Action& onConstruct, const Action& onDestruct);
	RAIIWrapper(const Action& onDestruct);

	virtual ~RAIIWrapper();

private:
	Action m_onDestruct;
};