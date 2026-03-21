defmodule OneshotUi.SelectionOverlay do
  @moduledoc false

  @behaviour :wx_object

  import OneshotUi.WxSupport

  @stay_on_top 32_768
  @frame_no_taskbar 2
  @full_repaint_on_resize 65_536
  @border_none 2_097_152
  @wxk_escape 27

  defstruct [
    :frame,
    :panel,
    :bitmap,
    :geometry,
    :owner,
    :ref,
    :drag_start,
    :drag_current
  ]

  def select(bitmap, geometry) do
    owner = self()
    ref = make_ref()
    {:ok, _pid} = :wx_object.start_link(__MODULE__, {owner, ref, bitmap, geometry}, [])

    receive do
      {^ref, result} -> result
    after
      600_000 -> :cancel
    end
  end

  def init({owner, ref, bitmap, geometry}) do
    style = @stay_on_top + @frame_no_taskbar + @border_none

    frame =
      :wxFrame.new(
        :wx.null(),
        -1,
        ~c"OneShot Selection",
        pos: {geometry.left, geometry.top},
        size: {geometry.width, geometry.height},
        style: style
      )

    panel = :wxPanel.new(frame, style: @full_repaint_on_resize)
    :wxPanel.connect(panel, :paint, [:callback])
    :wxPanel.connect(panel, :left_down)
    :wxPanel.connect(panel, :left_up)
    :wxPanel.connect(panel, :motion)
    :wxPanel.connect(panel, :right_down)
    :wxFrame.connect(frame, :close_window)
    :wxFrame.connect(frame, :char_hook)
    :wxFrame.setTransparent(frame, 235)
    :wxWindow.show(frame)
    :wxWindow.raise(frame)
    :wxWindow.setFocus(panel)

    state = %__MODULE__{
      frame: frame,
      panel: panel,
      bitmap: bitmap,
      geometry: geometry,
      owner: owner,
      ref: ref
    }

    {frame, state}
  end

  def handle_sync_event(event = wx(event: wxPaint()), _wx_obj, state) do
    _ = event
    dc = :wxPaintDC.new(state.panel)
    :wxDC.drawBitmap(dc, state.bitmap, {0, 0})

    if rect = rect_from_state(state) do
      pen = :wxPen.new({0, 255, 0, 255}, width: 2)
      brush = :wxBrush.new({0, 0, 0, 0})
      :wxDC.setPen(dc, pen)
      :wxDC.setBrush(dc, brush)
      :wxDC.drawRectangle(dc, {rect.left, rect.top, rect.width, rect.height})
      :wxBrush.destroy(brush)
      :wxPen.destroy(pen)
    end

    :wxPaintDC.destroy(dc)
    :ok
  end

  def handle_event(event = wx(event: wxMouse(type: :left_down, x: x, y: y)), state) do
    _ = event
    :wxPanel.captureMouse(state.panel)
    :wxPanel.refresh(state.panel)
    {:noreply, %{state | drag_start: {x, y}, drag_current: {x, y}}}
  end

  def handle_event(event = wx(event: wxMouse(type: :motion) = mouse), state) do
    _ = event

    if state.drag_start && :wxMouseEvent.leftIsDown(mouse) do
      x = :wxMouseEvent.getX(mouse)
      y = :wxMouseEvent.getY(mouse)
      :wxPanel.refresh(state.panel)
      {:noreply, %{state | drag_current: {x, y}}}
    else
      {:noreply, state}
    end
  end

  def handle_event(event = wx(event: wxMouse(type: :left_up, x: x, y: y)), state) do
    _ = event
    release_mouse(state.panel)

    case selection_rect(state.drag_start, {x, y}, state.geometry) do
      nil ->
        send(state.owner, {state.ref, :cancel})
        :wxFrame.destroy(state.frame)
        {:stop, :normal, state}

      rect ->
        send(state.owner, {state.ref, {:ok, rect}})
        :wxFrame.destroy(state.frame)
        {:stop, :normal, state}
    end
  end

  def handle_event(event = wx(event: wxMouse(type: :right_down)), state) do
    _ = event
    send(state.owner, {state.ref, :cancel})
    :wxFrame.destroy(state.frame)
    {:stop, :normal, state}
  end

  def handle_event(event = wx(event: wxKey(keyCode: @wxk_escape)), state) do
    _ = event
    send(state.owner, {state.ref, :cancel})
    :wxFrame.destroy(state.frame)
    {:stop, :normal, state}
  end

  def handle_event(event = wx(event: wxClose()), state) do
    _ = event
    send(state.owner, {state.ref, :cancel})
    {:stop, :normal, state}
  end

  def handle_event(_event, state) do
    {:noreply, state}
  end

  def handle_call(_request, _from, state) do
    {:reply, :ok, state}
  end

  def handle_cast(_message, state) do
    {:noreply, state}
  end

  def handle_info(_message, state) do
    {:noreply, state}
  end

  def terminate(_reason, state) do
    release_mouse(state.panel)
    :ok
  end

  def code_change(_old_vsn, state, _extra) do
    {:ok, state}
  end

  defp rect_from_state(state) do
    selection_rect(state.drag_start, state.drag_current, state.geometry, false)
  end

  defp selection_rect(start, current, geometry, absolute \\ true)

  defp selection_rect(nil, _current, _geometry, _absolute), do: nil
  defp selection_rect(_start, nil, _geometry, _absolute), do: nil

  defp selection_rect({x0, y0}, {x1, y1}, geometry, absolute) do
    left = min(x0, x1)
    top = min(y0, y1)
    width = abs(x1 - x0)
    height = abs(y1 - y0)

    if width < 3 or height < 3 do
      nil
    else
      if absolute do
        %{left: left + geometry.left, top: top + geometry.top, width: width, height: height}
      else
        %{left: left, top: top, width: width, height: height}
      end
    end
  end

  defp release_mouse(panel) do
    :wxPanel.releaseMouse(panel)
    :ok
  rescue
    _ -> :ok
  end
end
