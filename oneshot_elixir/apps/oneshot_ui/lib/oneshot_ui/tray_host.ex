defmodule OneshotUi.TrayHost do
  @moduledoc false

  @behaviour :wx_object

  import OneshotUi.WxSupport

  require Logger

  @take_snapshot_id 10_001
  @diagnostics_id 10_002
  @exit_id 10_003

  defstruct [
    :wx,
    :frame,
    :icon,
    :menu,
    :notification,
    menu_ids: %{}
  ]

  def start_link(_args) do
    :wx_object.start_link(__MODULE__, [], [])
  end

  @impl true
  def init(_args) do
    wx = :wx.new()
    frame = :wxFrame.new(:wx.null(), -1, ~c"OneShot", style: 0)
    menu = build_menu()
    :wxMenu.connect(menu, :command_menu_selected)

    icon = :wxTaskBarIcon.new(createPopupMenu: fn -> menu end)
    :wxTaskBarIcon.connect(icon, :taskbar_left_dclick)
    set_icon(icon)

    notification = build_notification("Resident daemon is running")
    :wxNotificationMessage.show(notification)

    :ok = OneshotCore.Events.subscribe(:snapshot)
    :ok = OneshotCore.Events.subscribe(:command)

    state = %__MODULE__{
      wx: wx,
      frame: frame,
      icon: icon,
      menu: menu,
      notification: notification,
      menu_ids: %{
        @take_snapshot_id => :snapshot,
        @diagnostics_id => :diagnostics,
        @exit_id => :exit
      }
    }

    {frame, state}
  end

  @impl true
  def handle_event(wx(id: id, event: wxCommand(type: :command_menu_selected)), state) do
    case Map.get(state.menu_ids, id) do
      :snapshot ->
        :ok = OneshotCore.CaptureCoordinator.request_snapshot(:tray)
        {:noreply, state}

      :diagnostics ->
        state = replace_notification(state, OneshotCore.Diagnostics.summary())
        {:noreply, state}

      :exit ->
        {:stop, :normal, state}

      _ ->
        {:noreply, state}
    end
  end

  def handle_event({:wx, _, _, _, {:wxTaskBarIcon, :taskbar_left_dclick}}, state) do
    :ok = OneshotCore.CaptureCoordinator.request_snapshot(:taskbar)
    {:noreply, state}
  end

  def handle_event(_event, state) do
    {:noreply, state}
  end

  @impl true
  def handle_info({:oneshot_event, :snapshot, payload}, state) do
    message =
      case payload do
        %{status: :requested, source: source} -> "Snapshot requested via #{source}"
        other -> inspect(other)
      end

    {:noreply, replace_notification(state, message)}
  end

  def handle_info({:oneshot_event, :command, %{command: "snapshot"}}, state) do
    case OneshotUi.SnapshotFlow.run() do
      {:ok, path} ->
        {:noreply, replace_notification(state, "Saved screenshot to #{path}")}

      :cancel ->
        {:noreply, replace_notification(state, "Snapshot cancelled")}

      {:error, reason} ->
        {:noreply, replace_notification(state, "Snapshot failed: #{inspect(reason)}")}
    end
  end

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
    OneshotCore.Events.unsubscribe(:snapshot)
    OneshotCore.Events.unsubscribe(:command)
    close_notification(state.notification)
    safe_invoke(fn -> :wxTaskBarIcon.removeIcon(state.icon) end)
    safe_invoke(fn -> :wxMenu.destroy(state.menu) end)
    safe_invoke(fn -> :wxFrame.destroy(state.frame) end)
    safe_invoke(fn -> :wx.destroy() end)
    :ok
  end

  @impl true
  def code_change(_old_vsn, state, _extra) do
    {:ok, state}
  end

  defp build_menu do
    menu = :wxMenu.new()
    :wxMenu.append(menu, @take_snapshot_id, ~c"Take Snapshot")
    :wxMenu.append(menu, @diagnostics_id, ~c"Diagnostics")
    :wxMenu.appendSeparator(menu)
    :wxMenu.append(menu, @exit_id, ~c"Exit")
    menu
  end

  defp set_icon(icon) do
    bitmap = :wxArtProvider.getBitmap(~c"wxART_INFORMATION", size: {16, 16})
    wx_icon = :wxIcon.new()
    :wxIcon.copyFromBitmap(wx_icon, bitmap)
    :wxTaskBarIcon.setIcon(icon, wx_icon, tooltip: ~c"OneShot")
  rescue
    error ->
      Logger.warning("Failed to set tray icon: #{Exception.message(error)}")
  end

  defp replace_notification(state, message) do
    close_notification(state.notification)
    notification = build_notification(message)
    safe_invoke(fn -> :wxNotificationMessage.show(notification) end)
    %{state | notification: notification}
  end

  defp build_notification(message) do
    :wxNotificationMessage.new(
      ~c"OneShot",
      message: String.to_charlist(message)
    )
  end

  defp close_notification(nil), do: :ok

  defp close_notification(notification) do
    safe_invoke(fn -> :wxNotificationMessage.close(notification) end)
  end

  defp safe_invoke(fun) do
    fun.()
    :ok
  rescue
    _ -> :ok
  catch
    _, _ -> :ok
  end
end
