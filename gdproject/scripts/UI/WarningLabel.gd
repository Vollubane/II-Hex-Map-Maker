class_name WarningLabel
extends Control

@onready var _label: Label = $CenterContainer/Label

var _fade_duration: float = 1.0
var _fade_timer: float = 0.0
var _is_active: bool = false
var _style: StyleBoxFlat = null
var _base_font_color: Color
var _base_shadow_color: Color
var _base_bg_color: Color
var _base_border_color: Color


func _ready() -> void:
	var existing: StyleBoxFlat = _label.get_theme_stylebox("normal") as StyleBoxFlat
	if existing:
		_style = existing.duplicate()
		_label.add_theme_stylebox_override("normal", _style)
		_base_bg_color = _style.bg_color
		_base_border_color = _style.border_color
	_base_font_color = _label.get_theme_color("font_color")
	_base_shadow_color = _label.get_theme_color("font_shadow_color")
	SystemEventBus.warning_event.connect(_on_warning_event)
	_apply_alpha(0.0)


func _on_warning_event(p_text: String, p_fade_duration: float) -> void:
	_label.text = p_text
	_fade_duration = maxf(p_fade_duration, 0.01)
	_fade_timer = _fade_duration
	_is_active = true
	_apply_alpha(1.0)


func _process(p_delta: float) -> void:
	if not _is_active:
		return
	_fade_timer -= p_delta
	if _fade_timer <= 0.0:
		_fade_timer = 0.0
		_is_active = false
		_apply_alpha(0.0)
		return
	_apply_alpha(_fade_timer / _fade_duration)


func _apply_alpha(p_alpha: float) -> void:
	_label.add_theme_color_override("font_color",
		Color(_base_font_color.r, _base_font_color.g, _base_font_color.b, _base_font_color.a * p_alpha))
	_label.add_theme_color_override("font_shadow_color",
		Color(_base_shadow_color.r, _base_shadow_color.g, _base_shadow_color.b, _base_shadow_color.a * p_alpha))
	if _style:
		_style.bg_color = Color(_base_bg_color.r, _base_bg_color.g, _base_bg_color.b, _base_bg_color.a * p_alpha)
		_style.border_color = Color(_base_border_color.r, _base_border_color.g, _base_border_color.b, _base_border_color.a * p_alpha)
