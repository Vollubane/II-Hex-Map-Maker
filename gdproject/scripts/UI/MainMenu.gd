extends Control

@onready var _main_panel: Panel = $MainPanel
@onready var _hex_map_list: Panel = $HexMapList
@onready var _hex_pack_list: Panel = $HexPackList


func _ready() -> void:
	var hex_btn: Button = _main_panel.get_node("HexMap Button") as Button
	if hex_btn:
		hex_btn.pressed.connect(_on_open_hex_map_list)
	var pack_btn: Button = _main_panel.get_node("TilePack Button") as Button
	if pack_btn:
		pack_btn.pressed.connect(_on_open_hex_pack_list)


func _on_open_hex_map_list() -> void:
	_hex_map_list.visible = true
	_hex_pack_list.visible = false
	_main_panel.visible = false


func _on_open_hex_pack_list() -> void:
	_hex_pack_list.visible = true
	_hex_map_list.visible = false
	_main_panel.visible = false
