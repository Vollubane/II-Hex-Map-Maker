class_name AssetPackDisplayer
extends PanelContainer

## Émis quand l’état du bouton bascule (toggle_mode).
signal toggled(pressed: bool)
## Émis sur double-clic gauche sur la zone principale (ouverture / action forte).
signal double_clicked
signal delete_pressed
signal title_submitted(new_title: String)

@onready var _main_button: Button = $MainButton
@onready var _title_label: Label = $MainButton/HBoxContainer/VBoxLabels/TitleSlot/TitleLabel
@onready var _title_line_edit: LineEdit = $MainButton/HBoxContainer/VBoxLabels/TitleSlot/TitleLineEdit
@onready var _info_label: Label = $MainButton/HBoxContainer/VBoxLabels/InfoLabel
@onready var _delete_button: Button = $MainButton/HBoxContainer/VBoxLabels2/DeleteButton
@onready var _rename_button: Button = $MainButton/HBoxContainer/VBoxLabels2/RenameButton

## Si false : le bouton supprimer est masqué et [signal delete_pressed] n’est pas émis.
@export var connect_delete_pressed: bool = true

## Optionnel si le nœud MainButton/HBoxContainer/PreviewTexture est absent de la scène.
var _preview: TextureRect

var _saved_title: String = ""
var _suppress_focus_exit: bool = false

const _FORBIDDEN_FILENAME_CHARS: Array[String] = ["/", "\\", "\"", ":", "*", "?", "<", ">", "|"]


func _title_contains_forbidden_filename_chars(s: String) -> bool:
	for ch in _FORBIDDEN_FILENAME_CHARS:
		if s.contains(ch):
			return true
	return false


func _ready() -> void:
	_preview = get_node_or_null("MainButton/HBoxContainer/PreviewTexture") as TextureRect
	_main_button.toggled.connect(_on_main_button_toggled)
	_main_button.gui_input.connect(_on_main_button_gui_input)
	if connect_delete_pressed:
		_delete_button.pressed.connect(_on_delete_pressed)
	else:
		_delete_button.visible = false
	_rename_button.pressed.connect(_begin_title_edit)
	_title_line_edit.text_submitted.connect(_on_title_line_edit_submitted)
	_title_line_edit.focus_exited.connect(_on_title_line_edit_focus_exited)


func _input(event: InputEvent) -> void:
	if not _title_line_edit.visible:
		return
	var global_pt: Vector2
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if not mb.pressed or mb.button_index != MOUSE_BUTTON_LEFT:
			return
		global_pt = mb.global_position
	elif event is InputEventScreenTouch:
		var st := event as InputEventScreenTouch
		if not st.pressed:
			return
		global_pt = st.position
	else:
		return
	if _title_line_edit.get_global_rect().has_point(global_pt):
		return
	_cancel_title_edit()
	get_viewport().set_input_as_handled()


func _on_main_button_toggled(button_pressed: bool) -> void:
	toggled.emit(button_pressed)


func _on_main_button_gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if (
			mb.button_index == MOUSE_BUTTON_LEFT
			and mb.pressed
			and mb.double_click
		):
			double_clicked.emit()


func set_title(text: String) -> void:
	_title_label.text = text
	_title_line_edit.text = text


func set_info(text: String) -> void:
	_info_label.text = text


func set_title_and_info(title: String, info: String) -> void:
	set_title(title)
	set_info(info)


func set_preview_texture(tex: Texture2D) -> void:
	if _preview:
		_preview.texture = tex


func set_button_pressed(on: bool) -> void:
	_main_button.set_pressed_no_signal(on)


## Relâche le bouton (état « off »). Émet [signal toggled] si l’état change.
func unpress_button() -> void:
	if not _main_button.button_pressed:
		return
	_main_button.set_pressed_no_signal(false)


func is_button_pressed() -> bool:
	return _main_button.button_pressed


func _begin_title_edit() -> void:
	_saved_title = _title_label.text
	_title_line_edit.text = _saved_title
	_title_label.visible = false
	_title_line_edit.visible = true
	set_process_input(true)
	_title_line_edit.grab_focus()
	_title_line_edit.call_deferred("select_all")


func _cancel_title_edit() -> void:
	if not _title_line_edit.visible:
		return
	_title_line_edit.text = _saved_title
	_title_label.text = _saved_title
	_title_line_edit.visible = false
	_title_label.visible = true
	set_process_input(false)


func _on_title_line_edit_submitted(new_text: String) -> void:
	if _title_contains_forbidden_filename_chars(new_text):
		_suppress_focus_exit = true
		_cancel_title_edit()
		call_deferred("_clear_suppress_focus_deferred")
		return
	_suppress_focus_exit = true
	_title_label.text = new_text
	_title_line_edit.text = new_text
	_title_line_edit.visible = false
	_title_label.visible = true
	set_process_input(false)
	title_submitted.emit(new_text)
	call_deferred("_clear_suppress_focus_deferred")


func _clear_suppress_focus_deferred() -> void:
	_suppress_focus_exit = false


func _on_title_line_edit_focus_exited() -> void:
	if _suppress_focus_exit:
		return
	if not _title_line_edit.visible:
		return
	_cancel_title_edit()


func _on_delete_pressed() -> void:
	delete_pressed.emit()
