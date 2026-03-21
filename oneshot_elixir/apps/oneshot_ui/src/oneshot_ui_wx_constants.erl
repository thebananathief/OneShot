-module(oneshot_ui_wx_constants).

-include_lib("wx/include/wx.hrl").

-export([
    all/0,
    bitmap_type_png/0,
    default_frame_style/0,
    expand/0,
    fd_overwrite_prompt/0,
    fd_save/0,
    fontfamily_default/0,
    fontstyle_normal/0,
    fontweight_normal/0,
    full_repaint_on_resize/0,
    horizontal/0,
    id_ok/0,
    left/0,
    top/0,
    vertical/0
]).

all() -> ?wxALL.
bitmap_type_png() -> ?wxBITMAP_TYPE_PNG.
default_frame_style() -> ?wxDEFAULT_FRAME_STYLE.
expand() -> ?wxEXPAND.
fd_overwrite_prompt() -> ?wxFD_OVERWRITE_PROMPT.
fd_save() -> ?wxFD_SAVE.
fontfamily_default() -> ?wxFONTFAMILY_DEFAULT.
fontstyle_normal() -> ?wxFONTSTYLE_NORMAL.
fontweight_normal() -> ?wxFONTWEIGHT_NORMAL.
full_repaint_on_resize() -> ?wxFULL_REPAINT_ON_RESIZE.
horizontal() -> ?wxHORIZONTAL.
id_ok() -> ?wxID_OK.
left() -> ?wxLEFT.
top() -> ?wxTOP.
vertical() -> ?wxVERTICAL.
