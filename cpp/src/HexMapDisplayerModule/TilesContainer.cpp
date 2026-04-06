#include "TilesContainer.h"

//godot
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace HMDisplayerModule {

    const int MAP_RADIUS_MAX = 18;
    const int MAP_RADIUS_MIN = 5;
    const float FPS_RATIO_BUDGET = 0.5f;
    constexpr int AXIAL_RING_DQ[6] = {1, 1, 0, -1, -1, 0};
    constexpr int AXIAL_RING_DR[6] = {-1, 0, 1, 1, 0, -1};


    void TilesContainer::_bind_methods() {
        ClassDB::bind_method(D_METHOD("onLoadTilesPendingTimeout"), &TilesContainer::onLoadTilesPendingTimeout);
        ClassDB::bind_method(D_METHOD("onFocusPointChanged"), &TilesContainer::onFocusPointChanged);
        ClassDB::bind_method(D_METHOD("onZoomChanged"), &TilesContainer::onZoomChanged);
    }

    TilesContainer::TilesContainer() :
        m_hexMapEventBus{nullptr},
        m_tileScene{},
        m_coordonateOfMappedTiles{},
        m_coordonateOfTilesToUnload{},
        m_coordonateOfTilesToLoad{},
        m_mapRadius{MAP_RADIUS_MIN+(MAP_RADIUS_MAX-MAP_RADIUS_MIN)/2},
        m_pooledTiles{},
        m_focussedTile{0, 0},
        m_pendingTileToPool{0},
        m_pendingTilePositions{},
        m_pendingPackSize{10}
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
        m_hexMapEventBus->connect("center_map_position", Callable(this, "onFocusPointChanged"));
        m_hexMapEventBus->connect("zoom_map_ratio", Callable(this, "onZoomChanged"));


        m_tileScene = ResourceLoader::get_singleton()->load(String(M_ModulesList.at("TILE_3D").c_str()));
        Node* testTile = m_tileScene->instantiate();
        if(!testTile) {
            UtilityFunctions::push_error("TilesContainer ready : Invalide tileScene");
            return;
        }
        testTile->queue_free();

        const int tileToPool = 3*(MAP_RADIUS_MAX+2)*(MAP_RADIUS_MAX+3)-3*m_mapRadius*(m_mapRadius+1);
        Array remainingTilePositionToSpawn = getDiskTilesPosition(Vector2i(0, 0), m_mapRadius);
        loadTilesContainer(tileToPool,remainingTilePositionToSpawn);
    }

    void TilesContainer::_process(double p_delta) {
        if(0<m_coordonateOfTilesToUnload.size()) {
            double timeBudget = 1e6 * 1/float(FPS_IN_CALCULATION) * FPS_RATIO_BUDGET;
            int packInOneBudget = 0;

            for (int zeroOnCount = 0;0<timeBudget&&zeroOnCount<3;zeroOnCount++) {

                const uint64_t timeCountStart = Time::get_singleton()->get_ticks_usec();
                for(int i=0;i<m_pendingPackSize&&0<m_coordonateOfTilesToUnload.size();i++) {

                    Vector2i tilePositionToUnload = m_coordonateOfTilesToUnload.pop_front();
                    Node* tileToUnload = Object::cast_to<Node>(m_coordonateOfMappedTiles[tilePositionToUnload]);

                    m_pooledTiles.append(tileToUnload);
                    m_coordonateOfMappedTiles.erase(tilePositionToUnload);
                    TileModule::Tile3D* unloadedTile = Object::cast_to<TileModule::Tile3D>(tileToUnload);

                    if(unloadedTile) {
                        unloadedTile->unload();
                    }
                }
                const uint64_t timePassInOnePass = Time::get_singleton()->get_ticks_usec() - timeCountStart;

                timeBudget -= timePassInOnePass;
                packInOneBudget++;

                if(0!=timePassInOnePass) {
                    zeroOnCount=0;
                }
                if(0>=m_coordonateOfTilesToUnload.size()) {
                    break;
                }
            }
            if(0>=m_coordonateOfTilesToLoad.size()) {
                m_pendingPackSize = 10;
                return;
            }
            if(1==packInOneBudget) {
                m_pendingPackSize = int(Math::floor(float(m_pendingPackSize) / 2.0f));
                return;
            }
            if(2<packInOneBudget) {
                m_pendingPackSize = int(Math::ceil(float(m_pendingPackSize) * 1.5f));
                return;
            }
        } else if (0<m_coordonateOfTilesToLoad.size()) {
            double timeBudget = 1e6 * 1/float(FPS_IN_CALCULATION) * FPS_RATIO_BUDGET;
            int packInOneBudget = 0;

            for (int zeroOnCount = 0;0<timeBudget&&zeroOnCount<3;zeroOnCount++) {

                const uint64_t timeCountStart = Time::get_singleton()->get_ticks_usec();
                for(int i=0;i<m_pendingPackSize&&0<m_coordonateOfTilesToLoad.size();i++) {

                    Vector2i newTilePosition = m_coordonateOfTilesToLoad.pop_front();
                    Node* newTileToLoad = Object::cast_to<Node>(m_pooledTiles.pop_front());
                    m_coordonateOfMappedTiles[newTilePosition] = newTileToLoad;
                    TileModule::Tile3D* newTile = Object::cast_to<TileModule::Tile3D>(newTileToLoad);

                    if(newTile) {
                        newTile->loadAtPosition(newTilePosition);
                    }
                }
                const uint64_t timePassInOnePass = Time::get_singleton()->get_ticks_usec() - timeCountStart;

                timeBudget -= timePassInOnePass;
                packInOneBudget++;

                if(0!=timePassInOnePass) {
                    zeroOnCount=0;
                }
                if(0>=m_coordonateOfTilesToLoad.size()) {
                    break;
                }
            }
            if(0>=m_coordonateOfTilesToLoad.size()) {
                m_pendingPackSize = 10;
                return;
            }
            if(1==packInOneBudget) {
                m_pendingPackSize = int(Math::floor(float(m_pendingPackSize) / 2.0f));
                return;
            }
            if(2<packInOneBudget) {
                m_pendingPackSize = int(Math::ceil(float(m_pendingPackSize) * 1.5f));
                return;
            }
        }
    }

    void TilesContainer::onFocusPointChanged(const Vector2 p_focusPoint) {
        const Vector2i newFocusTile = getMapPositionFromPlanPosition(p_focusPoint);
        if(newFocusTile==m_focussedTile) return;
        m_focussedTile = newFocusTile;
        updateMap();
    }

    void TilesContainer::onZoomChanged(const float p_zoomRatio) {
        const int newMapRadius = MAP_RADIUS_MIN + (MAP_RADIUS_MAX-MAP_RADIUS_MIN)*p_zoomRatio;
        if(newMapRadius==m_mapRadius) return;
        m_mapRadius = newMapRadius;
        updateMap();
    }

    void TilesContainer::updateMap() {
        Array diskTilesMapPosition = getDiskTilesPosition(m_focussedTile, m_mapRadius);
        m_coordonateOfTilesToUnload.clear();
        m_coordonateOfTilesToLoad.clear();

        Dictionary desired_lookup;
        for (int i = 0; i < diskTilesMapPosition.size(); ++i) {
            const Vector2i cell = diskTilesMapPosition[i];
            desired_lookup[cell] = true;
        }

        const Array keys_active = m_coordonateOfMappedTiles.keys();
        for (int i = 0; i < keys_active.size(); ++i) {
            const Vector2i cell = keys_active[i];
            if (!desired_lookup.has(cell)) {
                m_coordonateOfTilesToUnload.append(cell);
            }
        }
        for (int i = 0; i < diskTilesMapPosition.size(); ++i) {
            const Vector2i cell = diskTilesMapPosition[i];
            if (!m_coordonateOfMappedTiles.has(cell)) {
                m_coordonateOfTilesToLoad.append(cell);
            }
        }

        return;
    }

    void TilesContainer::loadTilesContainer(const int p_remainingTileToPool, const Array p_remainingTilePositionToSpawn, const int p_packSize) {
        if(p_remainingTileToPool+p_remainingTilePositionToSpawn.size()==0) {
            m_hexMapEventBus->emit_signal("tilesContainer_ready");
            m_pendingPackSize = 10;
            return;
        }

        double timeBudget = 1e6 * 1/float(FPS_IN_CALCULATION) * FPS_RATIO_BUDGET;
        if(p_remainingTileToPool!=0) {
            int currentTileToPool = p_remainingTileToPool;
            int packInOneBudget = 0;

            for (int zeroOnCount = 0;0<timeBudget&&zeroOnCount<3;zeroOnCount++) {

                const uint64_t timeCountStart = Time::get_singleton()->get_ticks_usec();
                for(int i=0;i<p_packSize&&0<currentTileToPool;i++) {
                    Node* pooledTile = m_tileScene->instantiate();
                    this->add_child(pooledTile);
                    m_pooledTiles.append(pooledTile);
                    currentTileToPool--;
                }
                const uint64_t timePassInOnePass = Time::get_singleton()->get_ticks_usec() - timeCountStart;

                timeBudget -= timePassInOnePass;
                packInOneBudget++;
                if(0!=timePassInOnePass) {
                    zeroOnCount=0;
                }
                if(0>=currentTileToPool) {
                    break;
                }
            }
            if(0==currentTileToPool) {
                scheduleLoadTilesNextFrame(0, p_remainingTilePositionToSpawn, p_packSize);
                return;
            }
            if(1==packInOneBudget) {
                scheduleLoadTilesNextFrame(currentTileToPool, p_remainingTilePositionToSpawn, int(Math::floor(float(p_packSize) / 2.0f)));
                return;
            }
            if(2<packInOneBudget) {
                scheduleLoadTilesNextFrame(currentTileToPool, p_remainingTilePositionToSpawn, int(Math::ceil(float(p_packSize) * 1.5f)));
                return;
            }
        } else {
            Array currentTileToSpawn = p_remainingTilePositionToSpawn;
            int packInOneBudget = 0;

            for (int zeroOnCount = 0;0<timeBudget&&zeroOnCount<3;zeroOnCount++) {

                const uint64_t timeCountStart = Time::get_singleton()->get_ticks_usec();
                for(int i=0;i<p_packSize&&0<currentTileToSpawn.size();i++) {
                    Node* pooledTile = m_tileScene->instantiate();
                    Vector2i tilePosition = currentTileToSpawn.pop_front();
                    this->add_child(pooledTile);
                    TileModule::Tile3D* newTile = Object::cast_to<TileModule::Tile3D>(pooledTile);
                    if(newTile) {
                        newTile->loadAtPosition(tilePosition);
                        m_coordonateOfMappedTiles[tilePosition]=pooledTile;
                    }
                }
                const uint64_t timePassInOnePass = Time::get_singleton()->get_ticks_usec() - timeCountStart;

                timeBudget -= timePassInOnePass;
                packInOneBudget++;
                if(0!=timePassInOnePass) {
                    zeroOnCount=0;
                }
                if(0>=currentTileToSpawn.size()) {
                    break;
                }
            }
            if(0==currentTileToSpawn.size()) {
                scheduleLoadTilesNextFrame(0, Array(), p_packSize);
                return;
            }
            if(1==packInOneBudget) {
                scheduleLoadTilesNextFrame(0, currentTileToSpawn, int(Math::floor(float(p_packSize) / 2.0f)));
                return;
            }
            if(2<packInOneBudget) {
                scheduleLoadTilesNextFrame(0, currentTileToSpawn, int(Math::ceil(float(p_packSize) * 1.5f)));
                return;
            }
        }
    }


    void TilesContainer::scheduleLoadTilesNextFrame(const int p_remainingTileToPool, const Array &p_remainingTilePositionToSpawn, const int p_packSize) {
        m_pendingTileToPool = p_remainingTileToPool;
        m_pendingTilePositions = p_remainingTilePositionToSpawn.duplicate();
        m_pendingPackSize = p_packSize;

        SceneTree *tree = get_tree();
        if (!tree) {
            UtilityFunctions::push_error("TilesContainer::scheduleLoadTilesNextFrame: SceneTree unavailable.");
            return;
        }
        Ref<SceneTreeTimer> timer = tree->create_timer(0.0, false, false, true);
        const Error err = timer->connect(
                "timeout",
                Callable(this, "onLoadTilesPendingTimeout"),
                static_cast<uint32_t>(Object::CONNECT_ONE_SHOT));
        if (err != OK) {
            UtilityFunctions::push_error("TilesContainer: connect load timer failed.");
        }
    }

    void TilesContainer::onLoadTilesPendingTimeout() {
        loadTilesContainer(m_pendingTileToPool, m_pendingTilePositions, m_pendingPackSize);
    }


    const Vector2i TilesContainer::getMapPositionFromPlanPosition(const Vector2 p_focusPoint) {
        const double neighbor_dist = Math::sqrt(3.0f) * HEX_GRID_DIAMETER*0.5;
        const double z_per_r = neighbor_dist * Math::sqrt(3.0f) * 0.5;
        const double frac_r = p_focusPoint[1] / z_per_r;
        const double frac_q = (p_focusPoint[0]- 0.5 * neighbor_dist * frac_r) / neighbor_dist;
        const double frac_s = -frac_q - frac_r;
        int snap_q = Math::round(frac_q);
        int snap_r = Math::round(frac_r);
        int snap_s = Math::round(frac_s);
        const double err_q = Math::abs(snap_q - frac_q);
        const double err_r = Math::abs(snap_r - frac_r);
        const double err_s = Math::abs(snap_s - frac_s);
        if (err_q > err_r && err_q > err_s) {
            snap_q = -snap_r - snap_s;
        } else if (err_r > err_s) {
            snap_r = -snap_q - snap_s;
        }
        return Vector2i(snap_q, snap_r);
    }

    const Array TilesContainer::getDiskTilesPosition(const Vector2i p_centerTilePosition, const int p_radius) {
        Array listTilePosition;
        for (int d = 0; d <= p_radius; ++d) {
            if (d == 0) {
                const int q = 0;
                const int r = 0;
                listTilePosition.append(Vector2i(q+p_centerTilePosition[0],r+p_centerTilePosition[1]));
                continue;
            }
            int q = d;
            int r = -d;
            for (int side = 0; side < 6; ++side) {
                const int step_q = AXIAL_RING_DQ[(side + 2) % 6];
                const int step_r = AXIAL_RING_DR[(side + 2) % 6];
                for (int i = 0; i < d; ++i) {
                    listTilePosition.append(Vector2i(q+p_centerTilePosition[0],r+p_centerTilePosition[1]));
                    q += step_q;
                    r += step_r;
                }
            }
        }
        return listTilePosition;
    }

    const int TilesContainer::getHexTilesDistance(const Vector2i p_tilePositionA, const Vector2i p_tilePositionB) {
        const int dq = p_tilePositionA[0] - p_tilePositionB[0];
        const int dr = p_tilePositionA[1] - p_tilePositionB[1];
        const int ds = -dq - dr;
        return Math::max(Math::abs(dq), Math::max(Math::abs(dr), Math::abs(ds)));
    }
}
