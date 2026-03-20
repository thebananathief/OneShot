defmodule OneshotCore.Application do
  @moduledoc false

  use Application

  @impl true
  def start(_type, _args) do
    config = OneshotCore.Config.fetch()

    children =
      [
        {OneshotCore.ConfigServer, config},
        {Registry, keys: :duplicate, name: OneshotCore.EventsRegistry},
        {OneshotCore.TempFileManager, config},
        {OneshotCore.CaptureCoordinator, []},
        {OneshotCore.InstanceLock, config}
      ] ++ ipc_children(config)

    opts = [strategy: :one_for_one, name: OneshotCore.Supervisor]
    Supervisor.start_link(children, opts)
  end

  defp ipc_children(config) do
    case OneshotCore.InstanceLock.peek_primary(config) do
      :primary ->
        [{OneshotCore.Ipc.Server, [config: config, handler: &OneshotCore.dispatch/1]}]

      :secondary ->
        []
    end
  end
end
