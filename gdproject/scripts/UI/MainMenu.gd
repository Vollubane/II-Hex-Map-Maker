extends Control

@onready var _main_panel: Panel = $MainPanel
@onready var _hex_map_list: Panel = $HexMapList


func _ready() -> void:
	var hex_btn: Button = _main_panel.get_node("HexMap Button") as Button
	if hex_btn:
		hex_btn.pressed.connect(_on_open_hex_map_list)


func _on_open_hex_map_list() -> void:
	_hex_map_list.visible = true
	_main_panel.visible = false
