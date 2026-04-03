#include "TilesContainer.h"

using namespace godot;
namespace HMDisplayerModule {

    void TilesContainer::_bind_methods() {
    }

    TilesContainer::TilesContainer() :
        m_hexMapEventBus{nullptr}
    {
    }

    TilesContainer::~TilesContainer() {
    }

    void TilesContainer::_ready() {
        m_hexMapEventBus = get_node<Node>("/root/HexMapEventBus");
        if (!m_hexMapEventBus) {
            UtilityFunctions::push_error("TilesContainer ready : Can't find HexMapEventBus");
            return;
        }
        m_hexMapEventBus->emit_signal("tilesContainer_ready");
    }
}
