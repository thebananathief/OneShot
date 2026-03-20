defmodule OneshotUi.WxSupervisor do
  @moduledoc false

  use Supervisor

  def start_link(args) do
    Supervisor.start_link(__MODULE__, args, name: __MODULE__)
  end

  @impl true
  def init(_args) do
    children = [
      %{
        id: OneshotUi.TrayHost,
        start: {OneshotUi.TrayHost, :start_link, [[]]},
        type: :worker,
        restart: :permanent,
        shutdown: 5_000
      }
    ]

    Supervisor.init(children, strategy: :one_for_one)
  end
end
