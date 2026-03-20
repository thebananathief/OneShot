defmodule OneshotCore.TempFileManager do
  @moduledoc false

  use GenServer

  alias OneshotCore.Config

  def start_link(config) do
    GenServer.start_link(__MODULE__, config, name: __MODULE__)
  end

  @impl true
  def init(%Config{} = config) do
    File.mkdir_p!(config.temp_dir)
    cleanup_temp_files(config)
    {:ok, config}
  end

  defp cleanup_temp_files(config) do
    config.temp_dir
    |> Path.join("drag_*.png")
    |> Path.wildcard()
    |> Enum.each(&File.rm/1)
  end
end
