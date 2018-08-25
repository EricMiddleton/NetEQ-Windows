#include "RAIIWrapper.hpp"

RAIIWrapper::RAIIWrapper(const Action& onConstruct, const Action& onDestruct)
	: RAIIWrapper(onDestruct) {

	onConstruct();
}

RAIIWrapper::RAIIWrapper(const Action& onDestruct)
	: m_onDestruct{ onDestruct } {

}

RAIIWrapper::~RAIIWrapper() {
	m_onDestruct();
}