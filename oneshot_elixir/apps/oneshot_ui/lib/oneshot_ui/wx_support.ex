defmodule OneshotUi.WxSupport do
  @moduledoc false

  require Record

  Record.defrecord(:wx, Record.extract(:wx, from_lib: "wx/include/wx.hrl"))
  Record.defrecord(:wxMouse, Record.extract(:wxMouse, from_lib: "wx/include/wx.hrl"))
  Record.defrecord(:wxPaint, Record.extract(:wxPaint, from_lib: "wx/include/wx.hrl"))
  Record.defrecord(:wxSize, Record.extract(:wxSize, from_lib: "wx/include/wx.hrl"))
  Record.defrecord(:wxClose, Record.extract(:wxClose, from_lib: "wx/include/wx.hrl"))
  Record.defrecord(:wxKey, Record.extract(:wxKey, from_lib: "wx/include/wx.hrl"))
  Record.defrecord(:wxCommand, Record.extract(:wxCommand, from_lib: "wx/include/wx.hrl"))
end
