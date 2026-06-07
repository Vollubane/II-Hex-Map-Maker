extends Panel

## Displays a full-screen loading overlay driven by SystemEventBus signals.
## Becomes visible on loading_event, hidden on loading_end_event.

const _SPRITESHEET_PATH := "res://Ressource/2DTexture/loading.png"
const _FRAME_W := 100
const _FRAME_H := 100
const _COLS := 5
const _FRAME_COUNT := 30
const _FPS := 15.0

@onready var _spinner: TextureRect = $Panel/VBoxContainer/Spinner
@onready var _protocol_label: Label = $Panel/VBoxContainer/ProtocolLabel
@onready var _sub_protocol_label: Label = $Panel/VBoxContainer/SubProtocolLabel
@onready var _element_label: Label = $Panel/VBoxContainer/ElementLabel

var _atlas_tex: AtlasTexture
var _frame: int = 0
var _time_acc: float = 0.0


func _ready() -> void:
	visible = false
	var sheet := load(_SPRITESHEET_PATH) as Texture2D
	if sheet != null:
		_atlas_tex = AtlasTexture.new()
		_atlas_tex.atlas = sheet
		_atlas_tex.region = Rect2(0, 0, _FRAME_W, _FRAME_H)
		_spinner.texture = _atlas_tex
	SystemEventBus.loading_event.connect(_on_loading_event)
	SystemEventBus.loading_end_event.connect(_on_loading_end_event)


func _process(p_delta: float) -> void:
	if not visible or _atlas_tex == null:
		return
	_time_acc += p_delta
	if _time_acc >= 1.0 / _FPS:
		_time_acc -= 1.0 / _FPS
		_frame = (_frame + 1) % _FRAME_COUNT
		var col := _frame % _COLS
		var row := _frame / _COLS
		_atlas_tex.region = Rect2(col * _FRAME_W, row * _FRAME_H, _FRAME_W, _FRAME_H)


func _on_loading_event(p_protocol: String, p_sub_protocol: String, p_element: Variant) -> void:
	_protocol_label.text = p_protocol
	_sub_protocol_label.text = p_sub_protocol
	var elem_str: String = str(p_element).strip_edges() if p_element != null else ""
	_element_label.text = elem_str
	_element_label.visible = not elem_str.is_empty()
	if not visible:
		_frame = 0
		_time_acc = 0.0
		visible = true


func _on_loading_end_event() -> void:
	visible = false
