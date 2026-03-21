defmodule OneshotUi.MarkupEditor do
  @moduledoc false

  @behaviour :wx_object
  import Bitwise

  alias OneshotCore.Markup.Primitive
  alias OneshotCore.Markup.State
  alias OneshotUi.MarkupEditor.Result
  alias :oneshot_ui_wx_constants, as: WxConst

  @tool_pen_id 21_001
  @tool_line_id 21_002
  @tool_arrow_id 21_003
  @tool_rectangle_id 21_004
  @tool_ellipse_id 21_005
  @tool_text_id 21_006
  @undo_id 20_001
  @clear_id 20_002
  @copy_id 20_003
  @save_id 20_004
  @cancel_id 20_005

  @default_color {255, 0, 0, 255}
  @default_font ~c"Segoe UI"
  @default_font_size 20
  @default_stroke 3

  defmodule Result do
    @moduledoc false

    @enforce_keys [:bitmap]
    defstruct [:bitmap, :saved_path, :markup_state]
  end

  defstruct [
    :wx,
    :frame,
    :panel,
    :canvas,
    :bitmap,
    :base_bitmap,
    :save_path,
    :reply_to,
    :reply_ref,
    :pending_reply?,
    :tool,
    :drag_origin,
    :last_point,
    :active_points,
    :stroke_color,
    :stroke_thickness,
    :font_size,
    :font_family,
    :primitives,
    undo: []
  ]

  def open(bitmap, opts \\ []) do
    reply_ref = make_ref()
    parent = self()
    wx_env = current_wx_env()

    {:ok, pid} =
      :wx_object.start_link(
        __MODULE__,
        [bitmap: bitmap, opts: opts, reply_to: parent, reply_ref: reply_ref, wx_env: wx_env],
        []
      )

    receive do
      {^reply_ref, result} ->
        result
    after
      Keyword.get(opts, :timeout, :infinity) ->
        Process.exit(pid, :kill)
        {:error, :timeout}
    end
  end

  @impl true
  def init(args) do
    wx_env = Keyword.get(args, :wx_env)
    wx = bootstrap_wx(wx_env)

    bitmap = Keyword.fetch!(args, :bitmap)
    opts = Keyword.fetch!(args, :opts)
    reply_to = Keyword.fetch!(args, :reply_to)
    reply_ref = Keyword.fetch!(args, :reply_ref)
    save_path = Keyword.get(opts, :save_path)

    frame =
      :wxFrame.new(
        :wx.null(),
        -1,
        ~c"OneShot Markup",
        size: frame_size(bitmap),
        style: bor_flags([WxConst.default_frame_style(), WxConst.full_repaint_on_resize()])
      )

    panel = :wxPanel.new(frame)
    canvas = :wxPanel.new(panel, style: WxConst.full_repaint_on_resize())

    toolbar = :wxBoxSizer.new(WxConst.horizontal())

    buttons = [
      {:pen, :wxButton.new(panel, @tool_pen_id, label: ~c"Pen")},
      {:line, :wxButton.new(panel, @tool_line_id, label: ~c"Line")},
      {:arrow, :wxButton.new(panel, @tool_arrow_id, label: ~c"Arrow")},
      {:rectangle, :wxButton.new(panel, @tool_rectangle_id, label: ~c"Rect")},
      {:ellipse, :wxButton.new(panel, @tool_ellipse_id, label: ~c"Ellipse")},
      {:text, :wxButton.new(panel, @tool_text_id, label: ~c"Text")},
      {:undo, :wxButton.new(panel, @undo_id, label: ~c"Undo")},
      {:clear, :wxButton.new(panel, @clear_id, label: ~c"Clear")},
      {:copy, :wxButton.new(panel, @copy_id, label: ~c"Copy")},
      {:save, :wxButton.new(panel, @save_id, label: ~c"Save")},
      {:cancel, :wxButton.new(panel, @cancel_id, label: ~c"Cancel")}
    ]

    Enum.each(buttons, fn {_name, button} ->
      :wxSizer.add(toolbar, button, border: 4, flag: bor_flags([WxConst.all()]))
    end)

    root = :wxBoxSizer.new(WxConst.vertical())

    :wxSizer.add(root, toolbar, flag: bor_flags([WxConst.left(), WxConst.top()]))
    :wxSizer.add(root, canvas, proportion: 1, flag: bor_flags([WxConst.expand(), WxConst.all()]), border: 8)

    :wxWindow.setSizer(panel, root)
    :wxPanel.setBackgroundColour(canvas, {24, 24, 24, 255})
    :wxWindow.setMinSize(canvas, {:wxBitmap.getWidth(bitmap), :wxBitmap.getHeight(bitmap)})

    :wxFrame.connect(frame, :close_window)
    :wxPanel.connect(canvas, :paint, [:callback])
    :wxPanel.connect(canvas, :left_down)
    :wxPanel.connect(canvas, :left_up)
    :wxPanel.connect(canvas, :motion)
    :wxPanel.connect(canvas, :leave_window)
    :wxPanel.connect(panel, :command_button_clicked)

    :wxFrame.show(frame)

    state = %__MODULE__{
      wx: wx,
      frame: frame,
      panel: panel,
      canvas: canvas,
      bitmap: copy_bitmap(bitmap),
      base_bitmap: copy_bitmap(bitmap),
      save_path: save_path,
      reply_to: reply_to,
      reply_ref: reply_ref,
      pending_reply?: true,
      tool: :pen,
      stroke_color: @default_color,
      stroke_thickness: @default_stroke,
      font_size: @default_font_size,
      font_family: @default_font,
      primitives: []
    }

    {frame, state}
  end

  @impl true
  def handle_sync_event({:wx, _, _, _, {:wxPaint, _}}, _wx_obj, state) do
    dc = :wxPaintDC.new(state.canvas)
    redraw(dc, state.bitmap)
    :wxPaintDC.destroy(dc)
    :ok
  end

  @impl true
  def handle_event({:wx, _, _, _, {:wxClose, :close_window}}, state) do
    {:stop, :normal, cancel(state)}
  end

  def handle_event({:wx, _, _, _, {:wxCommand, :command_button_clicked, _, _, id, _, _}}, state) do
    dispatch_button(id, state)
  end

  def handle_event({:wx, _, _, _, {:wxMouse, :left_down, x, y, _, _, _, _, _}}, %{tool: :text} = state) do
    case prompt_text(state.frame) do
      nil ->
        {:noreply, state}

      text ->
        next =
          state
          |> push_undo()
          |> draw_text({x, y}, text)

        {:noreply, next}
    end
  end

  def handle_event({:wx, _, _, _, {:wxMouse, :left_down, x, y, _, _, _, _, _}}, state) do
    :wxWindow.captureMouse(state.canvas)

    next =
      state
      |> push_undo()
      |> Map.put(:drag_origin, {x, y})
      |> Map.put(:last_point, {x, y})
      |> Map.put(:active_points, [{x, y}])

    {:noreply, next}
  end

  def handle_event({:wx, _, _, _, {:wxMouse, :left_up, x, y, _, _, _, _, _}}, state) do
    release_mouse(state.canvas)
    {:noreply, finalize_pointer_action(state, {x, y})}
  end

  def handle_event({:wx, _, _, _, {:wxMouse, :leave_window, _, _, _, _, _, _, _}}, state) do
    release_mouse(state.canvas)
    {:noreply, %{state | drag_origin: nil, last_point: nil, active_points: []}}
  end

  def handle_event({:wx, _, _, _, {:wxMouse, :motion, _x, _y, _, _, _, _, _}}, %{drag_origin: nil} = state) do
    {:noreply, state}
  end

  def handle_event({:wx, _, _, _, {:wxMouse, :motion, x, y, _, _, _, _, _}}, %{tool: :pen} = state) do
    next =
      state
      |> draw_pen_segment(state.last_point, {x, y})
      |> Map.put(:last_point, {x, y})
      |> update_in([Access.key(:active_points)], fn points -> points ++ [{x, y}] end)

    {:noreply, next}
  end

  def handle_event({:wx, _, _, _, {:wxMouse, :motion, _, _, _, _, _, _, _}}, state) do
    {:noreply, state}
  end

  def handle_event(_event, state) do
    {:noreply, state}
  end

  @impl true
  def handle_info(_message, state) do
    {:noreply, state}
  end

  @impl true
  def handle_call(_request, _from, state) do
    {:reply, :ok, state}
  end

  @impl true
  def handle_cast(_message, state) do
    {:noreply, state}
  end

  @impl true
  def terminate(_reason, state) do
    maybe_cancel(state)
    destroy_bitmap(state.bitmap)
    destroy_bitmap(state.base_bitmap)

    Enum.each(state.undo, fn {bitmap, _primitives} ->
      destroy_bitmap(bitmap)
    end)

    safe_destroy(fn -> :wxFrame.destroy(state.frame) end)
    safe_destroy(fn -> :wx.destroy() end)
    :ok
  end

  @impl true
  def code_change(_old_vsn, state, _extra) do
    {:ok, state}
  end

  defp dispatch_button(@tool_pen_id, state), do: {:noreply, %{state | tool: :pen}}
  defp dispatch_button(@tool_line_id, state), do: {:noreply, %{state | tool: :line}}
  defp dispatch_button(@tool_arrow_id, state), do: {:noreply, %{state | tool: :arrow}}
  defp dispatch_button(@tool_rectangle_id, state), do: {:noreply, %{state | tool: :rectangle}}
  defp dispatch_button(@tool_ellipse_id, state), do: {:noreply, %{state | tool: :ellipse}}
  defp dispatch_button(@tool_text_id, state), do: {:noreply, %{state | tool: :text}}
  defp dispatch_button(@undo_id, state), do: {:noreply, apply_undo(state)}
  defp dispatch_button(@clear_id, state), do: {:noreply, clear_canvas(state)}

  defp dispatch_button(@copy_id, state) do
    copy_to_clipboard(state.bitmap)
    {:noreply, state}
  end

  defp dispatch_button(@save_id, state) do
    case persist_bitmap(state) do
      {:ok, saved_path} ->
        {:stop, :normal,
         reply_ok(state, %Result{
           bitmap: copy_bitmap(state.bitmap),
           saved_path: saved_path,
           markup_state: %State{tool: state.tool, primitives: state.primitives}
         })}

      {:error, _reason} ->
        {:noreply, state}
    end
  end

  defp dispatch_button(@cancel_id, state), do: {:stop, :normal, cancel(state)}
  defp dispatch_button(_id, state), do: {:noreply, state}

  defp current_wx_env do
    try do
      :wx.get_env()
    catch
      _, _ -> nil
    end
  end

  defp bootstrap_wx(nil), do: :wx.new()

  defp bootstrap_wx(wx_env) do
    :wx.set_env(wx_env)
    :wx.new()
  rescue
    _ -> :wx.new()
  end

  defp frame_size(bitmap) do
    width = min(:wxBitmap.getWidth(bitmap) + 40, 1_600)
    height = min(:wxBitmap.getHeight(bitmap) + 120, 1_000)
    {max(width, 520), max(height, 420)}
  end

  defp bor_flags(flags) do
    Enum.reduce(flags, 0, fn flag, acc -> acc ||| flag end)
  end

  defp finalize_pointer_action(%{drag_origin: nil} = state, _point), do: state

  defp finalize_pointer_action(%{tool: :pen} = state, point) do
    release = %{state | drag_origin: nil, last_point: nil, active_points: []}

    if length(state.active_points) >= 2 do
      primitive = %Primitive.Pen{
        points: state.active_points ++ [point],
        stroke_color: state.stroke_color,
        stroke_thickness: state.stroke_thickness
      }

      %{release | primitives: release.primitives ++ [primitive]}
    else
      release
    end
  end

  defp finalize_pointer_action(state, point) do
    next =
      case build_shape_primitive(state, state.drag_origin, point) do
        nil ->
          state

        primitive ->
          state
          |> render_primitive(primitive)
          |> update_in([Access.key(:primitives)], &(&1 ++ [primitive]))
      end

    %{next | drag_origin: nil, last_point: nil, active_points: []}
  end

  defp build_shape_primitive(%{tool: :line} = state, start, stop) do
    %Primitive.Line{
      start: start,
      stop: stop,
      stroke_color: state.stroke_color,
      stroke_thickness: state.stroke_thickness
    }
  end

  defp build_shape_primitive(%{tool: :arrow} = state, start, stop) do
    %Primitive.Arrow{
      start: start,
      stop: stop,
      stroke_color: state.stroke_color,
      stroke_thickness: state.stroke_thickness
    }
  end

  defp build_shape_primitive(%{tool: :rectangle} = state, start, stop) do
    %Primitive.Rectangle{
      bounds: bounds_rect(start, stop),
      stroke_color: state.stroke_color,
      stroke_thickness: state.stroke_thickness,
      fill_color: nil
    }
  end

  defp build_shape_primitive(%{tool: :ellipse} = state, start, stop) do
    %Primitive.Ellipse{
      bounds: bounds_rect(start, stop),
      stroke_color: state.stroke_color,
      stroke_thickness: state.stroke_thickness,
      fill_color: nil
    }
  end

  defp build_shape_primitive(_state, _start, _stop), do: nil

  defp push_undo(state) do
    snapshot = {copy_bitmap(state.bitmap), state.primitives}
    %{state | undo: [snapshot | Enum.take(state.undo, 19)]}
  end

  defp apply_undo(%{undo: [{previous, primitives} | rest]} = state) do
    destroy_bitmap(state.bitmap)
    refreshed = copy_bitmap(previous)
    :wxWindow.refresh(state.canvas)
    destroy_bitmap(previous)
    %{state | bitmap: refreshed, undo: rest, drag_origin: nil, last_point: nil, active_points: [], primitives: primitives}
  end

  defp apply_undo(state), do: state

  defp clear_canvas(state) do
    destroy_bitmap(state.bitmap)
    refreshed = copy_bitmap(state.base_bitmap)
    :wxWindow.refresh(state.canvas)
    %{push_undo(%{state | bitmap: refreshed, drag_origin: nil, last_point: nil, active_points: [], primitives: []}) | bitmap: refreshed}
  end

  defp draw_pen_segment(state, nil, _current), do: state

  defp draw_pen_segment(state, start, stop) do
    primitive = %Primitive.Line{
      start: start,
      stop: stop,
      stroke_color: state.stroke_color,
      stroke_thickness: state.stroke_thickness
    }

    render_primitive(state, primitive)
  end

  defp draw_text(state, {x, y}, text) do
    primitive = %Primitive.Text{
      text: text,
      position: {x, y},
      font_family_name: List.to_string(state.font_family),
      font_size: state.font_size,
      font_color: state.stroke_color
    }

    state
    |> render_primitive(primitive)
    |> update_in([Access.key(:primitives)], &(&1 ++ [primitive]))
  end

  defp render_primitive(state, primitive) do
    draw_on_bitmap(state.bitmap, fn dc ->
      apply_primitive(dc, primitive)
    end)

    :wxWindow.refresh(state.canvas)
    state
  end

  defp apply_primitive(dc, %Primitive.Line{start: start, stop: stop} = primitive) do
    with_pen(dc, primitive.stroke_color, primitive.stroke_thickness, fn ->
      :wxDC.drawLine(dc, start, stop)
    end)
  end

  defp apply_primitive(dc, %Primitive.Arrow{start: start, stop: stop} = primitive) do
    with_pen(dc, primitive.stroke_color, primitive.stroke_thickness, fn ->
      :wxDC.drawLine(dc, start, stop)

      {hx1, hy1, hx2, hy2} = arrow_head(start, stop)
      :wxDC.drawLine(dc, stop, {hx1, hy1})
      :wxDC.drawLine(dc, stop, {hx2, hy2})
    end)
  end

  defp apply_primitive(dc, %Primitive.Rectangle{bounds: {x, y, width, height}} = primitive) do
    with_outline(dc, primitive.stroke_color, primitive.stroke_thickness, fn ->
      :wxDC.drawRectangle(dc, {x, y, width, height})
    end)
  end

  defp apply_primitive(dc, %Primitive.Ellipse{bounds: {x, y, width, height}} = primitive) do
    with_outline(dc, primitive.stroke_color, primitive.stroke_thickness, fn ->
      :wxDC.drawEllipse(dc, {x, y, width, height})
    end)
  end

  defp apply_primitive(dc, %Primitive.Text{} = primitive) do
    font =
      :wxFont.new(
        primitive.font_size,
        WxConst.fontfamily_default(),
        WxConst.fontstyle_normal(),
        WxConst.fontweight_normal(),
        faceName: String.to_charlist(primitive.font_family_name)
      )

    :wxDC.setFont(dc, font)
    :wxDC.setTextForeground(dc, primitive.font_color)
    :wxDC.drawText(dc, String.to_charlist(primitive.text), primitive.position)
    :wxFont.destroy(font)
  end

  defp apply_primitive(dc, %Primitive.Pen{points: [first | rest]} = primitive) do
    with_pen(dc, primitive.stroke_color, primitive.stroke_thickness, fn ->
      Enum.reduce(rest, first, fn point, previous ->
        :wxDC.drawLine(dc, previous, point)
        point
      end)
    end)
  end

  defp apply_primitive(_dc, _primitive), do: :ok

  defp with_pen(dc, color, width, fun) do
    pen = :wxPen.new(color, width: width)
    :wxDC.setPen(dc, pen)
    fun.()
    :wxPen.destroy(pen)
  end

  defp with_outline(dc, color, width, fun) do
    pen = :wxPen.new(color, width: width)
    brush = :wxBrush.new({0, 0, 0, 0})
    :wxDC.setPen(dc, pen)
    :wxDC.setBrush(dc, brush)
    fun.()
    :wxBrush.destroy(brush)
    :wxPen.destroy(pen)
  end

  defp arrow_head({x0, y0}, {x1, y1}) do
    angle = :math.atan2(y1 - y0, x1 - x0)
    head = 14
    spread = :math.pi() / 7

    hx1 = round(x1 - head * :math.cos(angle - spread))
    hy1 = round(y1 - head * :math.sin(angle - spread))
    hx2 = round(x1 - head * :math.cos(angle + spread))
    hy2 = round(y1 - head * :math.sin(angle + spread))

    {hx1, hy1, hx2, hy2}
  end

  defp bounds_rect({x0, y0}, {x1, y1}) do
    {min(x0, x1), min(y0, y1), max(abs(x1 - x0), 1), max(abs(y1 - y0), 1)}
  end

  defp prompt_text(frame) do
    dialog = :wxTextEntryDialog.new(frame, ~c"Text", caption: ~c"Add Text")

    try do
      if :wxTextEntryDialog.showModal(dialog) == WxConst.id_ok() do
        dialog
        |> :wxTextEntryDialog.getValue()
        |> List.to_string()
        |> case do
          "" -> nil
          value -> value
        end
      else
        nil
      end
    after
      :wxTextEntryDialog.destroy(dialog)
    end
  end

  defp draw_on_bitmap(bitmap, fun) do
    dc = :wxMemoryDC.new(bitmap)
    fun.(dc)
    :wxMemoryDC.destroy(dc)
    :ok
  end

  defp redraw(dc, bitmap) do
    memory = :wxMemoryDC.new(bitmap)
    :wxDC.blit(dc, {0, 0}, {:wxBitmap.getWidth(bitmap), :wxBitmap.getHeight(bitmap)}, memory, {0, 0})
    :wxMemoryDC.destroy(memory)
  end

  defp persist_bitmap(state) do
    path =
      state.save_path ||
        choose_save_path(state.frame)

    if is_binary(path) do
      case :wxBitmap.saveFile(state.bitmap, String.to_charlist(path), WxConst.bitmap_type_png()) do
        true -> {:ok, path}
        false -> {:error, :save_failed}
      end
    else
      {:error, :cancelled}
    end
  end

  defp choose_save_path(frame) do
    dialog =
      :wxFileDialog.new(
        frame,
        message: ~c"Save Markup",
        wildcard: ~c"PNG files (*.png)|*.png",
        style: bor_flags([WxConst.fd_save(), WxConst.fd_overwrite_prompt()])
      )

    try do
      if :wxFileDialog.showModal(dialog) == WxConst.id_ok() do
        dialog
        |> :wxFileDialog.getPath()
        |> List.to_string()
      else
        nil
      end
    after
      :wxFileDialog.destroy(dialog)
    end
  end

  defp copy_to_clipboard(bitmap) do
    clipboard = :wxClipboard.get()

    if :wxClipboard.open(clipboard) do
      data = :wxBitmapDataObject.new(bitmap)
      :wxClipboard.setData(clipboard, data)
      :wxClipboard.flush(clipboard)
      :wxClipboard.close(clipboard)
    end
  rescue
    _ -> :ok
  end

  defp copy_bitmap(bitmap) do
    image = :wxBitmap.convertToImage(bitmap)
    copy = :wxBitmap.new(image)
    :wxImage.destroy(image)
    copy
  end

  defp destroy_bitmap(nil), do: :ok

  defp destroy_bitmap(bitmap) do
    safe_destroy(fn -> :wxBitmap.destroy(bitmap) end)
  end

  defp release_mouse(canvas) do
    safe_destroy(fn -> :wxWindow.releaseMouse(canvas) end)
  end

  defp reply_ok(state, result) do
    send(state.reply_to, {state.reply_ref, {:ok, result}})
    %{state | pending_reply?: false}
  end

  defp cancel(state) do
    send(state.reply_to, {state.reply_ref, :cancel})
    %{state | pending_reply?: false}
  end

  defp maybe_cancel(%{pending_reply?: true} = state), do: cancel(state)
  defp maybe_cancel(state), do: state

  defp safe_destroy(fun) do
    fun.()
    :ok
  rescue
    _ -> :ok
  catch
    _, _ -> :ok
  end
end
