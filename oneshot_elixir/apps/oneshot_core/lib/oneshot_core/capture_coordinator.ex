defmodule OneshotCore.CaptureCoordinator do
  @moduledoc false

  use GenServer

  alias OneshotCore.CommandEnvelope

  def start_link(_args) do
    GenServer.start_link(__MODULE__, %{commands: []}, name: __MODULE__)
  end

  def dispatch(%CommandEnvelope{} = envelope) do
    GenServer.call(__MODULE__, {:dispatch, envelope})
  end

  def request_snapshot(source) do
    dispatch(CommandEnvelope.new("snapshot"))
    OneshotCore.Events.broadcast(:snapshot, %{status: :requested, source: source})
    :ok
  end

  def recent_commands do
    GenServer.call(__MODULE__, :recent_commands)
  end

  @impl true
  def init(state) do
    {:ok, state}
  end

  @impl true
  def handle_call({:dispatch, envelope}, _from, state) do
    notify_subscribers(envelope)
    apply_side_effect(envelope)
    commands = [envelope | Enum.take(state.commands, 19)]
    {:reply, :ok, %{state | commands: commands}}
  end

  def handle_call(:recent_commands, _from, state) do
    {:reply, Enum.reverse(state.commands), state}
  end

  defp notify_subscribers(envelope) do
    OneshotCore.Events.broadcast(:command, envelope)
  end

  defp apply_side_effect(%CommandEnvelope{command: "install-startup"}) do
    _ = OneshotCore.StartupRegistration.install()
    :ok
  end

  defp apply_side_effect(%CommandEnvelope{command: "uninstall-startup"}) do
    _ = OneshotCore.StartupRegistration.uninstall()
    :ok
  end

  defp apply_side_effect(_envelope), do: :ok
end
