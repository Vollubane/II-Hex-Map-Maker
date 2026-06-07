extends Node

## Fired to display a warning message that fades out over p_fade_duration seconds.
signal warning_event(p_text: String, p_fade_duration: float)

signal loading_event(p_loading_protocol: String, p_subloading_protocol: String, p_loading_element)
signal loading_end_event
