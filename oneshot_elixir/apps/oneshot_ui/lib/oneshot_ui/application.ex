defmodule OneshotUi.Application do
  @moduledoc false

  use Application

  @impl true
  def start(_type, _args) do
    children =
      if Mix.env() == :test do
        []
      else
        [{OneshotUi.WxSupervisor, []}]
      end

    opts = [strategy: :one_for_one, name: OneshotUi.Supervisor]
    Supervisor.start_link(children, opts)
  end
end
