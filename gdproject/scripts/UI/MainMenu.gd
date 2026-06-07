extends Control

const AssetManagerScene: PackedScene = preload("res://Scene/UI Node/AssetManager.tscn")

@onready var _main_panel: Panel = $MainPanel
@onready var _hex_map_list: Panel = $HexMapList
@onready var _hex_pack_list: Panel = $HexPackList

var _asset_manager: Control = null


func _ready() -> void:
	var hex_btn: Button = _main_panel.get_node("HexMap Button") as Button
	if hex_btn:
		hex_btn.pressed.connect(_on_open_hex_map_list)
	var pack_btn: Button = _main_panel.get_node("TilePack Button") as Button
	if pack_btn:
		pack_btn.pressed.connect(_on_open_hex_pack_list)
	var am_btn: Button = _main_panel.get_node("AssetManager Button") as Button
	if am_btn:
		am_btn.pressed.connect(_on_open_asset_manager)


func _on_open_hex_map_list() -> void:
	_hex_map_list.visible = true
	_hex_pack_list.visible = false
	_main_panel.visible = false


func _on_open_hex_pack_list() -> void:
	_hex_pack_list.visible = true
	_hex_map_list.visible = false
	_main_panel.visible = false


func _on_open_asset_manager() -> void:
	if _asset_manager != null:
		return
	_asset_manager = AssetManagerScene.instantiate() as Control
	add_child(_asset_manager)
	var wl := get_node_or_null("WarningLabel")
	if wl:
		move_child(wl, get_child_count() - 1)
	_main_panel.visible = false
	_asset_manager.close_requested.connect(_on_asset_manager_closed, CONNECT_ONE_SHOT)


func _on_asset_manager_closed() -> void:
	if _asset_manager:
		_asset_manager.queue_free()
		_asset_manager = null
	_main_panel.visible = true
