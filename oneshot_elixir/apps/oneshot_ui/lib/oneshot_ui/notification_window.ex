defmodule OneshotUi.NotificationWindow do
  @moduledoc false

  @behaviour :wx_object

  import OneshotUi.WxSupport

  @markup_id 20_001
  @dismiss_id 20_002
  @stay_on_top 32_768
  @frame_no_taskbar 2
  @border_simple 33_554_432

  defstruct [
    :manager,
    :frame,
    :panel,
    :image,
    :label,
    :bitmap,
    :thumbnail,
    :path,
    :temp_file
  ]

  def start_link(manager, bitmap, path, temp_file) do
    :wx_object.start_link(__MODULE__, {manager, bitmap, path, temp_file}, [])
  end

  def measure(pid) do
    :wx_object.call(pid, :measure)
  end

  def move_to(pid, {left, top}) do
    :wx_object.call(pid, {:move_to, left, top})
  end

  def init({manager, bitmap, path, temp_file}) do
    thumb = OneshotUi.Output.thumbnail(bitmap)
    frame = :wxFrame.new(:wx.null(), -1, ~c"OneShot", style: @stay_on_top + @frame_no_taskbar + @border_simple)
    panel = :wxPanel.new(frame)
    sizer = :wxBoxSizer.new(8)
    actions = :wxBoxSizer.new(4)

    image = :wxStaticBitmap.new(panel, -1, thumb)
    markup = :wxButton.new(panel, @markup_id, label: ~c"Markup")
    dismiss = :wxButton.new(panel, @dismiss_id, label: ~c"Dismiss")
    text = :wxStaticText.new(panel, -1, String.to_charlist(Path.basename(path)))

    :wxPanel.connect(panel, :command_button_clicked)
    :wxSizer.add(actions, markup)
    :wxSizer.add(actions, dismiss)
    :wxSizer.add(sizer, image, proportion: 0, flag: 16, border: 8)
    :wxSizer.add(sizer, text, proportion: 0, flag: 16, border: 8)
    :wxSizer.add(sizer, actions, proportion: 0, flag: 16, border: 8)
    :wxPanel.setSizer(panel, sizer)
    :wxSizer.fit(sizer, frame)

    :wxFrame.connect(frame, :close_window)
    :wxWindow.show(frame)
    :wxWindow.raise(frame)

    {frame,
     %__MODULE__{
       manager: manager,
       frame: frame,
       panel: panel,
       image: image,
       label: text,
       bitmap: bitmap,
       thumbnail: thumb,
       path: path,
       temp_file: temp_file
     }}
  end

  def handle_event(event = wx(id: @dismiss_id, event: wxCommand(type: :command_button_clicked)), state) do
    _ = event
    :wxFrame.destroy(state.frame)
    {:stop, :normal, state}
  end

  def handle_event(event = wx(id: @markup_id, event: wxCommand(type: :command_button_clicked)), state) do
    _ = event

    case OneshotUi.MarkupEditor.open(state.bitmap, save_path: state.path) do
      {:ok, %OneshotUi.MarkupEditor.Result{bitmap: updated_bitmap, saved_path: updated_path}} ->
        thumbnail = OneshotUi.Output.thumbnail(updated_bitmap)
        {:ok, temp_file} = OneshotUi.Output.write_temp_drag_file(updated_bitmap, state.temp_file)
        OneshotUi.ScreenCapture.destroy_bitmap(state.bitmap)
        OneshotUi.ScreenCapture.destroy_bitmap(state.thumbnail)
        :wxStaticBitmap.setBitmap(state.image, thumbnail)
        :wxStaticText.setLabel(state.label, String.to_charlist(Path.basename(updated_path || state.path)))
        send(state.manager, {:notification_updated, self(), updated_path || state.path, temp_file})

        {:noreply,
         %{
           state
           | bitmap: updated_bitmap,
             thumbnail: thumbnail,
             path: updated_path || state.path,
             temp_file: temp_file
         }}

      :cancel ->
        {:noreply, state}

      _ ->
        {:noreply, state}
    end
  end

  def handle_event(event = wx(event: wxClose()), state) do
    _ = event
    {:stop, :normal, state}
  end

  def handle_event(_event, state) do
    {:noreply, state}
  end

  def handle_call(:measure, _from, state) do
    {:reply, :wxWindow.getSize(state.frame), state}
  end

  def handle_call({:move_to, left, top}, _from, state) do
    :wxWindow.move(state.frame, {left, top})
    {:reply, :ok, state}
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
    send(state.manager, {:notification_closed, self(), state.temp_file})
    OneshotUi.ScreenCapture.destroy_bitmap(state.thumbnail)
    OneshotUi.ScreenCapture.destroy_bitmap(state.bitmap)
    :ok
  end

  def code_change(_old_vsn, state, _extra) do
    {:ok, state}
  end
end
