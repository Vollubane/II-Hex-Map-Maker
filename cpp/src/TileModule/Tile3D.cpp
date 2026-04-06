#include "Tile3D.h"


using namespace godot;
namespace TileModule {

    void Tile3D::_bind_methods() {
        ClassDB::bind_method(D_METHOD("onDisappearFinished"), &Tile3D::onDisappearFinished);
    }

    Tile3D::Tile3D() :
        m_data{},
        m_tween{}
    {
    }

    Tile3D::~Tile3D() {
    }

    void Tile3D::_ready() {
    }

    void Tile3D::loadAtPosition(const Vector2i p_mapPosition, const Dictionary p_tileData) {
        this->set_position(getWorldPositionFromMapPosition(p_mapPosition));
        this->set_scale(Vector3());
        this->set_visible(true);
        if (m_tween.is_valid()) {
            m_tween->kill();
        }
        m_tween = create_tween();
        Ref<PropertyTweener> tw = m_tween->tween_property(
            this,
            "scale",
            Vector3(1.0f, 1.0f, 1.0f),
            HEX_TILES_ANIMATION_TIME);
        tw->set_trans(Tween::TRANS_QUAD);
        tw->set_ease(Tween::EASE_OUT);

        // Action for data to implement
        m_data.clear();
        m_data = p_tileData;
    }

    void Tile3D::unload() {
        if (m_tween.is_valid()) {
            m_tween->kill();
        }
        m_tween = create_tween();
        Ref<PropertyTweener> tw = m_tween->tween_property(
            this,
            "scale",
            Vector3(0.0f, 0.0f, 0.0f),
            HEX_TILES_ANIMATION_TIME);
        tw->set_trans(Tween::TRANS_QUAD);
        tw->set_ease(Tween::EASE_IN);
        m_tween->connect("finished", Callable(this, "onDisappearFinished"));
    }

    void Tile3D::onDisappearFinished() {
        this->set_visible(false);
    }

    const Vector3 Tile3D::getWorldPositionFromMapPosition(const Vector2i p_mapPosition){
        const float d = Math::sqrt(3.0f) * HEX_GRID_DIAMETER*0.5f;
        const float x = d * static_cast<real_t>(p_mapPosition[0]) + (d * 0.5f) * static_cast<real_t>(p_mapPosition[1]);
        const float z = (d * Math::sqrt(3.0f) * 0.5f) * static_cast<real_t>(p_mapPosition[1]);
        return Vector3(x, 0, z);
    }
}
