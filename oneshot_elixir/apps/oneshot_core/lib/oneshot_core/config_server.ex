defmodule OneshotCore.ConfigServer do
  @moduledoc false

  use GenServer

  def start_link(config) do
    GenServer.start_link(__MODULE__, config, name: __MODULE__)
  end

  def fetch do
    GenServer.call(__MODULE__, :fetch)
  end

  @impl true
  def init(config) do
    {:ok, config}
  end

  @impl true
  def handle_call(:fetch, _from, config) do
    {:reply, config, config}
  end
end
