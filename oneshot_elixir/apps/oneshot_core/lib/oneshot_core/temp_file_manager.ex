defmodule OneshotCore.TempFileManager do
  @moduledoc false

  use GenServer

  alias OneshotCore.Config

  def start_link(config) do
    GenServer.start_link(__MODULE__, config, name: __MODULE__)
  end

  def allocate_drag_file_path do
    GenServer.call(__MODULE__, :allocate_drag_file_path)
  end

  def register(path) when is_binary(path) do
    GenServer.call(__MODULE__, {:register, path})
  end

  def release(path) when is_binary(path) do
    GenServer.call(__MODULE__, {:release, path})
  end

  @impl true
  def init(%Config{} = config) do
    File.mkdir_p!(config.temp_dir)
    cleanup_temp_files(config)
    {:ok, %{config: config, active_paths: MapSet.new()}}
  end

  @impl true
  def handle_call(:allocate_drag_file_path, _from, state) do
    path = Path.join(state.config.temp_dir, "drag_#{System.unique_integer([:positive])}.png")
    {:reply, path, state}
  end

  def handle_call({:register, path}, _from, state) do
    {:reply, :ok, %{state | active_paths: MapSet.put(state.active_paths, path)}}
  end

  def handle_call({:release, path}, _from, state) do
    _ = File.rm(path)
    {:reply, :ok, %{state | active_paths: MapSet.delete(state.active_paths, path)}}
  end

  defp cleanup_temp_files(config) do
    config.temp_dir
    |> Path.join("drag_*.png")
    |> Path.wildcard()
    |> Enum.each(&File.rm/1)
  end
end
