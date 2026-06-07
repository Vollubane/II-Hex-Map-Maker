extends Panel

## Emitted when the user validates; carries the trimmed input text. The panel frees itself immediately after.
signal text_submitted(p_text: String)

@onready var _description: Label = $Panel/VBoxContainer/Description
@onready var _line_edit: LineEdit = $Panel/VBoxContainer/HBoxContainer/LineEdit
@onready var _valid_button: Button = $Panel/VBoxContainer/HBoxContainer/ValidButton
@onready var _close_button: Button = $Panel/Button
@onready var _inner_panel: Panel = $Panel
@onready var _vbox: VBoxContainer = $Panel/VBoxContainer


func _ready() -> void:
	_close_button.pressed.connect(queue_free)
	_valid_button.pressed.connect(_on_submit)
	_line_edit.text_submitted.connect(_on_line_edit_enter)


## Sets the description label and the placeholder text; clears the input and grabs focus.
## Schedules a deferred panel resize so the inner box always contains its text on one line.
func setup(p_description: String, p_placeholder: String) -> void:
	_description.text = p_description
	_line_edit.placeholder_text = p_placeholder
	_line_edit.text = ""
	_line_edit.grab_focus()
	_fit_panel_size.call_deferred()


func _fit_panel_size() -> void:
	var min_size: Vector2 = _vbox.get_combined_minimum_size()
	var w: float = maxf(min_size.x + 11.0, 300.0)
	var h: float = maxf(min_size.y + 13.0, 80.0)
	_inner_panel.offset_left = -ceil(w * 0.5)
	_inner_panel.offset_right = ceil(w * 0.5)
	_inner_panel.offset_top = -ceil(h * 0.5)
	_inner_panel.offset_bottom = ceil(h * 0.5)


func _on_line_edit_enter(_submitted_text: String) -> void:
	_on_submit()


func _on_submit() -> void:
	text_submitted.emit(_line_edit.text.strip_edges())
	queue_free()
