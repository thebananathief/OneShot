defmodule OneshotUi.NotificationManager do
  @moduledoc false

  use GenServer

  @margin 16
  @gap 8

  def start_link(_args) do
    GenServer.start_link(__MODULE__, %{notifications: []}, name: __MODULE__)
  end

  def show(bitmap, path, temp_file) do
    GenServer.call(__MODULE__, {:show, bitmap, path, temp_file}, 30_000)
  end

  @impl true
  def init(state) do
    {:ok, state}
  end

  @impl true
  def handle_call({:show, bitmap, path, temp_file}, _from, state) do
    case OneshotUi.NotificationWindow.start_link(self(), bitmap, path, temp_file) do
      {:ok, pid} ->
        next =
          %{state | notifications: [%{pid: pid, temp_file: temp_file, path: path} | state.notifications]}
          |> reposition_notifications()

        {:reply, {:ok, pid}, next}

      other ->
        {:reply, other, state}
    end
  end

  @impl true
  def handle_info({:notification_closed, pid, temp_file}, state) do
    _ = OneshotUi.Output.release_temp_drag_file(temp_file)

    next =
      state.notifications
      |> Enum.reject(&(&1.pid == pid))
      |> then(&%{state | notifications: &1})
      |> reposition_notifications()

    {:noreply, next}
  end

  def handle_info({:notification_updated, pid, path, temp_file}, state) do
    notifications =
      Enum.map(state.notifications, fn entry ->
        if entry.pid == pid do
          %{entry | path: path, temp_file: temp_file}
        else
          entry
        end
      end)

    {:noreply, %{state | notifications: notifications}}
  end

  def handle_info(_message, state) do
    {:noreply, state}
  end

  defp reposition_notifications(state) do
    geometry = OneshotUi.VirtualScreen.geometry()
    top = geometry.top + @margin

    Enum.reduce(state.notifications, top, fn entry, acc_top ->
      case Process.alive?(entry.pid) do
        true ->
          {width, height} = OneshotUi.NotificationWindow.measure(entry.pid)
          left = geometry.left + geometry.width - width - @margin
          :ok = OneshotUi.NotificationWindow.move_to(entry.pid, {left, acc_top})
          acc_top + height + @gap

        false ->
          acc_top
      end
    end)

    state
  end
end
